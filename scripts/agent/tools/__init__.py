"""The tools the reference agent offers its model.

Files are read and written through the editor (fs/read_text_file and
fs/write_text_file), so a read sees unsaved edits and every write goes
through the editor's review. Commands run through terminal/create, so the
editor decides whether they may run. Listing and searching read the disk
here. Some tools exist only when the client is CRLF.

Each area is a module with `register(registry, context)`. The context is the
session: `cwd`, `request`, `show_terminal`, `record_write`, `writes`,
`terminals`, `command_cwd`, `index`, `refresh_index`, and `rules`, the
folders of rule files. A tool is a function `run(context, arguments)` that returns (text for the model, ACP tool-call
content, locations). Its definition carries `limit`, the characters of the
text the model is given.
"""

import functools
import os
import shutil
import subprocess

SKIPPED_DIRECTORIES = {"node_modules", "__pycache__"}
# A larger file is not searched or indexed.
MAX_FILE_BYTES = 1024 * 1024
# Keeps a console window from opening for git and rg on Windows.
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


class ToolError(Exception):
    pass


def clip(text, limit):
    if len(text) <= limit:
        return text
    return text[:limit] + "\n[cut: %d more characters]" % (len(text) - limit)


def schema(properties, required=()):
    return {"type": "object", "properties": properties, "required": list(required)}


def string(description):
    return {"type": "string", "description": description}


def integer(description):
    return {"type": "integer", "description": description}


def boolean(description):
    return {"type": "boolean", "description": description}


def full_path(context, path):
    return os.path.normpath(os.path.join(context.cwd, path or "."))


def shown_path(context, path):
    """The path as the model and the user read it: relative to the project
    when it is inside it, with forward slashes."""
    full = full_path(context, path)
    relative = os.path.relpath(full, context.cwd)
    return (full if relative.startswith("..") else relative).replace(os.sep, "/")


def git(root, *arguments):
    """What git prints for a command run in `root`, or None when `root` is
    not in a repository or git is not installed."""
    if shutil.which("git") is None:
        return None
    done = subprocess.run(["git", "-C", root] + list(arguments), stdin=subprocess.DEVNULL, capture_output=True,
                          creationflags=NO_WINDOW)
    return done.stdout.decode("utf-8", "replace") if done.returncode == 0 else None


def walk(root):
    """The files under `root`, as sorted paths relative to it with forward
    slashes. In a git repository these are the files git tracks or would
    add, so .gitignore is respected. Elsewhere, directories that start with
    a dot, node_modules, and __pycache__ are skipped."""
    listed = git(root, "ls-files", "-z", "--cached", "--others", "--exclude-standard")
    if listed is not None:
        # A file deleted from the folder stays in git's list until the deletion is staged.
        gone = set(git(root, "ls-files", "-z", "--deleted").split("\0"))
        return sorted(set(path for path in listed.split("\0") if path and path not in gone))
    paths = []
    for directory, names, files in os.walk(root):
        names[:] = [name for name in names if not name.startswith(".") and name not in SKIPPED_DIRECTORIES]
        prefix = os.path.relpath(directory, root).replace(os.sep, "/") + "/"
        paths += [("" if prefix == "./" else prefix) + name for name in files]
    return sorted(paths)


def read_text(path):
    """The file's text from the disk, or None for a file over 1 MB, a binary
    file, or a path that is not a file, such as a git submodule."""
    if not os.path.isfile(path) or os.path.getsize(path) > MAX_FILE_BYTES:
        return None
    with open(path, "rb") as handle:
        data = handle.read()
    if b"\0" in data[:1024]:
        return None
    return data.decode("utf-8", "replace")


# The modules use the names above, so they are imported after them.
from . import context as context_tools, files, search, terminal  # noqa: E402


class Tools:
    """The definitions, in an order that never changes within a session, so
    the start of every model request is the same."""

    def __init__(self, context, crlf_client):
        self.context = context
        # The _crlf/ methods are offered only to the editor that answers them.
        self.crlf_client = crlf_client
        self.definitions = []
        for module in (files, search, terminal, context_tools):
            module.register(self, context)
        self.by_name = {d["name"]: d for d in self.definitions}

    def define(self, name, description, parameters, kind, run, title, limit):
        """`kind` is the ACP tool kind, `title` makes the tool call's title
        from its arguments, and `limit` is how many characters of the result
        the model is given."""
        self.definitions.append({"name": name, "description": description, "parameters": parameters, "kind": kind,
                                 "run": functools.partial(run, self.context), "title": title, "limit": limit})

    def schemas(self):
        return [{k: d[k] for k in ("name", "description", "parameters")} for d in self.definitions]
