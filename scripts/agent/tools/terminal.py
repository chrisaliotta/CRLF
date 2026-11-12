"""Tools that run commands in the editor's terminals.

A command in the foreground is waited for and its terminal released. One in
the background keeps its terminal: the session lists it in `terminals` until
stop_command or the end of the session releases it.
"""

from . import ToolError, boolean, full_path, integer, schema, string

# The end of a command's output is what the model is given.
OUTPUT_LIMIT = 8000
# The output, and a line above it that says how the command ended.
RESULT_LIMIT = OUTPUT_LIMIT + 200
# What the editor keeps of the output. It is counted in bytes, so it is asked for more than is used.
OUTPUT_BYTES = 4 * OUTPUT_LIMIT
DEFAULT_TIMEOUT = 120


def register(registry, context):
    registry.define(
        "run_terminal_cmd", "Run a shell command and return the end of its output and its exit code. The user may "
                            "be asked first. Set background for a command that does not end by itself, such as a "
                            "server or a watcher: it returns an id at once for read_command_output and "
                            "stop_command.",
        schema({"command": string("The command line"),
                "cwd": string("Working directory, relative to the project. The default is the one the last "
                              "command was given, and the project before that"),
                "timeout_seconds": integer("Stop a foreground command after this long. The default is %d"
                                           % DEFAULT_TIMEOUT),
                "background": boolean("Start the command and return without waiting for it")}, ["command"]),
        "execute", run_terminal_cmd, lambda a: "Run " + a.get("command", ""), RESULT_LIMIT)
    registry.define(
        "read_command_output", "The end of a background command's output so far, and whether it is still running.",
        schema({"id": string("The id run_terminal_cmd returned")}, ["id"]),
        "read", read_command_output, lambda a: "Read the output of command " + a.get("id", ""), RESULT_LIMIT)
    registry.define(
        "stop_command", "Stop a background command and return the end of its output.",
        schema({"id": string("The id run_terminal_cmd returned")}, ["id"]),
        "execute", stop_command, lambda a: "Stop command " + a.get("id", ""), RESULT_LIMIT)


def _report(status, output, stopped_after=None):
    """`status` is the ACP exit status, or None while the command runs.
    `stopped_after` is the timeout, when the command was stopped for reaching it."""
    if stopped_after is not None:
        head = "Stopped after %d seconds." % stopped_after
    elif status is None:
        head = "Still running."
    elif status.get("exitCode") is None:
        head = "Ended by %s." % (status.get("signal") or "a signal")
    else:
        head = "Exit code %d." % status["exitCode"]
    text = output.get("output", "")
    cut = " The start of the output was cut." if output.get("truncated") or len(text) > OUTPUT_LIMIT else ""
    return "%s%s\n%s" % (head, cut, text[-OUTPUT_LIMIT:])


def _background(context, arguments):
    """The id comes from the model, so it is checked against the commands this session started."""
    if arguments["id"] not in context.terminals:
        raise ToolError("No background command has the id %s." % arguments["id"])
    return arguments["id"]


def run_terminal_cmd(context, arguments):
    if arguments.get("cwd"):
        context.command_cwd = full_path(context, arguments["cwd"])
    terminal = context.request("terminal/create", {"command": arguments["command"], "args": [],
                                                   "cwd": context.command_cwd,
                                                   "outputByteLimit": OUTPUT_BYTES})["terminalId"]
    context.show_terminal(terminal)
    shown = [{"type": "terminal", "terminalId": terminal}]
    if arguments.get("background"):
        context.terminals.append(terminal)
        return ("Started in the background with the id %s. Read what it prints with read_command_output, and end "
                "it with stop_command." % terminal), shown, []
    timeout = int(arguments.get("timeout_seconds") or DEFAULT_TIMEOUT)
    stopped_after = None
    try:
        try:
            status = context.request("terminal/wait_for_exit", {"terminalId": terminal}, timeout=timeout)
        except TimeoutError:
            context.request("terminal/kill", {"terminalId": terminal})
            status = context.request("terminal/wait_for_exit", {"terminalId": terminal})
            stopped_after = timeout
        output = context.request("terminal/output", {"terminalId": terminal})
    finally:
        context.request("terminal/release", {"terminalId": terminal})
    return _report(status, output, stopped_after), shown, []


def read_command_output(context, arguments):
    output = context.request("terminal/output", {"terminalId": _background(context, arguments)})
    return _report(output.get("exitStatus"), output), [], []


def stop_command(context, arguments):
    terminal = _background(context, arguments)
    context.request("terminal/kill", {"terminalId": terminal})
    status = context.request("terminal/wait_for_exit", {"terminalId": terminal})
    output = context.request("terminal/output", {"terminalId": terminal})
    context.request("terminal/release", {"terminalId": terminal})
    context.terminals.remove(terminal)
    return _report(status, output), [], []
