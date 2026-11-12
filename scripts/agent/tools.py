"""The tools the reference agent offers its model.

Files are read and written through the editor (fs/read_text_file and
fs/write_text_file), so a read sees unsaved edits and every write goes
through the editor's review. Commands run through terminal/create, so the
editor decides whether they may run. Listing and searching read the disk
here. The _crlf/ tools exist only when the client is CRLF.
"""

import fnmatch
import json
import os
import re

# Kept from one tool result for the model. The rest is cut with a note.
MAX_RESULT = 30000
MAX_READ_LINES = 2000
MAX_LIST_ENTRIES = 500
MAX_MATCHES = 200
SKIPPED_DIRECTORIES = {"node_modules", "__pycache__"}


class ToolError(Exception):
    pass


def clip(text, limit=MAX_RESULT):
    if len(text) <= limit:
        return text
    return text[:limit] + "\n[cut: %d more characters]" % (len(text) - limit)


def _schema(properties, required=()):
    return {"type": "object", "properties": properties, "required": list(required)}


def _string(description):
    return {"type": "string", "description": description}


def _integer(description):
    return {"type": "integer", "description": description}


def _walk(root, depth=None):
    """Directories that start with a dot, node_modules, and __pycache__ are skipped."""
    base = root.rstrip("/\\").count(os.sep)
    for directory, names, files in os.walk(root):
        names[:] = sorted(n for n in names if not n.startswith(".") and n not in SKIPPED_DIRECTORIES)
        if depth is not None and directory.count(os.sep) - base >= depth:
            names[:] = []
        yield directory, names, sorted(files)


