#!/usr/bin/env python3
"""A reference Agent Client Protocol agent for CRLF.

The editor starts this process and speaks JSON-RPC with it, one object per
line: the editor is the client, this is the agent. Everything an agent
decides lives here: which model, the prompt, and the tool loop. The editor
only presents the conversation and answers requests for files, commands,
and its own state. See docs/agent.md.

    python crlf_agent.py --provider anthropic --model <model>
    python crlf_agent.py --provider ollama --model qwen3-coder

The API key is read from the file --api-key-file names, or else from the
provider's environment variable (OPENAI_API_KEY, ANTHROPIC_API_KEY,
XAI_API_KEY), or the one --api-key-env names. Standard output is the
protocol; the log is standard error.

--model, --max-tokens, --reasoning-effort, and --thinking are where a
session starts. The session offers the provider's models and the settings
the chosen model takes as ACP config options, and the client changes them
with session/set_config_option.

--rules names a folder of rule files the model can list and read, by
default .crlf/rules in the session's working directory. It may be given
more than once, as for a project's rules and then the application's. A
rule name found in two folders is the earlier folder's rule.

--index picks what codebase_search asks: grep, fts (the default), or
vector, which needs --embed-provider. The index is kept in --index-dir, by
default .crlf/data in the session's working directory. See index.py and
embeddings.py.

--context-budget is where the session's Context option starts: unlimited,
or the tokens the conversation may reach before old tool results are
shortened. See history.py.

--prices names a file of prices and context windows by model. With it the
session reports what it has used after each reply: the context in use
against its size, and an estimated cost. --context-window gives the size
for a model the file does not list. See usage.py.
"""

import argparse
import difflib
import json
import os
import platform
import sys
import threading
import urllib.parse
import uuid

import embeddings
import history
import index
import providers
import usage
from tools import Tools, ToolError, clip, shown_path, walk
from tools.context import rule_files

PROTOCOL_VERSION = 1
DEFAULT_RULES = ".crlf/rules"
DEFAULT_INDEX_DIRECTORY = ".crlf/data"
# Model replies in one turn before the turn ends with max_turn_requests.
MAX_STEPS = 60


def log(text):
    sys.stderr.write(text.rstrip("\n") + "\n")
    sys.stderr.flush()


def status(text):
    log("CRLF_STATUS: " + text)


class RpcError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


class Connection:
    """JSON-RPC over standard input and output. Requests to the editor wait
    on the reader thread, which keeps reading while a turn runs."""

    def __init__(self):
        self.lock = threading.Lock()
        self.next_id = 0
        self.pending = {}

    def send(self, message):
        message["jsonrpc"] = "2.0"
        data = (json.dumps(message, ensure_ascii=False) + "\n").encode("utf-8")
        with self.lock:
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()

    def notify(self, method, params):
        self.send({"method": method, "params": params})

    def respond(self, id, result):
        self.send({"id": id, "result": result})

    def fail(self, id, code, message):
        self.send({"id": id, "error": {"code": code, "message": message}})

    def request(self, method, params, timeout=None):
        """Raises RpcError for an error reply and TimeoutError after `timeout` seconds."""
        done = threading.Event()
        slot = {}
        with self.lock:
            self.next_id += 1
            id = self.next_id
            self.pending[id] = (done, slot)
        self.send({"id": id, "method": method, "params": params})
        if not done.wait(timeout):
            with self.lock:
                self.pending.pop(id, None)
            raise TimeoutError()
        if "error" in slot:
            raise RpcError(slot["error"].get("code", 0), slot["error"].get("message", "error"))
        return slot.get("result")

    def deliver(self, message):
        with self.lock:
            waiting = self.pending.pop(message.get("id"), None)
        if waiting is None:
            return
        done, slot = waiting
        slot.update(message)
        done.set()


def path_from_uri(uri):
    parsed = urllib.parse.urlparse(uri)
    path = urllib.parse.unquote(parsed.path)
    if parsed.netloc:
        return "//" + parsed.netloc + path
    if len(path) > 2 and path[0] == "/" and path[2] == ":":
        return path[1:]
    return path


def session_path(cwd, path):
    """A folder a command-line option names: relative to the session's working directory, or absolute."""
    return os.path.normpath(os.path.join(cwd, path))


