#!/usr/bin/env python3
"""JSON-line completion in front of clangd or a C# language server.

The editor does not speak LSP. This process does. One request line in, one
reply line out. See docs/completion.md.
"""

import json
import os
import shutil
import subprocess
import sys
import threading

CPP = {
    "c": "c",
    "h": "cpp",
    "hh": "cpp",
    "hpp": "cpp",
    "hxx": "cpp",
    "cc": "cpp",
    "cpp": "cpp",
    "cxx": "cpp",
    "cppm": "cpp",
    "ccm": "cpp",
    "cxxm": "cpp",
    "c++": "cpp",
    "m": "cpp",
    "mm": "cpp",
    "inc": "cpp",
}


def uri_for(path):
    if not path:
        return "untitled:buffer"
    text = path.replace("\\", "/")
    if text.startswith("/"):
        return "file://" + text
    return "file:///" + text


def read_message(stream):
    length = None
    while True:
        line = stream.readline()
        if not line:
            return None
        if line in (b"\r\n", b"\n"):
            break
        key, _, value = line.decode("ascii", "replace").partition(":")
        if key.strip().lower() == "content-length":
            length = int(value.strip())
    if length is None:
        return None
    return json.loads(stream.read(length).decode("utf-8"))


def send(proc, message):
    body = json.dumps(message).encode("utf-8")
    proc.stdin.write(("Content-Length: %d\r\n\r\n" % len(body)).encode("ascii") + body)
    proc.stdin.flush()


class Server:
    def __init__(self, argv):
        self.proc = subprocess.Popen(
            argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL
        )
        self.next_id = 1
        self.versions = {}
        self.opened = set()
        self.inbox = []
        self.lock = threading.Lock()
        self.ready = threading.Condition(self.lock)
        threading.Thread(target=self._read, daemon=True).start()
        self.request(
            "initialize",
            {
                "processId": os.getpid(),
                "capabilities": {"textDocument": {"completion": {"completionItem": {}}}},
            },
        )
        self.notify("initialized", {})

    def _read(self):
        while True:
            message = read_message(self.proc.stdout)
            if message is None:
                with self.lock:
                    self.ready.notify_all()
                return
            if "id" in message and "method" not in message:
                with self.lock:
                    self.inbox.append(message)
                    self.ready.notify_all()

    def _send(self, message):
        send(self.proc, message)

    def request(self, method, params):
        number = self.next_id
        self.next_id += 1
        self._send({"jsonrpc": "2.0", "id": number, "method": method, "params": params})
        with self.lock:
            while True:
                for index, message in enumerate(self.inbox):
                    if message.get("id") == number:
                        self.inbox.pop(index)
                        return message.get("result")
                self.ready.wait()

    def notify(self, method, params):
        self._send({"jsonrpc": "2.0", "method": method, "params": params})

    def sync(self, path, language, text):
        uri = uri_for(path)
        if uri not in self.opened:
            self.opened.add(uri)
            self.versions[uri] = 1
            self.notify(
                "textDocument/didOpen",
                {"textDocument": {"uri": uri, "languageId": language, "version": 1, "text": text}},
            )
            return uri
        self.versions[uri] += 1
        self.notify(
            "textDocument/didChange",
            {
                "textDocument": {"uri": uri, "version": self.versions[uri]},
                "contentChanges": [{"text": text}],
            },
        )
        return uri

    def complete(self, path, language, text, line, column):
        uri = self.sync(path, language, text)
        result = self.request(
            "textDocument/completion",
            {
                "textDocument": {"uri": uri},
                "position": {"line": max(0, line - 1), "character": max(0, column - 1)},
            },
        )
        rows = result.get("items", []) if isinstance(result, dict) else result or []
        items = []
        for row in rows[:40]:
            insert = row.get("insertText") or row.get("label") or ""
            edit = row.get("textEdit") or {}
            span = edit.get("range")
            if edit.get("newText"):
                insert = edit["newText"]
            if span:
                start = span["start"]
                end = span["end"]
                replace_line = start["line"] + 1
                replace_column = start["character"] + 1
                replace_end = end["character"] + 1
            else:
                replace_line, replace_column, replace_end = word_range(text, line, column)
            items.append(
                {
                    "label": row.get("label") or insert,
                    "detail": row.get("detail") or "",
                    "insert": insert,
                    "replaceLine": replace_line,
                    "replaceColumn": replace_column,
                    "replaceEndColumn": replace_end,
                }
            )
        return items


def word_range(text, line, column):
    rows = text.split("\n")
    if line < 1 or line > len(rows):
        return line, column, column
    row = rows[line - 1]
    end = min(max(column - 1, 0), len(row))
    start = end
    while start > 0 and (row[start - 1].isalnum() or row[start - 1] == "_"):
        start -= 1
    return line, start + 1, end + 1


def roslyn_argv():
    if shutil.which("OmniSharp"):
        return ["OmniSharp", "-lsp"]
    if not shutil.which("dotnet"):
        return None
    listed = subprocess.check_output(["dotnet", "--list-sdks"], text=True, stderr=subprocess.DEVNULL)
    lines = [line for line in listed.splitlines() if line.strip()]
    if not lines:
        return None
    version, _, folder = lines[-1].partition(" ")
    folder = folder.strip().lstrip("[").rstrip("]")
    root = os.path.join(folder, version.strip())
    for dirpath, _, files in os.walk(root):
        if "Microsoft.CodeAnalysis.LanguageServer.dll" in files:
            return ["dotnet", "exec", os.path.join(dirpath, "Microsoft.CodeAnalysis.LanguageServer.dll")]
    return None


def reply(payload):
    print(json.dumps(payload), flush=True)


def main():
    servers = {}
    missing = set()
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        request = json.loads(line)
        number = request.get("id")
        revision = request.get("revision")
        path = request.get("path") or ""
        extension = os.path.splitext(path)[1].lstrip(".").lower()
        language = CPP.get(extension)
        key = None
        argv = None
        error = None
        if language:
            key = "cpp"
            argv = ["clangd", "--background-index"] if shutil.which("clangd") else None
            error = "clangd not found"
        elif extension == "cs":
            key = "cs"
            language = "csharp"
            argv = roslyn_argv()
            error = "No C# language server found"
        if key is None:
            reply({"id": number, "revision": revision, "items": []})
            continue
        if argv is None:
            if key not in missing:
                missing.add(key)
                reply({"id": number, "revision": revision, "error": error})
            else:
                reply({"id": number, "revision": revision, "items": []})
            continue
        if key not in servers:
            servers[key] = Server(argv)
        items = servers[key].complete(path, language, request.get("text") or "", request.get("line") or 1, request.get("column") or 1)
        reply({"id": number, "revision": revision, "items": items})


if __name__ == "__main__":
    main()
