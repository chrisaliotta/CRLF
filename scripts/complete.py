#!/usr/bin/env python3
"""JSON-line completion in front of clangd or a C# language server.

The editor does not speak LSP. This process does. Requests and replies are
one JSON object per line. Each server answers on its own thread, and only the
newest request of each kind is answered. Every file the editor names is given
a state, and every change of that state is one line. See docs/completion.md.
"""

import collections
import functools
import json
import os
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse

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

# CompletionItemKind in the language server protocol, by number.
KINDS = (
    "",
    "text",
    "method",
    "function",
    "constructor",
    "field",
    "variable",
    "class",
    "interface",
    "module",
    "property",
    "unit",
    "value",
    "enum",
    "keyword",
    "snippet",
    "color",
    "file",
    "reference",
    "folder",
    "enumMember",
    "constant",
    "struct",
    "event",
    "operator",
    "typeParameter",
)

# A list longer than this is cut to the rows that match the word being typed
# and marked incomplete, so the editor asks again as the word changes.
MAX_ITEMS = 1000

VERBOSE = "--verbose" in sys.argv or os.environ.get("CRLF_COMPLETE_VERBOSE") == "1"


def linux_path(path):
    """A distro path for a server that is running inside WSL."""
    text = path.replace("\\", "/")
    lowered = text.lower()
    for prefix in ("//wsl.localhost/", "//wsl$/"):
        if lowered.startswith(prefix):
            rest = text[len(prefix):]
            slash = rest.find("/")
            return "/" if slash < 0 else rest[slash:]
    return path


def uri_for(path):
    """Percent-encoded, which is the form a server sends back and compares."""
    text = linux_path(path).replace("\\", "/")
    if not text.startswith("/"):
        text = "/" + text
    return "file://" + urllib.parse.quote(text, safe="/:")


def name_of(path):
    return os.path.basename(linux_path(path).replace("\\", "/"))


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


OUTPUT_LOCK = threading.Lock()


def log(message):
    with OUTPUT_LOCK:
        print(message, file=sys.stderr, flush=True)


def reply(payload):
    with OUTPUT_LOCK:
        print(json.dumps(payload), flush=True)


class FileStates:
    """What the editor shows for each file. One reply line per change.

    starting, loading and ready are the path to a working server. unsupported,
    missing and failed are where a file stops when it has none.
    """

    def __init__(self):
        self.lock = threading.Lock()
        self.files = {}

    def set(self, path, state, server="", message="", triggers=(), announce=False):
        entry = {
            "kind": "state",
            "path": path,
            "state": state,
            "server": server,
            "message": message or state,
            "triggers": list(triggers),
        }
        # Written inside the lock, so two threads cannot report out of order.
        with self.lock:
            if self.files.get(path) == entry and not announce:
                return
            self.files[path] = entry
            reply(entry)

    def announce(self, path):
        """Says the state again. An open asks for it, so the editor can forget."""
        with self.lock:
            reply(self.files[path])


class Summary:
    """The Activity line on the Processes page: the servers, not the files."""

    def __init__(self):
        self.lock = threading.Lock()
        self.servers = {}
        self.notes = {}
        self.shown = None

    def set(self, key, state):
        with self.lock:
            self.servers[key] = state
        self._publish()

    def note(self, key, text):
        """Extra text such as analysis progress. None clears it."""
        with self.lock:
            if text:
                self.notes[key] = text
            else:
                self.notes.pop(key, None)
        self._publish()

    def _publish(self):
        with self.lock:
            states = list(self.servers.values())
            counts = ["%d %s" % (states.count(state), state) for state in ("starting", "ready", "failed") if state in states]
            parts = (["servers: " + ", ".join(counts)] if counts else []) + list(self.notes.values())[:1]
            line = "; ".join(parts) or "idle"
            if line == self.shown:
                return
            self.shown = line
        # The editor shows this line as Activity and does not keep it in the log.
        log("CRLF_STATUS:" + line)


STATES = FileStates()
SUMMARY = Summary()


def analysis_text(method, params):
    """The progress text for one server notification, or None once it has finished."""
    if method == "$/progress":
        value = params.get("value") or {}
        if value.get("kind") == "end":
            return None
        text = value.get("message") or value.get("title") or ""
        if value.get("percentage") is not None:
            text = "%s %d%%" % (text, value["percentage"])
        return text.strip() or None
    if method == "o#/backgrounddiagnosticstatus":
        total = params.get("NumberFilesTotal") or 0
        if params.get("Status") == 2 or not total:
            return None
        return "analyzing %d/%d" % (total - (params.get("NumberFilesRemaining") or 0), total)
    if params.get("Status") == 1:
        return None
    return ("analyzing " + (params.get("ProjectFilePath") or "")).strip()