def prompt_text(blocks):
    """The model sees one user message: the text, then what was attached."""
    parts = []
    for block in blocks:
        kind = block.get("type")
        if kind == "text":
            parts.append(block["text"])
        elif kind == "resource_link":
            parts.append("The user attached %s. Read it if it matters." % path_from_uri(block["uri"]))
        elif kind == "resource":
            resource = block["resource"]
            parts.append("Selected in %s:\n```\n%s\n```" % (path_from_uri(resource["uri"]), resource.get("text", "")))
    return "\n\n".join(parts)


def system_prompt(cwd, crlf_client, rules):
    """`rules` is the names of the project's rules."""
    shell = "cmd.exe" if os.name == "nt" else "bash"
    editor = ("The editor is CRLF. editor_state, problems, git_status, and read_terminal show what the user sees. "
              if crlf_client else "")
    ruled = ("The project has rules: %s. Read the ones that bear on the task with fetch_rules before you start. "
             % ", ".join(rules) if rules else "")
    return (
        "You are a coding agent working in the user's editor on the project at %s, on %s. "
        "Find code with codebase_search when you know what it does, grep_search when you know its text, and "
        "file_search when you know its name. "
        "Read files before you change them; read_file with outline shows a long file's shape first. "
        "Make focused changes with edit_file; use write_file for new files. "
        "The user reviews every write and may reject it; when that happens, ask what they want instead. "
        "run_terminal_cmd runs one command line in %s and may need the user's approval. "
        "%s%sAnswer in Markdown, briefly, and say what you changed."
        % (cwd, platform.system(), shell, editor, ruled)
    )


