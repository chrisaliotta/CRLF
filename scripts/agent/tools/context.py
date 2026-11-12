"""Tools that tell the model about the project and the session: its rules,
what has changed, and what the editor shows."""

import json
import os

from . import ToolError, git, schema, string

RULES_LIMIT = 8000
HISTORY_LIMIT = 4000
EDITOR_LIMIT = 4000
PROBLEMS_LIMIT = 6000
# The end of the user's terminal is the part that matters.
TERMINAL_LIMIT = 8000
GIT_LOG_ENTRIES = 10


def register(registry, context):
    if any(os.path.isdir(directory) for directory in context.rules):
        registry.define(
            "fetch_rules", "The project's rules: how its owner wants the work done. Without a name, lists the "
                           "rules with the first line of each. With one, returns that rule.",
            schema({"name": string("A rule's name, as listed")}),
            "read", fetch_rules, lambda a: "Read the rule " + a["name"] if a.get("name") else "List the rules",
            RULES_LIMIT)
    registry.define(
        "diff_history", "What has changed: the files you wrote in this session with the lines added and removed, "
                        "then git's changed files and last %d commits when the project is a repository."
                        % GIT_LOG_ENTRIES,
        schema({}), "read", diff_history, lambda a: "Read the change history", HISTORY_LIMIT)
    if registry.crlf_client:
        registry.define(
            "editor_state", "The files open in the editor, which one is in front, the caret, and the selection.",
            schema({}), "read", _extension("_crlf/editor/state"), lambda a: "Read the editor state", EDITOR_LIMIT)
        registry.define(
            "problems", "Errors and warnings the project's syntax check reported for open files.",
            schema({}), "read", _extension("_crlf/problems"), lambda a: "Read the problems", PROBLEMS_LIMIT)
        registry.define(
            "git_status", "Branch, upstream, and changed files of each git repository in the project.",
            schema({}), "read", _extension("_crlf/git/status"), lambda a: "Read git status", EDITOR_LIMIT)
        registry.define(
            "read_terminal", "The end of the text in the user's terminal: scrollback and screen.",
            schema({}), "read", read_terminal, lambda a: "Read the terminal", TERMINAL_LIMIT)


def rule_files(directories):
    """{name: path} of the rules, in name order. The rules are the .md files
    of the folders; a rule's name is its file name without the extension.
    A name in two folders is the earlier folder's rule. A folder that does
    not exist has no rules."""
    found = {}
    for directory in directories:
        if os.path.isdir(directory):
            for name in os.listdir(directory):
                if name.endswith(".md"):
                    found.setdefault(name[:-3], os.path.join(directory, name))
    return dict(sorted(found.items()))


def _rule(path):
    with open(path, encoding="utf-8") as handle:
        return handle.read()


def fetch_rules(context, arguments):
    rules = rule_files(context.rules)
    name = arguments.get("name")
    if not name:
        listed = ["%s: %s" % (rule, _rule(path).strip().split("\n")[0].lstrip("# ")) for rule, path in rules.items()]
        return "\n".join(listed) or "There are no rules.", [], []
    # The name comes from the model, so only a file that was listed is read.
    if name not in rules:
        raise ToolError("There is no rule named %s. The rules are: %s" % (name, ", ".join(rules)))
    return _rule(rules[name]), [], [{"path": rules[name]}]


def diff_history(context, arguments):
    """Git runs here, in the agent's own process, as searching reads the disk here."""
    parts = ["Written in this session:\n" + "\n".join("%s +%d -%d" % write for write in context.writes)
             if context.writes else "Nothing was written in this session."]
    changed = git(context.cwd, "status", "--short")
    if changed is not None:
        parts.append("git status --short:\n" + (changed.rstrip() or "(clean)"))
        log = git(context.cwd, "log", "--oneline", "-n", str(GIT_LOG_ENTRIES))
        parts.append("git log --oneline -n %d:\n%s" % (GIT_LOG_ENTRIES, (log or "").rstrip() or "(no commits)"))
    return "\n\n".join(parts), [], []


def _extension(method):
    """A tool that returns what one of the editor's own methods answers."""
    def run(context, arguments):
        return json.dumps(context.request(method, {}), indent=1, ensure_ascii=False), [], []
    return run


def read_terminal(context, arguments):
    return context.request("_crlf/terminal/read", {})["text"][-TERMINAL_LIMIT:], [], []
