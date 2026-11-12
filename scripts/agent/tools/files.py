"""Tools that read, change, and find files."""

import os
import re

from . import ToolError, boolean, full_path, integer, schema, shown_path, string, walk

# Lines read_file returns when the model names no limit.
READ_LINES = 400
# Characters of one read, whatever the line count.
READ_LIMIT = 30000
LIST_LIMIT = 4000
FILE_SEARCH_RESULTS = 20
FILE_SEARCH_LIMIT = 4000
# A write, an edit, and a delete answer with one line, or with an error.
CHANGE_LIMIT = 1000

# The lines an outline keeps, by file extension: the ones that open a class,
# a function, or another named block.
# A type, a name, and an opening parenthesis on a line that does not end a statement.
_FUNCTION = (r"(?!(?:if|for|while|switch|return|else|do|case|catch|new|delete|throw|using)\b)"
             r"[\w~][\w:<>,.*&~?\[\] \t]*[ \t*&:~]+[\w~]+\([^;]*$")
_C_LIKE = r"\s*(?:template\b|(?:class|struct|enum|union|namespace)\b|#\s*define\b|" + _FUNCTION + ")"
_SCRIPT = (r"\s*(?:export\s+)?(?:default\s+)?(?:async\s+)?(?:function\b|class\b|interface\b|enum\b|type\s+\w+\s*="
           r"|(?:const|let|var)\s+\w+\s*=\s*(?:async\s*)?(?:\(|function\b))")
_MANAGED = r"\s*(?:(?:class|interface|enum|struct|record|namespace|object|fun)\b|" + _FUNCTION + ")"
_OUTLINES = [
    ((".py", ".pyi"), r"\s*(?:async\s+def|def|class)\s"),
    ((".c", ".h", ".cc", ".cpp", ".cxx", ".hh", ".hpp", ".hxx", ".m", ".mm"), _C_LIKE),
    ((".js", ".jsx", ".mjs", ".ts", ".tsx"), _SCRIPT),
    ((".java", ".cs", ".kt", ".scala"), _MANAGED),
    ((".go",), r"(?:func|type)\b"),
    ((".rs",), r"\s*(?:pub(?:\([^)]*\))?\s+)?(?:async\s+|unsafe\s+)?(?:fn|struct|enum|trait|impl|mod|type)\b"),
    ((".rb",), r"\s*(?:def|class|module)\b"),
    ((".sh", ".bash"), r"\s*(?:function\s+\w+|\w+\s*\(\))"),
    ((".md",), r"#{1,6}\s"),
]
OUTLINE_PATTERNS = {extension: re.compile(pattern) for extensions, pattern in _OUTLINES for extension in extensions}


def register(registry, context):
    def shown(arguments, default=""):
        return shown_path(context, arguments.get("path", default))

    registry.define(
        "read_file", "Read a text file. Lines are numbered. The editor's unsaved text is what you see. "
                     "At most %d lines come back unless limit says otherwise. Set outline to see only the lines "
                     "that define classes and functions, then read the part you need." % READ_LINES,
        schema({"path": string("File path, absolute or relative to the project"),
                "line": integer("First line to read, starting at 1"),
                "limit": integer("How many lines to read. The default is %d" % READ_LINES),
                "outline": boolean("Return the definition lines of the whole file instead of its text")}, ["path"]),
        "read", read_file, lambda a: "Read " + shown(a), READ_LIMIT)
    registry.define(
        "write_file", "Create or replace a whole file. The user reviews the change before it is saved.",
        schema({"path": string("File path"), "content": string("The complete new text")}, ["path", "content"]),
        "edit", write_file, lambda a: "Write " + shown(a), CHANGE_LIMIT)
    edit = {"old_text": string("Text to find, exactly as it appears"),
            "new_text": string("Text to put in its place"),
            "replace_all": boolean("Replace every match")}
    registry.define(
        "edit_file", "Replace exact text in a file. old_text must match once, unless replace_all is true. "
                     "For several changes to one file, give edits: they are applied in order and reviewed as one "
                     "change, and none is made if one does not match. Prefer this to write_file for a small change.",
        schema(dict(edit, path=string("File path"),
                    edits={"type": "array", "description": "Several replacements, instead of old_text and new_text",
                           "items": schema(edit, ["old_text", "new_text"])}), ["path"]),
        "edit", edit_file, lambda a: "Edit " + shown(a), CHANGE_LIMIT)
    if registry.crlf_client:
        registry.define(
            "delete_file", "Delete a file. The user reviews the deletion first.",
            schema({"path": string("File path")}, ["path"]),
            "delete", delete_file, lambda a: "Delete " + shown(a), CHANGE_LIMIT)
    registry.define(
        "list_dir", "List files and folders as a tree. Files git ignores are left out; outside a repository, "
                    "dot folders and node_modules are.",
        schema({"path": string("Folder, relative to the project. The default is the project"),
                "depth": integer("How many levels to show. The default is 2")}),
        "read", list_dir, lambda a: "List " + shown(a, "."), LIST_LIMIT)
    registry.define(
        "file_search", "Find files by name. The query's letters must appear in the path in order; a match in the "
                       "file name and letters that sit together rank higher. Returns the best %d paths."
                       % FILE_SEARCH_RESULTS,
        schema({"query": string("Part of a file name or path, such as agentpanel or tools/files")}, ["query"]),
        "search", file_search, lambda a: "Find files named " + a.get("query", ""), FILE_SEARCH_LIMIT)


