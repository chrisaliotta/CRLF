"""Tools that search what files contain."""

import fnmatch
import itertools
import json
import os
import re
import shutil
import subprocess

from index import GrepIndex

from . import NO_WINDOW, ToolError, boolean, clip, full_path, read_text, schema, shown_path, string, walk

GREP_MATCHES = 50
GREP_LINE_LIMIT = 200
# Characters of a whole grep result: every match at full length, with room for its path.
GREP_LIMIT = GREP_MATCHES * (GREP_LINE_LIMIT + 100)
CODE_CHUNKS = 8
CODE_LIMIT = 6000


def register(registry, context):
    registry.define(
        "codebase_search", "Find code by what it does, when you do not know the exact words. Returns the best %d "
                           "passages with their file and lines. Use grep_search for an exact name." % CODE_CHUNKS,
        schema({"query": string("What you are looking for, in plain words")}, ["query"]),
        "search", codebase_search, lambda a: "Search the code for " + a.get("query", ""), CODE_LIMIT)
    registry.define(
        "grep_search", "Search file contents with a regular expression. Reads the files on disk. Returns at most "
                       "%d matching lines, so narrow the pattern, path, or glob when it stops there." % GREP_MATCHES,
        schema({"pattern": string("Regular expression"),
                "path": string("Folder to search. The default is the project"),
                "glob": string("File name pattern such as *.cpp"),
                "ignore_case": boolean("Match without case")}, ["pattern"]),
        "search", grep_search, lambda a: "Search for " + a.get("pattern", ""), GREP_LIMIT)


def _ripgrep(pattern, root, glob, ignore_case):
    """Yields (path, line number, line) from `rg --json`."""
    command = ["rg", "--json", "--regexp", pattern]
    if ignore_case:
        command.append("--ignore-case")
    if glob:
        command += ["--glob", glob]
    # Standard input is the protocol, and rg would search it if it were handed down.
    with subprocess.Popen(command + [root], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          creationflags=NO_WINDOW) as process:
        found = False
        try:
            for raw in process.stdout:
                event = json.loads(raw)
                if event["type"] == "match":
                    found = True
                    data = event["data"]
                    yield data["path"]["text"], data["line_number"], data["lines"].get("text", "")
            if process.wait() == 2 and not found:
                raise ToolError(process.stderr.read().decode("utf-8", "replace").strip())
        finally:
            # The caller stops reading at the match limit.
            process.kill()


def _python_grep(pattern, root, glob, ignore_case):
    """Yields (path, line number, line) from the files of the shared walk."""
    try:
        expression = re.compile(pattern, re.IGNORECASE if ignore_case else 0)
    except re.error as error:
        raise ToolError("Not a regular expression: %s" % error)
    for relative in walk(root):
        if glob and not fnmatch.fnmatch(os.path.basename(relative), glob):
            continue
        path = os.path.join(root, relative)
        text = read_text(path)
        if text is None:
            continue
        for number, line in enumerate(text.splitlines(), 1):
            if expression.search(line):
                yield path, number, line


def grep_search(context, arguments):
    root = full_path(context, arguments.get("path"))
    grep = _ripgrep if shutil.which("rg") else _python_grep
    matches = grep(arguments["pattern"], root, arguments.get("glob"), arguments.get("ignore_case"))
    lines = ["%s:%d: %s" % (shown_path(context, path), number, line.strip()[:GREP_LINE_LIMIT])
             for path, number, line in itertools.islice(matches, GREP_MATCHES + 1)]
    if len(lines) > GREP_MATCHES:
        lines[GREP_MATCHES:] = ["[stopped at %d matches]" % GREP_MATCHES]
    return "\n".join(lines) or "No matches.", [], []


def codebase_search(context, arguments):
    """Asks the session's index, after starting a refresh that it does not
    wait for. Until the index has been built once, the grep index answers:
    it keeps nothing, so it is always ready."""
    context.refresh_index()
    index = context.index
    note = ""
    if not index.ready:
        index = GrepIndex(context.cwd, lambda: walk(context.cwd))
        note = "[The index is still being built, so these are word matches. Search again later for better ones.]\n\n"
    chunks = index.search(arguments["query"], CODE_CHUNKS)
    if not chunks:
        return note + "Nothing found.", [], []
    # Every passage gets the same share of the limit, less room for its heading, so the last one is not the one
    # that is cut.
    share = CODE_LIMIT // len(chunks) - 120
    passages = ["%s:%d-%d\n%s" % (path.replace(os.sep, "/"), first, last, clip(text, share))
                for path, first, last, text, _ in chunks]
    locations = [{"path": full_path(context, path), "line": first} for path, first, _, _, _ in chunks]
    return note + "\n\n".join(passages), [], locations
