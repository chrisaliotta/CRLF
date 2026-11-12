"""Confirms the reference agent against scripted providers.

No provider is reached: each test starts a local HTTP server that plays one
and records what it was sent. The tests exist because a provider's wire
format is where the agent breaks without anyone noticing until a key is in
hand: how each stream becomes text, thinking, and tool calls; what each
provider must be handed back on the next request (Anthropic's signed
thinking blocks, OpenAI's reasoning items); which models are offered; and
how a setting the client picks reaches the request. See docs/agent.md.

    python tests/reference_agent_tests.py
"""

import http.server
import json
import os
import subprocess
import sys
import tempfile
import threading
import unittest

AGENT_DIRECTORY = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scripts", "agent")
sys.path.insert(0, AGENT_DIRECTORY)

import providers  # noqa: E402

TOOLS = [{"name": "read_file", "description": "Read a file",
          "parameters": {"type": "object", "properties": {"path": {"type": "string"}}, "required": ["path"]}}]


class FakeProvider:
    """`answer(path, body)` returns the events to stream for one POST.
    `models` is the JSON a GET returns. Every POST body is kept in `posts`."""

    def __init__(self, answer, models=None):
        self.posts = []
        fake = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *arguments):
                pass

            def do_GET(self):
                data = json.dumps(models or {"data": []}).encode()
                self.send_response(200)
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                fake.posts.append(body)
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.end_headers()
                for event in answer(self.path, body):
                    self.wfile.write(("data: %s\n\n" % json.dumps(event)).encode())

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.url = "http://127.0.0.1:%d" % self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def close(self):
        self.server.shutdown()
        self.server.server_close()


def run(adapter, history, settings):
    """One reply, with what streamed as text and as thinking."""
    text, thoughts = [], []
    reply = adapter.stream("system", history, TOOLS, settings, text.append, thoughts.append, threading.Event())
    return reply, "".join(text), "".join(thoughts)


def assistant(reply):
    return {"role": "assistant", "content": reply.text, "tool_calls": reply.tool_calls,
            "provider_state": reply.provider_state}


def tool_result(reply, content):
    call = reply.tool_calls[0]
    return {"role": "tool", "tool_call_id": call["id"], "name": call["name"], "content": content, "is_error": False}