class Server:
    """One language server process and the documents it has been given."""

    ANALYSIS = ("$/progress", "o#/backgrounddiagnosticstatus", "o#/projectdiagnosticstatus")
    PROJECT = ("o#/projectadded", "o#/projectchanged")

    def __init__(self, argv, root, on_note):
        self.proc = subprocess.Popen(
            argv,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd=root,
        )
        self.on_note = on_note
        # Set by the owner once initialize has returned.
        self.on_exit = None
        self.on_project = None
        self.next_id = 1
        self.versions = {}
        self.texts = {}
        # Documents the server has answered a request for.
        self.probed = set()
        self.inbox = []
        self.alive = True
        self.lock = threading.Lock()
        self.write_lock = threading.Lock()
        self.arrived = threading.Condition(self.lock)
        self.root_uri = uri_for(root)
        self.root_name = os.path.basename(root) or "project"
        threading.Thread(target=self._read, daemon=True).start()
        threading.Thread(target=self._drain, daemon=True).start()
        # OmniSharp asks the client questions while it loads a project, and
        # it will not answer initialize until those questions are answered.
        try:
            result = self.request(
                "initialize",
                {
                    "processId": os.getpid(),
                    "rootUri": self.root_uri,
                    "workspaceFolders": [{"uri": self.root_uri, "name": self.root_name}],
                    "capabilities": {
                        "workspace": {"configuration": True, "workspaceFolders": True},
                        "window": {"workDoneProgress": True},
                        "textDocument": {
                            "completion": {"contextSupport": True, "completionItem": {"preselectSupport": True}}
                        },
                    },
                },
            )
            self.notify("initialized", {})
        except Exception:
            self.proc.kill()
            raise
        provider = (result.get("capabilities") or {}).get("completionProvider") or {}
        self.triggers = provider.get("triggerCharacters") or []
        self.ready_at = time.monotonic()

    def _drain(self):
        for line in self.proc.stderr:
            text = line.decode("utf-8", "replace").strip()
            if text:
                log(text)

    def _read(self):
        while True:
            message = read_message(self.proc.stdout)
            if message is None:
                with self.lock:
                    self.alive = False
                    self.arrived.notify_all()
                if self.on_exit:
                    self.on_exit(self)
                return
            method = message.get("method")
            if method == "window/logMessage":
                params = message.get("params") or {}
                text = params.get("message") or ""
                # 1 error, 2 warning, 3 information, 4 log.
                if text and (VERBOSE or (params.get("type") or 4) <= 2):
                    log(text)
                continue
            if method in self.ANALYSIS:
                self.on_note(analysis_text(method, message.get("params") or {}))
                continue
            if method in self.PROJECT:
                if self.on_project:
                    self.on_project()
                continue
            if "id" in message and method:
                self._answer(message)
                continue
            if "id" in message:
                with self.lock:
                    self.inbox.append(message)
                    self.arrived.notify_all()

    def _answer(self, message):
        method = message.get("method")
        if method == "workspace/configuration":
            items = (message.get("params") or {}).get("items") or []
            result = [{} for _ in items]
        elif method == "workspace/workspaceFolders":
            result = [{"uri": self.root_uri, "name": self.root_name}]
        else:
            result = None
        self._send({"jsonrpc": "2.0", "id": message.get("id"), "result": result})

    def _send(self, message):
        with self.write_lock:
            send(self.proc, message)

    def request(self, method, params):
        number = self.next_id
        self.next_id += 1
        self._send({"jsonrpc": "2.0", "id": number, "method": method, "params": params})
        with self.lock:
            while self.alive:
                for index, message in enumerate(self.inbox):
                    if message.get("id") == number:
                        self.inbox.pop(index)
                        error = message.get("error")
                        if error:
                            text = error.get("message") if isinstance(error, dict) else str(error)
                            raise RuntimeError((text or "language server request failed").splitlines()[0])
                        return message.get("result")
                self.arrived.wait()
        raise RuntimeError("language server exited")

    def notify(self, method, params):
        self._send({"jsonrpc": "2.0", "method": method, "params": params})

    def sync(self, path, language, text):
        """Gives the server this buffer, unless it is the text it already has."""
        uri = uri_for(path)
        if self.texts.get(uri) == text:
            return uri
        if uri not in self.versions:
            self.versions[uri] = 1
            self.notify(
                "textDocument/didOpen",
                {"textDocument": {"uri": uri, "languageId": language, "version": 1, "text": text}},
            )
        # OmniSharp reads the saved file on didOpen and ignores the text sent
        # with it, so the buffer is always followed by a change.
        self.texts[uri] = text
        self.versions[uri] += 1
        self.notify(
            "textDocument/didChange",
            {
                "textDocument": {"uri": uri, "version": self.versions[uri]},
                "contentChanges": [{"text": text}],
            },
        )
        return uri

    def probe(self, uri):
        """Returns once the server has taken the document in.

        A server answers in the order it was asked, so a reply about the
        document means the open and the change before it have been applied.
        """
        if uri in self.probed:
            return
        self.request("textDocument/documentSymbol", {"textDocument": {"uri": uri}})
        self.probed.add(uri)

    def complete(self, uri, line, column, trigger):
        """The server's rows and whether it says there are more."""
        context = {"triggerKind": 2, "triggerCharacter": trigger} if trigger else {"triggerKind": 1}
        result = self.request(
            "textDocument/completion",
            {
                "textDocument": {"uri": uri},
                "position": {"line": line - 1, "character": column - 1},
                "context": context,
            },
        )
        if isinstance(result, dict):
            return result.get("items") or [], bool(result.get("isIncomplete"))
        return result or [], False

    def signature(self, uri, line, column):
        result = self.request(
            "textDocument/signatureHelp",
            {
                "textDocument": {"uri": uri},
                "position": {"line": line - 1, "character": column - 1},
            },
        ) or {}
        rows = result.get("signatures") or []
        active_signature = result.get("activeSignature") or 0
        if active_signature >= len(rows):
            active_signature = 0
        signatures = []
        for index, row in enumerate(rows):
            parameters = []
            for param in row.get("parameters") or []:
                label = param.get("label")
                if isinstance(label, str):
                    parameters.append(label)
                elif isinstance(label, list) and len(label) == 2:
                    parameters.append((row.get("label") or "")[label[0]:label[1]])
            active = 0
            if index == active_signature:
                active = row.get("activeParameter", result.get("activeParameter") or 0)
            signatures.append({"label": row.get("label") or "", "parameters": parameters, "active": active})
        return signatures, active_signature