class Session:
    def __init__(self, agent, cwd, adapter, models, settings, index, prices):
        self.agent = agent
        self.id = "session-" + uuid.uuid4().hex[:12]
        self.cwd = cwd
        self.adapter = adapter
        # [(id, display name)], as the provider listed them.
        self.models = models
        # {"model": id, option id: value}. The command line's values start
        # it; a setting it does not name takes the adapter's default. The
        # Context option is the session's own, and the adapter does not read it.
        self.settings = settings
        self.history = []
        # What the session has used, for the client's usage line. See usage.py.
        self.prices = prices
        self.totals = usage.Totals()
        # The tokens the next request will carry: the last reply's input and
        # output. None until a provider has reported a reply's tokens.
        self.context_used = None
        # What the provider counted for the last request beyond the estimate
        # of the conversation, which the context budget allows for.
        self.context_offset = 0
        self.cancel = threading.Event()
        self.tool_call = None
        # The folders of rule files, for fetch_rules. An earlier folder's rule wins a shared name.
        self.rules = [session_path(cwd, folder) for folder in agent.options.rules]
        # The code index codebase_search asks.
        self.index = index
        # [(path, lines added, lines removed)] for each write the user accepted, for diff_history.
        self.writes = []
        # Ids of the commands left running in the background, released when the session ends.
        self.terminals = []
        # Where the next command runs when it names no directory: where the last one was told to.
        self.command_cwd = cwd
        self.tools = Tools(self, agent.crlf_client)
        # Built once, so every request of the session starts with the same text.
        self.system = system_prompt(cwd, agent.crlf_client, list(rule_files(self.rules)))

    def config_options(self):
        """The ACP config options for the model in use, each with its
        current value. A value the list does not have, such as a model or a
        token count from the command line, is added to it."""
        model = {"id": "model", "name": "Model", "category": "model", "type": "select",
                 "description": "The model that answers",
                 "options": [{"value": id, "name": name} for id, name in self.models]}
        options = [model] + self.adapter.config_options(self.settings["model"]) + [history.config_option()]
        defaults = self.adapter.defaults(self.settings["model"])
        for option in options:
            current = self.settings.setdefault(option["id"], defaults.get(option["id"]))
            if all(choice["value"] != current for choice in option["options"]):
                option["options"].append({"value": current, "name": current})
            option["currentValue"] = current
        return options

    def set_config_option(self, id, value):
        """Returns the options after the change. The value comes from the
        client, so it is checked against what was offered."""
        offered = {option["id"]: option for option in self.config_options()}
        if id not in offered or all(choice["value"] != value for choice in offered[id]["options"]):
            raise RpcError(-32602, "%r is not a value of %s" % (value, id))
        self.settings[id] = value
        options = self.config_options()
        if id == "model":
            # Another model can take other settings, so the client is told the whole list again.
            self.agent.status("ready: %s" % value)
            self.update({"sessionUpdate": "config_option_update", "configOptions": options})
        if id in ("model", history.OPTION_ID):
            # Either one changes what the context is measured against.
            self.report_usage()
        return options

    def context_size(self):
        """What the context is measured against, in tokens: the Context
        budget when one is set, otherwise the model's window, from
        --context-window or the prices file. None when neither is known."""
        budget = self.settings[history.OPTION_ID]
        if budget != history.UNLIMITED:
            return int(budget)
        entry = self.prices.entry(self.settings["model"])
        return self.agent.options.context_window or (entry or {}).get("context")

    def account(self, reply, estimated):
        """Adds one reply to the session's totals and tells the client.
        `estimated` is history.estimate of the conversation the request
        carried. A server that reported no tokens changes nothing."""
        if not reply.usage:
            return
        self.totals.add(reply.usage, self.prices.entry(self.settings["model"]))
        self.context_used = reply.usage["input"] + reply.usage["output"]
        self.context_offset = reply.usage["input"] - estimated
        self.report_usage()

    def report_usage(self):
        """ACP's usage_update: the context in use against its size, and the
        session's cost when the model is priced. The update has no way to
        say the size is unknown, so without one nothing is sent. The
        session's token totals ride in _meta, which other clients ignore."""
        size = self.context_size()
        if self.context_used is None or not size:
            return
        update = {"sessionUpdate": "usage_update", "used": self.context_used, "size": size,
                  "_meta": {"crlf": {"inputTokens": self.totals.input, "cachedTokens": self.totals.cached,
                                     "outputTokens": self.totals.output}}}
        if usage.priced(self.prices.entry(self.settings["model"])):
            update["cost"] = {"amount": round(self.totals.cost, 6), "currency": self.prices.currency}
        self.update(update)

    # The context the tools call.

    def request(self, method, params, timeout=None):
        try:
            return self.agent.connection.request(method, dict(params, sessionId=self.id), timeout)
        except RpcError as error:
            raise ToolError(str(error))

    def record_write(self, path, old, new):
        """`old` is None for a file that did not exist."""
        before, after = (old or "").splitlines(), new.splitlines()
        added = removed = 0
        for kind, i1, i2, j1, j2 in difflib.SequenceMatcher(None, before, after, autojunk=False).get_opcodes():
            if kind != "equal":
                removed += i2 - i1
                added += j2 - j1
        self.writes.append((shown_path(self, path), added, removed))

    def refresh_index(self):
        """Brings the index up to date with the disk on a thread of its own.
        Nothing waits for it: a search made meanwhile sees the index as it was."""
        threading.Thread(target=self._refresh_index, daemon=True).start()

    def _refresh_index(self):
        reported = []

        def progress(done, total):
            reported.append(done)
            status("indexing %d/%d" % (done, total))
        try:
            self.index.refresh(progress)
        except Exception as error:  # The session goes on without the index, as with an embedding server that is down.
            log("The index could not be refreshed: %s" % error)
        self.index.close()
        if reported:
            self.agent.status(self.agent.activity)

    def close(self):
        """Releases the commands left running in the background. The client
        is going away, so nothing waits for its replies."""
        for terminal in self.terminals:
            self.agent.connection.send({"id": "release-" + terminal, "method": "terminal/release",
                                        "params": {"sessionId": self.id, "terminalId": terminal}})

    def show_terminal(self, terminal_id):
        self.update({"sessionUpdate": "tool_call_update", "toolCallId": self.tool_call,
                     "content": [{"type": "terminal", "terminalId": terminal_id}]})

    def update(self, update):
        self.agent.connection.notify("session/update", {"sessionId": self.id, "update": update})

    def say(self, kind, text):
        self.update({"sessionUpdate": kind, "content": {"type": "text", "text": text}})

    def turn(self, blocks):
        """Returns the ACP stop reason."""
        self.cancel.clear()
        self.history.append({"role": "user", "content": prompt_text(blocks)})
        for _ in range(MAX_STEPS):
            shortened = history.shorten(self.history, self.settings[history.OPTION_ID], self.context_offset)
            if shortened:
                log("The conversation passed the context budget: %d old tool results were shortened." % shortened)
            estimated = history.estimate(self.history)
            try:
                # The settings are read for each reply, so a change made during a turn applies to the next one.
                reply = self.adapter.stream(
                    self.system, self.history, self.tools.schemas(), dict(self.settings),
                    lambda text: self.say("agent_message_chunk", text),
                    lambda text: self.say("agent_thought_chunk", text),
                    self.cancel)
            except providers.Cancelled:
                return "cancelled"
            self.history.append({"role": "assistant", "content": reply.text, "tool_calls": reply.tool_calls,
                                 "provider_state": reply.provider_state})
            self.account(reply, estimated)
            if not reply.tool_calls:
                return "max_tokens" if reply.stop == "max_tokens" else "end_turn"
            for call in reply.tool_calls:
                self.history.append(self.run_tool(call))
            if self.cancel.is_set():
                return "cancelled"
        return "max_turn_requests"

    def run_tool(self, call):
        definition = self.tools.by_name.get(call["name"])
        self.tool_call = call["id"]
        result = {"role": "tool", "tool_call_id": call["id"], "name": call["name"]}
        if definition is None or "_invalid" in call["arguments"]:
            result.update(content="Error: %s" % ("no tool named " + call["name"] if definition is None
                                                 else "the arguments were not valid JSON"), is_error=True)
            return result
        self.update({"sessionUpdate": "tool_call", "toolCallId": call["id"],
                     "title": definition["title"](call["arguments"]), "kind": definition["kind"],
                     "status": "in_progress", "rawInput": call["arguments"]})
        if self.cancel.is_set():
            text, content, locations, failed = "Cancelled by the user.", [], [], True
        else:
            try:
                text, content, locations = definition["run"](call["arguments"])
                failed = False
            # ProviderError is codebase_search's, when the embedding server does not answer.
            except (ToolError, KeyError, ValueError, OSError, providers.ProviderError) as error:
                text, content, locations, failed = "Error: %s" % error, [], [], True
        shown = [{"type": "content", "content": {"type": "text", "text": clip(text, 2000)}}]
        self.update({"sessionUpdate": "tool_call_update", "toolCallId": call["id"],
                     "status": "failed" if failed else "completed",
                     "content": content + shown, "locations": locations})
        result.update(content=clip(text, definition["limit"]), is_error=failed)
        return result


