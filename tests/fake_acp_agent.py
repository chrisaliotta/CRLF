"""A scripted ACP agent for AgentSessionTests.

One prompt reads the attached file through the editor, runs `echo hello`
through terminal/create, and reports both in a message chunk. The session
offers one setting, `model`, with the values small and large.
"""

import json
import sys
import urllib.parse

next_id = 0
model = "small"


def config_options():
    return [{"id": "model", "name": "Model", "category": "model", "type": "select", "currentValue": model,
             "options": [{"value": "small", "name": "Small"}, {"value": "large", "name": "Large"}]}]


def send(message):
    message["jsonrpc"] = "2.0"
    sys.stdout.buffer.write((json.dumps(message) + "\n").encode("utf-8"))
    sys.stdout.buffer.flush()


def read():
    return json.loads(sys.stdin.buffer.readline().decode("utf-8"))


def call(method, params):
    global next_id
    next_id += 1
    send({"id": next_id, "method": method, "params": params})
    while True:
        message = read()
        if message.get("id") == next_id and "method" not in message:
            return message.get("result")


def path_of(uri):
    parsed = urllib.parse.urlparse(uri)
    path = urllib.parse.unquote(parsed.path)
    return path[1:] if len(path) > 2 and path[2] == ":" else path


while True:
    message = read()
    method = message.get("method")
    if method == "initialize":
        send({"id": message["id"], "result": {"protocolVersion": 1, "agentInfo": {"name": "fake", "title": "Fake"},
                                              "agentCapabilities": {"promptCapabilities": {"embeddedContext": True}}}})
    elif method == "session/new":
        send({"id": message["id"], "result": {"sessionId": "s1", "configOptions": config_options()}})
    elif method == "session/set_config_option":
        params = message["params"]
        if params["configId"] != "model" or params["value"] not in ("small", "large"):
            send({"id": message["id"], "error": {"code": -32602, "message": "not offered"}})
        else:
            model = params["value"]
            send({"id": message["id"], "result": {"configOptions": config_options()}})
    elif method == "session/prompt":
        blocks = message["params"]["prompt"]
        link = next(b for b in blocks if b["type"] == "resource_link")
        content = call("fs/read_text_file", {"sessionId": "s1", "path": path_of(link["uri"])})["content"]
        terminal = call("terminal/create", {"sessionId": "s1", "command": "echo", "args": ["hello"]})["terminalId"]
        status = call("terminal/wait_for_exit", {"sessionId": "s1", "terminalId": terminal})
        output = call("terminal/output", {"sessionId": "s1", "terminalId": terminal})["output"].strip()
        call("terminal/release", {"sessionId": "s1", "terminalId": terminal})
        send({"method": "session/update", "params": {"sessionId": "s1", "update": {
            "sessionUpdate": "agent_message_chunk",
            "content": {"type": "text", "text": "content=%s|output=%s|exit=%s" % (content, output, status["exitCode"])}}}})
        send({"id": message["id"], "result": {"stopReason": "end_turn"}})