def documentation(value):
    if isinstance(value, str):
        return value
    if isinstance(value, dict):
        return value.get("value") or ""
    return ""


def detail_of(row):
    details = row.get("labelDetails") or {}
    return row.get("detail") or details.get("detail") or details.get("description") or ""


def word_range(text, line, column):
    """The line and the 1-based columns of the word that ends at the caret."""
    row = text.split("\n")[line - 1]
    end = column - 1
    start = end
    while start > 0 and (row[start - 1].isalnum() or row[start - 1] == "_"):
        start -= 1
    return line, start + 1, end + 1


def edit_for(edit):
    start = edit["range"]["start"]
    end = edit["range"]["end"]
    return {
        "line": start["line"] + 1,
        "column": start["character"] + 1,
        "endLine": end["line"] + 1,
        "endColumn": end["character"] + 1,
        "text": edit.get("newText") or "",
    }


def item_for(row, token, word):
    """One row for the editor. Keys the editor can default are left out."""
    label = row.get("label") or row.get("insertText") or ""
    edit = row.get("textEdit") or {}
    insert = edit.get("newText") or row.get("insertText") or label
    span = edit.get("range") or edit.get("replace")
    if span:
        line, column, end = span["start"]["line"] + 1, span["start"]["character"] + 1, span["end"]["character"] + 1
    else:
        line, column, end = word
    item = {"label": label, "data": token, "replaceLine": line, "replaceColumn": column, "replaceEndColumn": end}
    optional = {
        "detail": detail_of(row),
        "documentation": documentation(row.get("documentation")),
        "insert": insert if insert != label else "",
        "kind": KINDS[row.get("kind") or 0],
        "filterText": row.get("filterText") if row.get("filterText") != label else "",
        "preselect": bool(row.get("preselect")),
        "additionalEdits": [edit_for(extra) for extra in row.get("additionalTextEdits") or []],
    }
    item.update({key: value for key, value in optional.items() if value})
    return item