class ProviderTests(unittest.TestCase):
    def serve(self, answer, models=None):
        fake = FakeProvider(answer, models)
        self.addCleanup(fake.close)
        return fake

    def test_chat_completions_assembles_a_split_tool_call(self):
        """xAI and local servers send a tool call's arguments in pieces, and
        some send reasoning as reasoning_content. The chosen max_tokens goes
        in the request; a setting left at its default does not."""
        def answer(path, body):
            return [
                {"choices": [{"delta": {"reasoning_content": "hm"}}]},
                {"choices": [{"delta": {"content": "Looking. "}}]},
                {"choices": [{"delta": {"tool_calls": [{"index": 0, "id": "c1",
                                                        "function": {"name": "read_file", "arguments": "{\"pa"}}]}}]},
                {"choices": [{"delta": {"tool_calls": [{"index": 0, "function": {"arguments": "th\": \"a.txt\"}"}}]},
                              "finish_reason": "tool_calls"}]},
            ]
        fake = self.serve(answer)
        adapter = providers.create("openai-compatible", fake.url)
        reply, text, thoughts = run(adapter, [{"role": "user", "content": "hi"}],
                                    {"model": "local", "max_tokens": "4096"})
        self.assertEqual((text, thoughts, reply.stop), ("Looking. ", "hm", "tool_use"))
        self.assertEqual(reply.tool_calls, [{"id": "c1", "name": "read_file", "arguments": {"path": "a.txt"}}])
        self.assertEqual(fake.posts[0]["max_tokens"], 4096)
        self.assertEqual(fake.posts[0]["model"], "local")

        run(adapter, [{"role": "user", "content": "hi"}], {"model": "local", "max_tokens": "default"})
        self.assertNotIn("max_tokens", fake.posts[1])

    def test_anthropic_thinking_is_streamed_and_handed_back(self):
        """With thinking on, Anthropic rejects a tool result unless the reply
        that asked for the tool goes back with its thinking block first and
        the signature intact. The budget is added to max_tokens so the
        answer keeps its room."""
        def answer(path, body):
            if len(body["messages"]) > 1:
                return [{"type": "content_block_start", "index": 0, "content_block": {"type": "text", "text": ""}},
                        {"type": "content_block_delta", "index": 0, "delta": {"type": "text_delta", "text": "Done."}},
                        {"type": "message_delta", "delta": {"stop_reason": "end_turn"}}]
            return [
                {"type": "content_block_start", "index": 0, "content_block": {"type": "thinking", "thinking": ""}},
                {"type": "content_block_delta", "index": 0, "delta": {"type": "thinking_delta", "thinking": "Read "}},
                {"type": "content_block_delta", "index": 0, "delta": {"type": "thinking_delta", "thinking": "it."}},
                {"type": "content_block_delta", "index": 0, "delta": {"type": "signature_delta", "signature": "SIG"}},
                {"type": "content_block_start", "index": 1,
                 "content_block": {"type": "tool_use", "id": "tu1", "name": "read_file", "input": {}}},
                {"type": "content_block_delta", "index": 1,
                 "delta": {"type": "input_json_delta", "partial_json": "{\"path\": \"a.txt\"}"}},
                {"type": "message_delta", "delta": {"stop_reason": "tool_use"}},
            ]
        fake = self.serve(answer)
        os.environ["ANTHROPIC_API_KEY"] = "k"
        adapter = providers.create("anthropic", fake.url)
        settings = {"model": "claude", "thinking": "4k", "max_tokens": "8192"}
        history = [{"role": "user", "content": "hi"}]
        reply, _, thoughts = run(adapter, history, settings)
        self.assertEqual(thoughts, "Read it.")
        self.assertEqual(reply.tool_calls[0]["arguments"], {"path": "a.txt"})
        self.assertEqual(fake.posts[0]["thinking"], {"type": "enabled", "budget_tokens": 4000})
        self.assertEqual(fake.posts[0]["max_tokens"], 12192)

        history += [assistant(reply), tool_result(reply, "alpha")]
        final, text, _ = run(adapter, history, settings)
        self.assertEqual((text, final.stop), ("Done.", "end"))
        sent = fake.posts[1]["messages"]
        self.assertEqual([message["role"] for message in sent], ["user", "assistant", "user"])
        self.assertEqual(sent[1]["content"][0], {"type": "thinking", "thinking": "Read it.", "signature": "SIG"})
        self.assertEqual(sent[1]["content"][1]["type"], "tool_use")
        self.assertEqual(sent[2]["content"][0]["type"], "tool_result")

        # Thinking switched off: the request asks for none and sends none back.
        run(adapter, history, dict(settings, thinking="off"))
        self.assertNotIn("thinking", fake.posts[2])
        self.assertEqual(fake.posts[2]["messages"][1]["content"][0]["type"], "tool_use")

    def test_openai_responses_streams_the_summary_and_replays_reasoning(self):
        """GPT-5's thinking only arrives through the Responses API, as a
        reasoning summary. Nothing is stored at OpenAI, so the reasoning
        item goes back, encrypted, ahead of the call it led to."""
        reasoning = {"id": "rs_1", "type": "reasoning", "summary": [{"type": "summary_text", "text": "Plan"}],
                     "encrypted_content": "ENC"}
        call = {"id": "fc_1", "type": "function_call", "call_id": "call_1", "name": "read_file",
                "arguments": "{\"path\": \"a.txt\"}", "status": "completed"}

        def answer(path, body):
            if any(item.get("type") == "function_call_output" for item in body["input"]):
                message = {"type": "message", "role": "assistant",
                           "content": [{"type": "output_text", "text": "Done."}]}
                return [{"type": "response.output_text.delta", "delta": "Done."},
                        {"type": "response.completed", "response": {"output": [message]}}]
            return [{"type": "response.reasoning_summary_part.added", "summary_index": 0},
                    {"type": "response.reasoning_summary_text.delta", "delta": "Plan"},
                    {"type": "response.completed", "response": {"output": [reasoning, call]}}]
        fake = self.serve(answer)
        os.environ["OPENAI_API_KEY"] = "k"
        adapter = providers.create("openai", fake.url)
        settings = dict(adapter.defaults("gpt-5"), model="gpt-5", reasoning_effort="low", verbosity="high")
        history = [{"role": "user", "content": "hi"}]
        reply, _, thoughts = run(adapter, history, settings)
        self.assertEqual(thoughts, "Plan")
        self.assertEqual(reply.tool_calls, [{"id": "call_1", "name": "read_file", "arguments": {"path": "a.txt"}}])
        first = fake.posts[0]
        self.assertEqual(first["reasoning"], {"effort": "low", "summary": "auto"})
        self.assertEqual(first["text"], {"verbosity": "high"})
        self.assertEqual((first["store"], first["include"]), (False, ["reasoning.encrypted_content"]))
        self.assertNotIn("max_output_tokens", first)

        history += [assistant(reply), tool_result(reply, "alpha")]
        final, text, _ = run(adapter, history, settings)
        self.assertEqual((text, final.stop), ("Done.", "end"))
        self.assertEqual(fake.posts[1]["input"], [
            {"role": "user", "content": "hi"}, reasoning, call,
            {"type": "function_call_output", "call_id": "call_1", "output": "alpha"}])

        # Another model cannot read that reasoning, so it gets the call alone.
        run(adapter, history, dict(settings, model="gpt-4.1"))
        other = fake.posts[2]
        self.assertEqual([item.get("type") for item in other["input"]],
                         [None, "function_call", "function_call_output"])
        self.assertNotIn("reasoning", other)

    def test_openai_lists_only_chat_models(self):
        """The provider's list is what the model picker shows, and most of
        what OpenAI lists cannot hold a conversation."""
        listed = ["gpt-5", "gpt-5-2025-08-07", "gpt-4o-mini-tts", "text-embedding-3-small", "gpt-image-2",
                  "gpt-realtime", "o3", "gpt-4.1", "omni-moderation-latest", "gpt-5-search-api", "whisper-1"]
        fake = self.serve(lambda path, body: [], {"data": [{"id": id} for id in listed]})
        os.environ["OPENAI_API_KEY"] = "k"
        adapter = providers.create("openai", fake.url)
        self.assertEqual(adapter.models(), [("gpt-4.1", "gpt-4.1"), ("gpt-5", "gpt-5"), ("o3", "o3")])
        offered = [option["id"] for option in adapter.config_options("gpt-5")]
        self.assertEqual(offered, ["reasoning_effort", "reasoning_summary", "verbosity", "max_tokens"])
        self.assertEqual([option["id"] for option in adapter.config_options("gpt-4.1")], ["max_tokens"])


