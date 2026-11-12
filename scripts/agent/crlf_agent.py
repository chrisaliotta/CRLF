#!/usr/bin/env python3
"""A reference Agent Client Protocol agent for CRLF.

The editor starts this process and speaks JSON-RPC with it, one object per
line: the editor is the client, this is the agent. Everything an agent
decides lives here: which model, the prompt, and the tool loop. The editor
only presents the conversation and answers requests for files, commands,
and its own state. See docs/agent.md.

    python crlf_agent.py --provider anthropic --model <model>
    python crlf_agent.py --provider ollama --model qwen3-coder

The API key is read from the provider's environment variable
(OPENAI_API_KEY, ANTHROPIC_API_KEY, XAI_API_KEY), or the one --api-key-env
names. Standard output is the protocol; the log is standard error.
"""

import argparse
import json
import os
import platform
import sys
import threading
import urllib.parse
import uuid

import providers
from tools import Tools, ToolError, clip

PROTOCOL_VERSION = 1
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


def system_prompt(cwd, crlf_client):
    shell = "cmd.exe" if os.name == "nt" else "bash"
    editor = ("The editor is CRLF. editor_state, problems, git_status, and read_terminal show what the user sees."
              if crlf_client else "")
    return (
        "You are a coding agent working in the user's editor on the project at %s, on %s. "
        "Read files before you change them. Make focused changes with edit_file; use write_file for new files. "
        "The user reviews every write and may reject it; when that happens, ask what they want instead. "
        "run_command runs one command line in %s and may need the user's approval. "
        "%s Answer in Markdown, briefly, and say what you changed."
        % (cwd, platform.system(), shell, editor)
    )


class Session:
    def __init__(self, agent, cwd):
        self.agent = agent
        self.id = "session-" + uuid.uuid4().hex[:12]
        self.cwd = cwd
        self.history = []
        self.cancel = threading.Event()
        self.tools = Tools(self, agent.crlf_client)
        self.tool_call = None

    # The context tools.py calls.

    def request(self, method, params, timeout=None):
        try:
            return self.agent.connection.request(method, dict(params, sessionId=self.id), timeout)
        except RpcError as error:
            raise ToolError(str(error))

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
        system = system_prompt(self.cwd, self.agent.crlf_client)
        for _ in range(MAX_STEPS):
            try:
                reply = self.agent.model.stream(
                    system, self.history, self.tools.schemas(),
                    lambda text: self.say("agent_message_chunk", text),
                    lambda text: self.say("agent_thought_chunk", text),
                    self.cancel)
            except providers.Cancelled:
                return "cancelled"
            self.history.append({"role": "assistant", "content": reply.text, "tool_calls": reply.tool_calls})
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
            except (ToolError, KeyError, ValueError, OSError) as error:
                text, content, locations, failed = "Error: %s" % error, [], [], True
        shown = [{"type": "content", "content": {"type": "text", "text": clip(text, 2000)}}]
        self.update({"sessionUpdate": "tool_call_update", "toolCallId": call["id"],
                     "status": "failed" if failed else "completed",
                     "content": content + shown, "locations": locations})
        result.update(content=clip(text), is_error=failed)
        return result


class Agent:
    def __init__(self, options):
        self.options = options
        self.connection = Connection()
        self.sessions = {}
        self.model = None
        self.crlf_client = False

    def initialize(self, params):
        self.crlf_client = (params.get("clientInfo") or {}).get("name") == "CRLF"
        return {
            "protocolVersion": PROTOCOL_VERSION,
            "agentCapabilities": {"loadSession": False,
                                  "promptCapabilities": {"image": False, "audio": False, "embeddedContext": True}},
            "authMethods": [],
            "agentInfo": {"name": "crlf-agent", "title": "%s (%s)" % (self.options.model, self.options.provider),
                          "version": "1.0.0"},
        }

    def new_session(self, params):
        if self.model is None:
            self.model = providers.create(self.options.provider, self.options.model, self.options.base_url,
                                          self.options.api_key_env, self.options.max_tokens)
        session = Session(self, params["cwd"])
        self.sessions[session.id] = session
        status("ready: %s" % self.model.label())
        return {"sessionId": session.id}

    def prompt(self, id, params):
        session = self.sessions[params["sessionId"]]
        status("working")
        try:
            self.connection.respond(id, {"stopReason": session.turn(params["prompt"])})
        except providers.ProviderError as error:
            log(str(error))
            self.connection.fail(id, -32603, str(error))
        except Exception as error:  # A bug here must still end the turn the editor is waiting on.
            log("turn failed: %r" % error)
            self.connection.fail(id, -32603, "The agent failed: %s" % error)
        status("ready")

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
            elif method == "session/prompt":
                threading.Thread(target=self.prompt, args=(id, params), daemon=True).start()
            else:
                self.connection.fail(id, -32601, "Method not found: %s" % method)
        except providers.ProviderError as error:
            self.connection.fail(id, -32603, str(error))

    def run(self):
        for raw in sys.stdin.buffer:
            line = raw.decode("utf-8", "replace").strip()
            if line:
                self.dispatch(json.loads(line))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--provider", required=True, choices=sorted(providers.PRESETS))
    parser.add_argument("--model", required=True)
    parser.add_argument("--base-url", help="Server address, for openai-compatible or another port")
    parser.add_argument("--api-key-env", help="Environment variable that holds the API key")
    parser.add_argument("--max-tokens", type=int, help="Longest reply the model may write")
    Agent(parser.parse_args()).run()


if __name__ == "__main__":
    main()