def answer_empty(request):
    """The reply for a request no server will answer. An open or a resolve has none."""
    kind = request["kind"]
    if kind == "signature":
        reply({"id": request["id"], "revision": request["revision"], "signatures": []})
    elif kind == "completion":
        reply({"id": request["id"], "revision": request["revision"], "items": []})


# Where a file's server comes from. argv is None when the toolchain is not
# installed, and `missing` is what the editor shows then.
Route = collections.namedtuple("Route", "key argv root language label context missing")


def single_server(key, argv, languages, label):
    """A provider with one server for the whole working directory."""

    def route(path, extension):
        found = argv if shutil.which(argv[0]) else None
        return Route(key, found, os.getcwd(), languages[extension], label, None, label + " not found")

    return route


@functools.lru_cache(maxsize=None)
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


def norm(path):
    return os.path.normpath(linux_path(path))


class CSharp:
    """One server per .csproj, and one for the files that belong to none."""

    SKIP_DIRS = {"bin", "obj", ".git", "node_modules", ".vs"}
    MISSING = "No C# language server found"

    def __init__(self, root):
        SUMMARY.note("scan", "scanning for C# projects")
        self.projects = []
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [name for name in dirnames if name not in self.SKIP_DIRS]
            self.projects += [os.path.abspath(os.path.join(dirpath, name)) for name in filenames if name.endswith(".csproj")]
        log("%d C# projects" % len(self.projects))
        SUMMARY.note("scan", None)
        self.loose_root = None

    def route(self, path, extension):
        project = self._owner(path) or self._above(path)
        if project:
            return Route(
                project,
                self._argv(project),
                os.path.dirname(project),
                "csharp",
                os.path.basename(project),
                "using " + project,
                self.MISSING,
            )
        if self.loose_root is None:
            self.loose_root = tempfile.mkdtemp(prefix="crlf-loose-")
        return Route("loose", self._argv(None), self.loose_root, "csharp", "loose files", "loose file", self.MISSING)

    def _owner(self, path):
        """The project whose directory is the longest prefix of the path."""
        text = norm(path)
        owners = [project for project in self.projects if text.startswith(os.path.dirname(norm(project)) + os.sep)]
        return max(owners, key=lambda project: len(os.path.dirname(project)), default=None)

    def _above(self, path):
        """A project above the session directory is still the owner of its files."""
        directory = os.path.dirname(norm(path))
        while True:
            names = sorted(name for name in os.listdir(directory) if name.endswith(".csproj"))
            if names:
                project = os.path.abspath(os.path.join(directory, names[0]))
                self.projects.append(project)
                return project
            parent = os.path.dirname(directory)
            if parent == directory:
                return None
            directory = parent

    def _argv(self, project):
        base = roslyn_argv()
        if not base or base[0] != "OmniSharp":
            return base
        # Errors only, unless --verbose. Analyzing open documents only skips the
        # whole-project pass that delays the initialize reply.
        argv = base + ["-l", "Information" if VERBOSE else "Error"]
        # OmniSharp rejects a .csproj. The project directory loads that project
        # and follows its ProjectReferences without taking in sibling projects.
        if project:
            argv += ["-s", os.path.dirname(project)]
        return argv + ["RoslynExtensionsOptions:AnalyzeOpenDocumentsOnly=true"]