class Agent:
    def __init__(self, options):
        self.options = options
        self.connection = Connection()
        self.sessions = {}
        self.crlf_client = False
        # The last status set, which the row goes back to when indexing ends.
        self.activity = ""

    def status(self, text):
        self.activity = text
        status(text)

    def initialize(self, params):
        self.crlf_client = (params.get("clientInfo") or {}).get("name") == "CRLF"
        return {
            "protocolVersion": PROTOCOL_VERSION,
            "agentCapabilities": {"loadSession": False,
                                  "promptCapabilities": {"image": False, "audio": False, "embeddedContext": True}},
            "authMethods": [],
            "agentInfo": {"name": "crlf-agent", "title": "CRLF agent (%s)" % self.options.provider,
                          "version": "1.1.0"},
        }

    def new_session(self, params):
        # Read the key for every session, so a rotated key file is used without restarting the agent.
        adapter = providers.create(self.options.provider, self.options.base_url,
                                   api_key_env=self.options.api_key_env, api_key_file=self.options.api_key_file)
        try:
            models = adapter.models()
        except providers.ProviderError as error:
            log("Could not list the models, so only %s is offered: %s" % (self.options.model, error))
            models = [(self.options.model, self.options.model)]
        settings = {"model": self.options.model, history.OPTION_ID: self.options.context_budget}
        for id, value in (("max_tokens", self.options.max_tokens), ("reasoning_effort", self.options.reasoning_effort),
                          ("thinking", self.options.thinking)):
            if value is not None:
                settings[id] = str(value)
        cwd = params["cwd"]
        # Only the vector index embeds, so only it needs the embedder's package or key.
        embedder = (embeddings.create(self.options.embed_provider, self.options.embed_model,
                                      self.options.embed_base_url, self.options.embed_api_key_env,
                                      self.options.embed_api_key_file)
                    if self.options.index == "vector" else None)
        # Read for every session, so an edited price list is used after New.
        session = Session(self, cwd, adapter, models, settings,
                          index.create(self.options.index, cwd, session_path(cwd, self.options.index_dir), embedder,
                                       lambda: walk(cwd)),
                          usage.load(self.options.prices))
        self.sessions[session.id] = session
        self.status("ready: %s" % self.options.model)
        session.refresh_index()
        return {"sessionId": session.id, "configOptions": session.config_options()}

    def prompt(self, id, params):
        session = self.sessions[params["sessionId"]]
        self.status("working")
        try:
            self.connection.respond(id, {"stopReason": session.turn(params["prompt"])})
        except providers.ProviderError as error:
            log(str(error))
            self.connection.fail(id, -32603, str(error))
        except Exception as error:  # A bug here must still end the turn the editor is waiting on.
            log("turn failed: %r" % error)
            self.connection.fail(id, -32603, "The agent failed: %s" % error)
        # A search in this turn opened this thread's connection to the index.
        session.index.close()
        self.status("ready")

    def dispatch(self, message):
        method = message.get("method")
        if method is None:
            self.connection.deliver(message)
            return
        params = message.get("params") or {}
        if "id" not in message:
            if method == "session/cancel":
                self.sessions[params["sessionId"]].cancel.set()
            return
        id = message["id"]
        try:
            if method == "initialize":
                self.connection.respond(id, self.initialize(params))
            elif method == "authenticate":
                self.connection.respond(id, {})
            elif method == "session/new":
                self.connection.respond(id, self.new_session(params))
            elif method == "session/set_config_option":
                session = self.sessions[params["sessionId"]]
                self.connection.respond(id, {"configOptions": session.set_config_option(params["configId"],
                                                                                        params["value"])})
            elif method == "session/prompt":
                threading.Thread(target=self.prompt, args=(id, params), daemon=True).start()
            else:
                self.connection.fail(id, -32601, "Method not found: %s" % method)
        except providers.ProviderError as error:
            self.connection.fail(id, -32603, str(error))
        except RpcError as error:
            self.connection.fail(id, error.code, str(error))

    def run(self):
        for raw in sys.stdin.buffer:
            line = raw.decode("utf-8", "replace").strip()
            if line:
                self.dispatch(json.loads(line))
        # The client closed standard input, which ends every session.
        for session in self.sessions.values():
            session.close()