class AgentTests(unittest.TestCase):
    """The agent as the editor sees it: over ACP, with a scripted provider."""

    def setUp(self):
        def answer(path, body):
            return [{"choices": [{"delta": {"content": "I am " + body["model"]}, "finish_reason": "stop"}]}]
        self.fake = FakeProvider(answer, {"data": [{"id": "small"}, {"id": "large"}]})
        self.addCleanup(self.fake.close)
        self.agent = subprocess.Popen(
            [sys.executable, os.path.join(AGENT_DIRECTORY, "crlf_agent.py"), "--provider", "openai-compatible",
             "--base-url", self.fake.url, "--model", "small", "--max-tokens", "1234"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        self.addCleanup(self.stop)
        self.next_id = 0

    def stop(self):
        self.agent.stdin.close()
        self.agent.stdout.close()
        self.agent.wait()

    def call(self, method, params):
        """The reply to one request, and the notifications that came before it."""
        self.next_id += 1
        message = {"jsonrpc": "2.0", "id": self.next_id, "method": method, "params": params}
        self.agent.stdin.write((json.dumps(message) + "\n").encode())
        self.agent.stdin.flush()
        updates = []
        while True:
            reply = json.loads(self.agent.stdout.readline())
            if reply.get("id") == self.next_id:
                return reply, updates
            updates.append(reply["params"]["update"])

    def test_the_client_picks_the_model_and_settings(self):
        """session/new offers the provider's models and the settings, at the
        values the command line gave. A choice is answered with the whole
        list and used by the next reply; a value that was not offered is
        refused."""
        self.call("initialize", {"protocolVersion": 1, "clientInfo": {"name": "CRLF"}})
        created, _ = self.call("session/new", {"cwd": tempfile.gettempdir(), "mcpServers": []})
        session = created["result"]["sessionId"]
        options = {option["id"]: option for option in created["result"]["configOptions"]}
        self.assertEqual(list(options), ["model", "max_tokens"])
        self.assertEqual(options["model"]["category"], "model")
        self.assertEqual([choice["value"] for choice in options["model"]["options"]], ["large", "small"])
        self.assertEqual(options["model"]["currentValue"], "small")
        # 1234 is not one of the listed sizes, so it is added to them.
        self.assertEqual(options["max_tokens"]["currentValue"], "1234")
        self.assertIn("1234", [choice["value"] for choice in options["max_tokens"]["options"]])

        changed, updates = self.call("session/set_config_option",
                                     {"sessionId": session, "configId": "model", "value": "large"})
        self.assertEqual(changed["result"]["configOptions"][0]["currentValue"], "large")
        self.assertEqual(updates[0]["sessionUpdate"], "config_option_update")

        refused, _ = self.call("session/set_config_option",
                               {"sessionId": session, "configId": "model", "value": "huge"})
        self.assertEqual(refused["error"]["code"], -32602)

        ended, updates = self.call("session/prompt",
                                   {"sessionId": session, "prompt": [{"type": "text", "text": "who are you"}]})
        self.assertEqual(ended["result"], {"stopReason": "end_turn"})
        self.assertEqual(updates[0]["content"]["text"], "I am large")
        self.assertEqual(self.fake.posts[0]["max_tokens"], 1234)


if __name__ == "__main__":
    unittest.main()