def _read(context, path):
    return context.request("fs/read_text_file", {"path": path})["content"]


def _numbered(lines):
    return "\n".join("%6d|%s" % line for line in lines)


def _outline(path, lines):
    """[(line number, line)] for the lines that open a definition. A file
    type the table does not have gets the first line of each block that
    starts at the left margin."""
    pattern = OUTLINE_PATTERNS.get(os.path.splitext(path)[1].lower())
    if pattern is not None:
        return [(number, line) for number, line in enumerate(lines, 1) if pattern.match(line)]
    kept = []
    for index, line in enumerate(lines):
        if not line.strip() or line[0].isspace():
            continue
        after_blank = index == 0 or not lines[index - 1].strip()
        opens_block = index + 1 < len(lines) and lines[index + 1][:1].isspace() and lines[index + 1].strip()
        if after_blank or opens_block:
            kept.append((index + 1, line))
    return kept


def read_file(context, arguments):
    path = full_path(context, arguments["path"])
    if arguments.get("outline"):
        kept = _outline(path, _read(context, path).split("\n"))
        return _numbered(kept) or "No definitions found.", [], [{"path": path}]
    first = int(arguments.get("line") or 1)
    limit = int(arguments.get("limit") or READ_LINES)
    # One line more than asked for shows whether the file goes on.
    lines = context.request("fs/read_text_file", {"path": path, "line": first, "limit": limit + 1})["content"].split("\n")
    note = ""
    if len(lines) > limit:
        lines = lines[:limit]
        note = "\n[The file goes on: read again with line %d, or with outline.]" % (first + limit)
    return _numbered(enumerate(lines, first)) + note, [], [{"path": path, "line": first}]


def _write(context, path, old, new):
    context.request("fs/write_text_file", {"path": path, "content": new})
    context.record_write(path, old, new)
    diff = {"type": "diff", "path": path, "oldText": old, "newText": new}
    return "Wrote %s" % shown_path(context, path), [diff], [{"path": path}]


def write_file(context, arguments):
    path = full_path(context, arguments["path"])
    try:
        old = _read(context, path)
    except ToolError:
        old = None
    return _write(context, path, old, arguments["content"])


def edit_file(context, arguments):
    """Every edit is made to the text in memory and the result is sent as
    one write, so the user reviews one diff."""
    if "edits" not in arguments and "old_text" not in arguments:
        raise ToolError("Give old_text and new_text, or edits.")
    path = full_path(context, arguments["path"])
    edits = arguments.get("edits") or [arguments]
    old = _read(context, path)
    text = old
    for number, edit in enumerate(edits, 1):
        which = "old_text of edit %d" % number if len(edits) > 1 else "old_text"
        count = text.count(edit["old_text"])
        if count == 0:
            raise ToolError("%s was not found in %s. Nothing was changed." % (which, shown_path(context, path)))
        if count > 1 and not edit.get("replace_all"):
            raise ToolError("%s appears %d times in %s. Add surrounding lines, or set replace_all. Nothing was changed."
                            % (which, count, shown_path(context, path)))
        text = text.replace(edit["old_text"], edit["new_text"])
    return _write(context, path, old, text)


def delete_file(context, arguments):
    path = full_path(context, arguments["path"])
    context.request("_crlf/fs/delete", {"path": path})
    return "Deleted %s" % shown_path(context, path), [], [{"path": path}]


def list_dir(context, arguments):
    root = full_path(context, arguments.get("path"))
    depth = int(arguments.get("depth") or 2)
    lines = []
    folders = set()
    for path in sorted(walk(root), key=lambda path: path.split("/")):
        parts = path.split("/")
        for level, name in enumerate(parts[:depth]):
            if level == len(parts) - 1:
                lines.append("  " * level + name)
            elif tuple(parts[:level + 1]) not in folders:
                folders.add(tuple(parts[:level + 1]))
                lines.append("  " * level + name + "/")
    return "\n".join(lines) or "(empty)", [], [{"path": root}]


def _positions(query, text, start):
    """Where each letter of the query sits in the text from `start` on, or
    None when they do not appear in order. The query as one piece is taken
    first."""
    whole = text.find(query, start)
    if whole >= 0:
        return list(range(whole, whole + len(query)))
    positions = []
    for letter in query:
        start = text.find(letter, start)
        if start < 0:
            return None
        positions.append(start)
        start += 1
    return positions


def _path_score(query, path):
    """None when the path does not match. Letters in the file name and
    letters next to each other count double; a shorter path wins a tie."""
    text = path.lower()
    name = text.rfind("/") + 1
    positions = _positions(query, text, name) or _positions(query, text, 0)
    if positions is None:
        return None
    together = sum(1 for before, after in zip(positions, positions[1:]) if after == before + 1)
    in_name = sum(1 for position in positions if position >= name)
    return 2 * together + 2 * in_name - len(text) / 1000.0


def file_search(context, arguments):
    query = "".join(arguments["query"].lower().replace("\\", "/").split())
    if not query:
        raise ToolError("The query is empty.")
    scored = [(score, path) for score, path in ((_path_score(query, path), path) for path in walk(context.cwd))
              if score is not None]
    scored.sort(key=lambda entry: (-entry[0], entry[1]))
    best = [path for _, path in scored[:FILE_SEARCH_RESULTS]]
    return "\n".join(best) or "No files match.", [], []