class Tools:
    def __init__(self, context, crlf_client):
        self.context = context
        self.definitions = [
            self._define("read_file", "Read a text file. Lines are numbered. The editor's unsaved text is what you see.",
                         _schema({"path": _string("File path, absolute or relative to the project"),
                                  "line": _integer("First line to read, starting at 1"),
                                  "limit": _integer("How many lines to read")}, ["path"]),
                         "read", self.read_file, lambda a: "Read " + self._shown(a.get("path", ""))),
            self._define("write_file", "Create or replace a whole file. The user reviews the change before it is saved.",
                         _schema({"path": _string("File path"), "content": _string("The complete new text")},
                                 ["path", "content"]),
                         "edit", self.write_file, lambda a: "Write " + self._shown(a.get("path", ""))),
            self._define("edit_file", "Replace exact text in a file. old_text must match once, unless replace_all is true. "
                                      "Prefer this to write_file for a small change.",
                         _schema({"path": _string("File path"),
                                  "old_text": _string("Text to find, exactly as it appears"),
                                  "new_text": _string("Text to put in its place"),
                                  "replace_all": {"type": "boolean", "description": "Replace every match"}},
                                 ["path", "old_text", "new_text"]),
                         "edit", self.edit_file, lambda a: "Edit " + self._shown(a.get("path", ""))),
            self._define("list_directory", "List files and folders. Dot folders and node_modules are left out.",
                         _schema({"path": _string("Folder, relative to the project. The default is the project"),
                                  "depth": _integer("How many levels to descend. The default is 2")}),
                         "read", self.list_directory, lambda a: "List " + self._shown(a.get("path", "."))),
            self._define("search", "Search file contents with a regular expression. Reads the files on disk.",
                         _schema({"pattern": _string("Python regular expression"),
                                  "path": _string("Folder to search. The default is the project"),
                                  "glob": _string("File name pattern such as *.cpp"),
                                  "ignore_case": {"type": "boolean", "description": "Match without case"}},
                                 ["pattern"]),
                         "search", self.search, lambda a: "Search for " + a.get("pattern", "")),
            self._define("run_command", "Run a shell command in the project and return its output and exit code. "
                                        "The user may be asked first.",
                         _schema({"command": _string("The command line"),
                                  "cwd": _string("Working directory. The default is the project"),
                                  "timeout_seconds": _integer("Stop the command after this long. The default is 120")},
                                 ["command"]),
                         "execute", self.run_command, lambda a: "Run " + a.get("command", "")),
        ]
        if crlf_client:
            self.definitions += [
                self._define("editor_state", "The files open in the editor, which one is in front, the caret, and the selection.",
                             _schema({}), "read", self._extension("_crlf/editor/state"), lambda a: "Read the editor state"),
                self._define("problems", "Errors and warnings the project's syntax check reported for open files.",
                             _schema({}), "read", self._extension("_crlf/problems"), lambda a: "Read the problems"),
                self._define("git_status", "Branch, upstream, and changed files of each git repository in the project.",
                             _schema({}), "read", self._extension("_crlf/git/status"), lambda a: "Read git status"),
                self._define("read_terminal", "The text in the user's terminal: scrollback and screen.",
                             _schema({}), "read", self._extension("_crlf/terminal/read"), lambda a: "Read the terminal"),
            ]
        self.by_name = {d["name"]: d for d in self.definitions}

    @staticmethod
    def _define(name, description, parameters, kind, run, title):
        return {"name": name, "description": description, "parameters": parameters, "kind": kind, "run": run,
                "title": title}

    def schemas(self):
        return [{k: d[k] for k in ("name", "description", "parameters")} for d in self.definitions]

    def _path(self, path):
        return os.path.normpath(os.path.join(self.context.cwd, path or "."))

    def _shown(self, path):
        full = self._path(path)
        relative = os.path.relpath(full, self.context.cwd)
        return full if relative.startswith("..") else relative

    def _read(self, path):
        return self.context.request("fs/read_text_file", {"path": path})["content"]

    # Each tool returns (text for the model, ACP tool-call content, locations).

    def read_file(self, args):
        path = self._path(args["path"])
        params = {"path": path}
        first = int(args.get("line") or 1)
        if args.get("line"):
            params["line"] = first
        if args.get("limit"):
            params["limit"] = int(args["limit"])
        lines = self.context.request("fs/read_text_file", params)["content"].split("\n")
        note = ""
        if len(lines) > MAX_READ_LINES:
            note = "\n[%d more lines: read again with line and limit]" % (len(lines) - MAX_READ_LINES)
            lines = lines[:MAX_READ_LINES]
        text = "\n".join("%6d|%s" % (first + i, line) for i, line in enumerate(lines)) + note
        return text, [], [{"path": path, "line": first}]

    def _write(self, path, old, new):
        self.context.request("fs/write_text_file", {"path": path, "content": new})
        diff = {"type": "diff", "path": path, "oldText": old, "newText": new}
        return "Wrote %s" % self._shown(path), [diff], [{"path": path}]

    def write_file(self, args):
        path = self._path(args["path"])
        try:
            old = self._read(path)
        except ToolError:
            old = None
        return self._write(path, old, args["content"])

    def edit_file(self, args):
        path = self._path(args["path"])
        old = self._read(path)
        count = old.count(args["old_text"])
        if count == 0:
            raise ToolError("old_text was not found in %s" % self._shown(path))
        if count > 1 and not args.get("replace_all"):
            raise ToolError("old_text appears %d times in %s. Add surrounding lines, or set replace_all."
                            % (count, self._shown(path)))
        return self._write(path, old, old.replace(args["old_text"], args["new_text"]))

    def list_directory(self, args):
        root = self._path(args.get("path"))
        depth = int(args.get("depth") or 2)
        lines = []
        for directory, names, files in _walk(root, depth):
            indent = "  " * (directory.count(os.sep) - root.rstrip("/\\").count(os.sep))
            if directory != root:
                lines.append(indent[2:] + os.path.basename(directory) + "/")
            lines += [indent + name for name in files]
            if len(lines) >= MAX_LIST_ENTRIES:
                lines.append("[stopped at %d entries]" % MAX_LIST_ENTRIES)
                break
        return "\n".join(lines) or "(empty)", [], [{"path": root}]

    def search(self, args):
        flags = re.IGNORECASE if args.get("ignore_case") else 0
        try:
            pattern = re.compile(args["pattern"], flags)
        except re.error as error:
            raise ToolError("Not a regular expression: %s" % error)
        root = self._path(args.get("path"))
        glob = args.get("glob")
        matches = []
        for directory, _, files in _walk(root):
            for name in files:
                if glob and not fnmatch.fnmatch(name, glob):
                    continue
                path = os.path.join(directory, name)
                try:
                    if os.path.getsize(path) > 1024 * 1024:
                        continue
                    with open(path, "rb") as handle:
                        data = handle.read()
                except OSError:
                    continue
                if b"\0" in data[:1024]:
                    continue
                for number, line in enumerate(data.decode("utf-8", "replace").splitlines(), 1):
                    if pattern.search(line):
                        matches.append("%s:%d: %s" % (os.path.relpath(path, self.context.cwd), number, line.strip()))
                        if len(matches) >= MAX_MATCHES:
                            return "\n".join(matches) + "\n[stopped at %d matches]" % MAX_MATCHES, [], []
        return "\n".join(matches) or "No matches.", [], []

    def run_command(self, args):
        cwd = self._path(args.get("cwd"))
        timeout = int(args.get("timeout_seconds") or 120)
        created = self.context.request("terminal/create", {"command": args["command"], "args": [], "cwd": cwd,
                                                           "outputByteLimit": 64000})
        terminal = created["terminalId"]
        self.context.show_terminal(terminal)
        try:
            try:
                status = self.context.request("terminal/wait_for_exit", {"terminalId": terminal}, timeout=timeout)
            except TimeoutError:
                self.context.request("terminal/kill", {"terminalId": terminal})
                status = self.context.request("terminal/wait_for_exit", {"terminalId": terminal})
                status["timedOut"] = True
            output = self.context.request("terminal/output", {"terminalId": terminal})
        finally:
            self.context.request("terminal/release", {"terminalId": terminal})
        if status.get("timedOut"):
            head = "Stopped after %d seconds." % timeout
        elif status.get("exitCode") is None:
            head = "Ended by %s." % (status.get("signal") or "a signal")
        else:
            head = "Exit code %d." % status["exitCode"]
        cut = " The start of the output was cut." if output.get("truncated") else ""
        return "%s%s\n%s" % (head, cut, output.get("output", "")), [{"type": "terminal", "terminalId": terminal}], []

    def _extension(self, method):
        def run(args):
            result = self.context.request(method, {})
            return json.dumps(result, indent=1, ensure_ascii=False), [], []
        return run