def context_budget(text):
    """The value of --context-budget as the Context option holds it. A
    count that is not a number raises ValueError, which argparse reports."""
    return text if text == history.UNLIMITED else str(int(text))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--provider", required=True, choices=sorted(providers.PRESETS))
    parser.add_argument("--model", required=True, help="The model a session starts with. The client can pick another")
    parser.add_argument("--base-url", help="Server address, for openai-compatible or another port")
    parser.add_argument("--api-key-env", help="Environment variable that holds the API key")
    parser.add_argument("--api-key-file", help="File that holds the API key, relative to the working directory. "
                                               "It is used instead of the environment variable")
    parser.add_argument("--max-tokens", type=int, help="Longest reply the model may write, to start with")
    parser.add_argument("--reasoning-effort", help="Reasoning level to start with, for OpenAI and xAI: "
                                                   "minimal, low, medium, or high")
    parser.add_argument("--thinking", help="Thinking to start with, for Anthropic: off, 4k, 16k, 32k, or adaptive")
    # No default here: argparse would add the folders given to it instead of replacing it.
    parser.add_argument("--rules", action="append", metavar="FOLDER",
                        help="Folder of .md rule files the model can read, relative to the session's working "
                             "directory or absolute. By default %s. Give it more than once for several folders; "
                             "a rule name in two of them is the earlier folder's rule" % DEFAULT_RULES)
    parser.add_argument("--index", default="fts", choices=["grep", "fts", "vector"],
                        help="What codebase_search asks: grep reads the files for every search, fts keeps a "
                             "full-text index, and vector keeps embeddings and needs --embed-provider")
    parser.add_argument("--index-dir", default=DEFAULT_INDEX_DIRECTORY,
                        help="Folder the index is kept in, relative to the session's working directory or "
                             "absolute. By default %(default)s")
    parser.add_argument("--embed-provider", choices=embeddings.PROVIDERS,
                        help="Where the vector index gets its embeddings. local runs a model in this process "
                             "and needs the fastembed package")
    parser.add_argument("--embed-model", help="The embedding model. local has a default; the others need one")
    parser.add_argument("--embed-base-url", help="Embedding server address, for openai-compatible or another port")
    parser.add_argument("--embed-api-key-file", help="File that holds the embedding server's API key")
    parser.add_argument("--embed-api-key-env", help="Environment variable that holds the embedding server's API key")
    parser.add_argument("--context-budget", type=context_budget, default=history.UNLIMITED, metavar="TOKENS",
                        help="Where the Context option starts: unlimited, or the tokens the conversation may "
                             "reach before old tool results are shortened")
    parser.add_argument("--prices", metavar="FILE",
                        help="JSON file of prices and context windows by model, for the client's usage line. "
                             "Without it no cost is reported, and a percentage needs --context-window or a "
                             "context budget. See usage.py")
    parser.add_argument("--context-window", type=int, metavar="TOKENS",
                        help="The model's context window, when the prices file does not list it")
    options = parser.parse_args()
    options.rules = options.rules or [DEFAULT_RULES]
    if options.index == "vector" and not options.embed_provider:
        parser.error("--index vector needs --embed-provider")
    Agent(options).run()


if __name__ == "__main__":
    main()