class Worker:
    """One server's requests, on their own thread.

    A project that is loading does not hold up a loose file or C++. Requests
    that piled up behind a slow one are skipped: only the newest of each kind
    is answered, because the editor drops any older reply. Every open is kept,
    one per file, because each one loads a different document.
    """

    RETRY_WINDOW = 60
    RETRIES = 3
    # A server that exits is started again with the files it had, this many times.
    RESTARTS = 2

    def __init__(self, route):
        self.route = route
        self.server = None
        # Why this server is not coming back, once that is known.
        self.failed = None
        self.exits = 0
        # The server's own rows for the last list, by the token the editor holds.
        self.rows = {}
        self.paths = {}
        self.guard = threading.Lock()
        self.queue = queue.Queue()
        threading.Thread(target=self._run, daemon=True).start()

    def put(self, request):
        path = request["path"]
        with self.guard:
            known = path in self.paths
            # The newest buffer for each file, so a server that is started
            # again can be given every file it had.
            if not known or "text" in request:
                self.paths[path] = request
        if known and request["kind"] == "open":
            STATES.announce(path)
        elif not known:
            # Said here so a file has a state before its turn comes. Handling
            # the request is what settles it.
            state = "starting" if self.server is None else "loading"
            self._say(path, state, "%s %s" % (state, self.route.label))
        self.queue.put(request)

    def _say(self, path, state, message):
        triggers = self.server.triggers if self.server else ()
        STATES.set(path, state, self.route.label, message, triggers)

    def _say_all(self, state, message):
        with self.guard:
            paths = list(self.paths)
        for path in paths:
            self._say(path, state, message)

    def _take(self):
        pending = [self.queue.get()]
        while True:
            try:
                pending.append(self.queue.get_nowait())
            except queue.Empty:
                break
        newest = {}
        for request in pending:
            slot = (request["kind"], request.get("path")) if request["kind"] == "open" else request["kind"]
            newest.pop(slot, None)
            newest[slot] = request
        skipped = len(pending) - len(newest)
        if skipped:
            log("%d older requests skipped" % skipped)
        return list(newest.values())

    def _run(self):
        while True:
            for request in self._take():
                self._handle(request)

    def _start(self):
        route = self.route
        if route.context:
            log(route.context)
        log("starting " + " ".join(route.argv))
        SUMMARY.set(route.key, "starting")
        self._say_all("starting", "starting " + route.label)
        # OmniSharp holds its own progress notifications until initialized,
        # so elapsed time is the only sign of life while it loads a project.
        started = time.monotonic()
        loaded = threading.Event()
        gate = threading.Lock()

        def tick():
            while not loaded.wait(2):
                with gate:
                    if not loaded.is_set():
                        self._say_all("starting", "starting %s (%ds)" % (route.label, time.monotonic() - started))

        threading.Thread(target=tick, daemon=True).start()
        try:
            server = Server(route.argv, route.root, lambda text: SUMMARY.note(route.key, text))
        except Exception as exc:
            with gate:
                loaded.set()
            self._fail("%s failed to start: %s" % (route.label, exc))
            return
        with gate:
            loaded.set()
        self.server = server
        server.on_exit = self._exited
        server.on_project = lambda: self.queue.put({"kind": "project"})
        log("%s loaded in %ds" % (route.label, server.ready_at - started))
        SUMMARY.set(route.key, "ready")
        self._say_all("loading", "loading " + route.label)
        if self.exits:
            with self.guard:
                held = list(self.paths.values())
            for request in held:
                self.queue.put(dict(request, kind="open"))

    def _fail(self, message):
        self.failed = message
        log(message)
        SUMMARY.set(self.route.key, "failed")
        self._say_all("failed", message)

    def _exited(self, server):
        """Called from the server's reader, and from a request that found it gone."""
        with self.guard:
            if self.server is not server:
                return
            self.server = None
        # Only exits close together count against the limit.
        if time.monotonic() - server.ready_at > self.RETRY_WINDOW:
            self.exits = 0
        self.exits += 1
        self.rows = {}
        SUMMARY.note(self.route.key, None)
        if self.exits > self.RESTARTS:
            self._fail("%s exited %d times" % (self.route.label, self.exits))
            return
        log(self.route.label + " exited")
        SUMMARY.set(self.route.key, "failed")
        self._say_all("failed", self.route.label + " exited")
        self.queue.put({"kind": "restart"})

    def _handle(self, request):
        kind = request["kind"]
        if kind == "project":
            self._reload()
            return
        if self.server is None and self.failed is None:
            self._start()
        if kind == "restart":
            return
        path = request["path"]
        server = self.server
        if server is None:
            self._say(path, "failed", self.failed)
            answer_empty(request)
            return
        started = time.monotonic()
        try:
            if kind == "resolve":
                self._resolve(server, request)
            else:
                uri = server.sync(path, request["language"], request["text"])
                self._probe(server, path, uri)
                counted = self._answer(server, request, uri)
        except Exception as exc:
            log("%s failed: %s" % (kind, exc))
            if not server.alive:
                self._exited(server)
            answer_empty(request)
            return
        elapsed = int((time.monotonic() - started) * 1000)
        if kind == "completion":
            log("completion %s %s:%s: %d items in %dms" % (name_of(path), request["line"], request["column"], counted, elapsed))
        else:
            log("%s %s in %dms" % (kind, name_of(path), elapsed))

    def _probe(self, server, path, uri):
        """Loading until the server answers for this document, then ready."""
        if uri not in server.probed:
            self._say(path, "loading", "loading " + name_of(path))
        try:
            server.probe(uri)
        except Exception as exc:
            if server.alive:
                self._say(path, "failed", "%s did not load: %s" % (name_of(path), exc))
            raise
        self._say(path, "ready", "ready")

    def _reload(self):
        """The server changed a project, so what it knew about each file is asked again."""
        server = self.server
        if server is None:
            return
        server.probed.clear()
        with self.guard:
            paths = list(self.paths)
        try:
            for path in paths:
                uri = uri_for(path)
                if uri in server.versions:
                    self._probe(server, path, uri)
        except Exception as exc:
            log("reload failed: %s" % exc)
            if not server.alive:
                self._exited(server)

    def _answer(self, server, request, uri):
        """Replies to an open, a signature or a completion. Returns the row count."""
        kind = request["kind"]
        path = request["path"]
        line = request["line"]
        column = request["column"]
        base = {"id": request["id"], "revision": request["revision"]}
        if kind == "open":
            return 0
        if kind == "signature":
            signatures, active = self._ask(server, path, lambda: server.signature(uri, line, column), ([], 0), False)
            reply(dict(base, signatures=signatures, activeSignature=active))
            return len(signatures)
        trigger = request.get("trigger") or ""
        # A space is a trigger that the server answers with nothing most of
        # the time, so an empty list after one is not asked again.
        rows, incomplete = self._ask(
            server, path, lambda: server.complete(uri, line, column, trigger), ([], False), not trigger.isspace()
        )
        word = word_range(request["text"], line, column)
        rows = sorted(rows, key=lambda row: (row.get("sortText") or row.get("label") or "").lower())
        if len(rows) > MAX_ITEMS:
            typed = request["text"].split("\n")[line - 1][word[1] - 1:word[2] - 1].lower()
            rows = [row for row in rows if (row.get("filterText") or row.get("label") or "").lower().startswith(typed)]
            rows = rows[:MAX_ITEMS]
            incomplete = True
        self.rows = {"%d:%d" % (request["id"], index): row for index, row in enumerate(rows)}
        reply(dict(base, items=[item_for(row, token, word) for token, row in self.rows.items()], incomplete=incomplete))
        return len(rows)

    def _ask(self, server, path, ask, empty, retry_empty):
        """Asks again while a server that just loaded fails or has nothing yet.

        Only within the first minute after it loaded, a few times, a second
        apart. A newer request in the queue ends the wait.
        """
        attempts = 0
        while True:
            again = False
            try:
                result = ask()
                again = retry_empty and result == empty
            except Exception as exc:
                if not server.alive:
                    raise
                # The server can answer before it has applied the edit. The
                # exception stays in the log; the editor gets an empty reply.
                log("request failed: %s" % exc)
                result = empty
                again = True
            fresh = time.monotonic() - server.ready_at < self.RETRY_WINDOW
            if not again or not fresh or attempts >= self.RETRIES or not self.queue.empty():
                break
            attempts += 1
            self._say(path, "loading", "waiting for " + self.route.label)
            time.sleep(1)
            if not self.queue.empty():
                break
        if attempts:
            self._say(path, "ready", "ready")
        return result

    def _resolve(self, server, request):
        """Detail and documentation for one row of the last list."""
        token = request["data"]
        row = self.rows.get(token)
        if row is None:
            return
        resolved = server.request("completionItem/resolve", row) or row
        reply(
            {
                "id": request["id"],
                "revision": request["revision"],
                "kind": "resolve",
                "data": token,
                "detail": detail_of(resolved),
                "documentation": documentation(resolved.get("documentation")),
            }
        )


def main():
    log("completion session ready")
    providers = [
        (set(CPP), single_server("cpp", ["clangd", "--background-index"], CPP, "clangd")),
        ({"cs"}, CSharp(os.getcwd()).route),
    ]
    workers = {}
    for line in sys.stdin:
        request = json.loads(line)
        path = request["path"]
        extension = os.path.splitext(path)[1].lstrip(".").lower()
        route = next((make(path, extension) for extensions, make in providers if extension in extensions), None)
        opening = request["kind"] == "open"
        if route is None:
            STATES.set(path, "unsupported", "", "not available for .%s files" % extension, announce=opening)
            answer_empty(request)
            continue
        if route.argv is None:
            STATES.set(path, "missing", route.label, route.missing, announce=opening)
            SUMMARY.note(route.label, route.missing)
            answer_empty(request)
            continue
        request["language"] = route.language
        if route.key not in workers:
            workers[route.key] = Worker(route)
        workers[route.key].put(request)


if __name__ == "__main__":
    main()
