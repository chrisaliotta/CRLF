"""Confirms the reference agent against scripted providers.

No provider is reached: each test starts a local HTTP server that plays one
and records what it was sent. The tests exist because a provider's wire
format is where the agent breaks without anyone noticing until a key is in
hand: how each stream becomes text, thinking, and tool calls; what each
provider must be handed back on the next request (Anthropic's signed
thinking blocks, OpenAI's reasoning items); which models are offered; and
how a setting the client picks reaches the request. The tools, the code
index, and the context budget are confirmed the same way, in temporary
folders and against a local server that plays the embedding provider. See
docs/agent.md.

    python tests/reference_agent_tests.py
"""

import copy
import http.server
import itertools
import json
import os
import queue
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest import mock

AGENT_DIRECTORY = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scripts", "agent")
sys.path.insert(0, AGENT_DIRECTORY)

import embeddings  # noqa: E402
import history  # noqa: E402
import index  # noqa: E402
import providers  # noqa: E402
import tools  # noqa: E402
import usage  # noqa: E402
from tools import files  # noqa: E402

TOOLS = [{"name": "read_file", "description": "Read a file",
          "parameters": {"type": "object", "properties": {"path": {"type": "string"}}, "required": ["path"]}}]


# The full-text index needs FTS5 in this Python's SQLite. Without it the agent uses grep, which has its own test.
needs_fts5 = unittest.skipUnless(index.has_fts5(), "this Python's SQLite has no FTS5")

CALL_IDS = itertools.count(1)


class FakeServer:
    """A local HTTP server that plays a provider. `post(path, body)` returns
    (status, content type, data) for one POST. `models` is the JSON a GET
    returns. Every POST body is kept in `posts`."""

    def __init__(self, post, models=None):
        self.posts = []
        fake = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *arguments):
                pass

            def answer(self, status, content_type, data):
                self.send_response(status)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(data)))
                # One request to a connection, so no handler is left reading one the client has dropped.
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(data)

            def do_GET(self):
                self.answer(200, "application/json", json.dumps(models or {"data": []}).encode())

            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                fake.posts.append(body)
                self.answer(*post(self.path, body))

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.url = "http://127.0.0.1:%d" % self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def close(self):
        self.server.shutdown()
        self.server.server_close()


def fake_provider(answer, models=None):
    """A model provider: `answer(path, body)` returns the events to stream for one POST."""
    def post(path, body):
        events = "".join("data: %s\n\n" % json.dumps(event) for event in answer(path, body))
        return 200, "text/event-stream", events.encode()
    return FakeServer(post, models)


def fake_embeddings(vector):
    """An embedding provider: `vector(text)` is the embedding of one text."""
    def post(path, body):
        data = [{"index": number, "embedding": vector(text)} for number, text in enumerate(body["input"])]
        return 200, "application/json", json.dumps({"data": data}).encode()
    return FakeServer(post)


def embedded(fake):
    """Every text the fake embedding provider was sent, in order."""
    return [text for body in fake.posts for text in body["input"]]


def write(folder, name, text):
    path = os.path.join(folder, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def tool_calls(*calls):
    """The Chat Completions events of one reply that asks for these (name, arguments) calls."""
    parts = [{"index": number, "id": "call_%d" % next(CALL_IDS),
              "function": {"name": name, "arguments": json.dumps(arguments)}}
             for number, (name, arguments) in enumerate(calls)]
    return [{"choices": [{"delta": {"tool_calls": parts}, "finish_reason": "tool_calls"}]}]


def tool_results(post):
    """What the tools answered, as one Chat Completions request carries it."""
    return [message["content"] for message in post["messages"] if message["role"] == "tool"]


def chat_usage(prompt, cached, completion):
    """The last Chat Completions event of a reply: the tokens it was billed for."""
    return [{"choices": [], "usage": {"prompt_tokens": prompt, "prompt_tokens_details": {"cached_tokens": cached},
                                      "completion_tokens": completion}}]


def usage_updates(updates):
    return [update for update in updates if update.get("sessionUpdate") == "usage_update"]


def unsized_usage(updates):
    """The _crlf/usage reports among what a call returned. Only they have no sessionUpdate."""
    return [update for update in updates if "sessionUpdate" not in update]


class TemporaryFolderTest(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.folder = folder.name


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
        fake = fake_provider(answer, models)
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

    def test_each_wire_format_reports_the_tokens_of_a_reply(self):
        """The usage line and the cost are built from the tokens a provider
        says a reply used. Each API reports them in its own place and with
        its own meaning: OpenAI's input count includes the cached tokens,
        and Anthropic's leaves them out and counts cache reads and writes
        apart. All three come out in one shape, where input is everything
        sent. A server that reports nothing leaves the usage empty."""
        expected = {"input": 1000, "cached": 600, "cache_write": 0, "output": 50}
        history_ = [{"role": "user", "content": "hi"}]

        chat = self.serve(lambda path, body: [{"choices": [{"delta": {"content": "ok"}}]}] + chat_usage(1000, 600, 50))
        reply, _, _ = run(providers.create("openai-compatible", chat.url), history_, {"model": "local"})
        self.assertEqual(reply.usage, expected)
        # A stream carries its counts only when the request asks for them.
        self.assertEqual(chat.posts[0]["stream_options"], {"include_usage": True})

        os.environ["OPENAI_API_KEY"] = "k"
        responses = self.serve(lambda path, body: [{"type": "response.completed", "response": {
            "output": [], "usage": {"input_tokens": 1000, "input_tokens_details": {"cached_tokens": 600},
                                    "output_tokens": 50}}}])
        reply, _, _ = run(providers.create("openai", responses.url), history_, {"model": "gpt-4.1"})
        self.assertEqual(reply.usage, expected)

        os.environ["ANTHROPIC_API_KEY"] = "k"
        anthropic = self.serve(lambda path, body: [
            {"type": "message_start", "message": {"usage": {
                "input_tokens": 100, "cache_read_input_tokens": 600, "cache_creation_input_tokens": 300,
                "output_tokens": 1}}},
            {"type": "content_block_start", "index": 0, "content_block": {"type": "text", "text": ""}},
            {"type": "content_block_delta", "index": 0, "delta": {"type": "text_delta", "text": "ok"}},
            {"type": "message_delta", "delta": {"stop_reason": "end_turn"}, "usage": {"output_tokens": 50}}])
        reply, _, _ = run(providers.create("anthropic", anthropic.url), history_,
                          {"model": "claude", "thinking": "off", "max_tokens": "8192"})
        self.assertEqual(reply.usage, dict(expected, cache_write=300))

        silent = self.serve(lambda path, body: [{"choices": [{"delta": {"content": "ok"}}]}])
        reply, _, _ = run(providers.create("openai-compatible", silent.url), history_, {"model": "local"})
        self.assertEqual(reply.usage, {})

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

    def test_anthropic_request_marks_what_to_cache(self):
        """Anthropic caches a prompt only up to a marked block, and bills
        the rest in full on every call. The three markers are the system
        prompt, the last tool, and the newest message block; a missing one
        costs money without any error, which is why this is checked."""
        fake = self.serve(lambda path, body: [{"type": "message_delta", "delta": {"stop_reason": "end_turn"}}])
        os.environ["ANTHROPIC_API_KEY"] = "k"
        adapter = providers.create("anthropic", fake.url)
        history = [{"role": "user", "content": "first"}, {"role": "assistant", "content": "answer", "tool_calls": []},
                   {"role": "user", "content": "second"}]
        run(adapter, history, {"model": "claude"})
        sent = fake.posts[0]
        marker = {"type": "ephemeral"}
        self.assertEqual(sent["system"], [{"type": "text", "text": "system", "cache_control": marker}])
        self.assertEqual(sent["tools"][-1]["cache_control"], marker)
        self.assertEqual(sent["messages"][-1]["content"][-1],
                         {"type": "text", "text": "second", "cache_control": marker})
        # Only the newest block is marked: a marker on an older one would move the cached prefix back.
        self.assertNotIn("cache_control", sent["messages"][0]["content"][0])

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


class FakeContext:
    """The session as a tool sees it. `texts` is what the editor reads for
    each path, and every request a tool makes is kept in `requests`."""

    def __init__(self, cwd, texts=None):
        self.cwd = cwd
        self.texts = texts or {}
        self.requests = []

    def request(self, method, params, timeout=None):
        self.requests.append((method, params))
        return {"content": self.texts[params["path"]]} if method == "fs/read_text_file" else {}

    def record_write(self, path, old, new):
        pass


class ToolTests(TemporaryFolderTest):
    """Tools called as the session calls them, with the editor played by FakeContext."""

    def test_file_search_ranks_a_file_name_above_a_folder_name(self):
        """A model that asks for "agent" wants agent.py, not every file
        under agent/. The ranking exists so the file it means comes first
        in a result that is cut at 20 paths."""
        write(self.folder, "agent/readme.txt", "")
        write(self.folder, "agent/notes.txt", "")
        write(self.folder, "src/agent.py", "")
        write(self.folder, "src/other.py", "")
        text, _, _ = files.file_search(FakeContext(self.folder), {"query": "agent"})
        self.assertEqual(text.split("\n"), ["src/agent.py", "agent/notes.txt", "agent/readme.txt"])

    def test_edit_file_with_several_edits_is_one_write_or_none(self):
        """The user reviews each write, so several edits to one file must
        arrive as one diff. When one edit does not match, nothing is
        written: half of a change is worse than none."""
        path = os.path.join(self.folder, "a.txt")
        context = FakeContext(self.folder, {path: "one\ntwo\nthree\n"})
        files.edit_file(context, {"path": "a.txt", "edits": [{"old_text": "one", "new_text": "1"},
                                                              {"old_text": "three", "new_text": "3"}]})
        writes = [params for method, params in context.requests if method == "fs/write_text_file"]
        self.assertEqual(writes, [{"path": path, "content": "1\ntwo\n3\n"}])

        context = FakeContext(self.folder, {path: "one\ntwo\nthree\n"})
        with self.assertRaises(tools.ToolError):
            files.edit_file(context, {"path": "a.txt", "edits": [{"old_text": "one", "new_text": "1"},
                                                                  {"old_text": "four", "new_text": "4"}]})
        self.assertEqual([method for method, _ in context.requests], ["fs/read_text_file"])


class IndexTests(TemporaryFolderTest):
    """The code index behind codebase_search, on a temporary folder."""

    def create(self, kind, embedder=None):
        created = index.create(kind, self.folder, os.path.join(self.folder, ".crlf", "data"), embedder,
                               lambda: tools.walk(self.folder))
        # Windows does not delete the folder while the index has its file open.
        self.addCleanup(created.close)
        return created

    def embedder(self, vector):
        fake = fake_embeddings(vector)
        self.addCleanup(fake.close)
        return fake, embeddings.create("openai-compatible", "fake-model", fake.url)

    def test_grep_ranks_by_how_many_words_match(self):
        """The grep index is what answers with --index grep and while
        another index is still being built, so its ranking is what a model
        sees first: the passage with more of the query's words wins."""
        write(self.folder, "one.txt", "the parser reads a header\n")
        write(self.folder, "two.txt", "the parser\n")
        write(self.folder, "three.txt", "nothing here\n")
        found = self.create("grep").search("parser header", 8)
        self.assertEqual([result[:3] for result in found], [("one.txt", 1, 1), ("two.txt", 1, 1)])

    @needs_fts5
    def test_fts_finds_a_chunk_and_an_edit_after_refresh(self):
        """The default index. A search finds the passage by its stemmed
        words, and a refresh reads only the file that changed: that is what
        keeps the refresh before each search cheap."""
        write(self.folder, "parser.py", "def parse_header(line):\n    return line.split(':')\n")
        write(self.folder, "notes.txt", "Shopping: milk and eggs.\n")
        fts = self.create("fts")
        progress = []
        self.assertFalse(fts.ready)
        fts.refresh(lambda done, total: progress.append((done, total)))
        self.assertTrue(fts.ready)
        self.assertEqual(progress, [(1, 2), (2, 2)])
        self.assertEqual([result[:3] for result in fts.search("parsing headers", 8)], [("parser.py", 1, 2)])
        self.assertEqual(fts.search("tokenizer", 8), [])
        # The index folder keeps itself out of git.
        with open(os.path.join(self.folder, ".crlf", "data", ".gitignore")) as handle:
            self.assertEqual(handle.read(), "*\n")

        write(self.folder, "notes.txt", "The tokenizer splits a line into words.\n")
        del progress[:]
        fts.refresh(lambda done, total: progress.append((done, total)))
        self.assertEqual(progress, [(1, 1)])
        self.assertEqual([result[0] for result in fts.search("tokenizer", 8)], ["notes.txt"])

    def test_vector_ranks_the_nearest_chunk_first(self):
        """The vector index ranks by the angle between the query's
        embedding and each chunk's. The comparison is the plain Python one,
        which every install has; sqlite-vec and numpy only make it faster."""
        topics = ("apple", "river", "engine")
        fake, embedder = self.embedder(lambda text: [text.count(topic) for topic in topics])
        write(self.folder, "fruit.txt", "apple pie and apple juice\n")
        write(self.folder, "water.txt", "the river runs to the river mouth\n")
        write(self.folder, "car.txt", "engine oil for the engine\n")
        with mock.patch.dict(sys.modules, {"sqlite_vec": None, "numpy": None}):
            vectors = self.create("vector", embedder)
        self.assertEqual(vectors.comparison, "python")
        vectors.refresh(lambda done, total: None)
        found = vectors.search("where does the river go", 2)
        self.assertEqual(found[0][0], "water.txt")
        self.assertAlmostEqual(found[0][4], 1.0, places=5)

    def test_vector_embeds_only_text_it_has_not_seen(self):
        """Embedding is the part of the vector index that costs time and
        money. An edit must embed only the chunks whose text changed, and
        text that comes back, as it does when a branch is switched and
        switched again, must embed nothing."""
        fake, embedder = self.embedder(lambda text: [1.0, 0.0])
        original = "".join("line %d of the module\n" % number for number in range(1, 151))
        write(self.folder, "module.py", original)
        vectors = self.create("vector", embedder)
        vectors.refresh(lambda done, total: None)
        chunks = len(embedded(fake))
        self.assertGreater(chunks, 2)

        write(self.folder, "module.py", original.replace("line 140 of", "line 140, edited, of"))
        vectors.refresh(lambda done, total: None)
        changed = embedded(fake)[chunks:]
        self.assertEqual(len(changed), 1)
        self.assertIn("line 140, edited, of", changed[0])

        write(self.folder, "module.py", original)
        vectors.refresh(lambda done, total: None)
        self.assertEqual(len(embedded(fake)), chunks + 1)


class HistoryTests(unittest.TestCase):
    def test_a_conversation_over_the_budget_loses_its_oldest_tool_results(self):
        """Old tool results are most of what every model call sends again.
        Over the budget, the oldest become a line that names the call, down
        to 70 percent of the budget. The calls and the newest result stay,
        because a provider rejects a tool call that has no result."""
        calls = [{"id": "c%d" % number, "name": "read_file", "arguments": {"path": "%d.txt" % number}}
                 for number in range(3)]
        conversation = [{"role": "user", "content": "read them"},
                        {"role": "assistant", "content": "", "tool_calls": calls, "provider_state": None}]
        conversation += [{"role": "tool", "tool_call_id": call["id"], "name": "read_file", "content": "x" * 4000,
                          "is_error": False} for call in calls]
        before = copy.deepcopy(conversation)

        self.assertEqual(history.shorten(conversation, history.UNLIMITED), 0)
        self.assertEqual(conversation, before)

        # 2,000 tokens are 8,000 characters, and the three results are 12,000.
        self.assertEqual(history.shorten(conversation, "2000"), 2)
        self.assertEqual(conversation[2]["content"],
                         '[The result of read_file({"path": "0.txt"}) was dropped to save context. '
                         'Call it again if you need it.]')
        self.assertTrue(conversation[3]["content"].startswith("[The result of read_file("))
        self.assertEqual(conversation[4], before[4])
        self.assertEqual(conversation[:2], before[:2])
        self.assertEqual([message["tool_call_id"] for message in conversation[2:]], ["c0", "c1", "c2"])
        self.assertLessEqual(history.estimate(conversation), 2000 * history.TARGET)
        # Under the budget again, nothing more is touched until it is passed again.
        self.assertEqual(history.shorten(conversation, "2000"), 0)


class UsageTests(TemporaryFolderTest):
    def lists(self, shipped, own=None):
        """The prices of a shipped list and, over it, a user's file."""
        write(self.folder, "models.json", json.dumps(shipped))
        if own is None:
            return usage.load(None, os.path.join(self.folder, "models.json"))
        write(self.folder, "prices.json", json.dumps(own))
        return usage.load(os.path.join(self.folder, "prices.json"), os.path.join(self.folder, "models.json"))

    def test_prices_match_a_model_and_its_snapshots(self):
        """No provider reports a price, so the cost comes from a list of
        models, by model id. Providers serve one model under several ids,
        such as a dated snapshot, and nobody lists them all: an id uses the
        longest listed name it starts with. A model that is not listed has
        no price, so the client shows no cost instead of a wrong one. Cached
        input is cheaper and cache writes dearer than fresh input, and each
        is charged at its own price."""
        prices = self.lists({"currency": "EUR", "models": {
            "gpt-5": {"input": 3.0, "cached_input": 0.3, "cache_write": 3.75, "output": 15.0, "context": 400000},
            "gpt-5-mini": {"input": 0.25, "output": 2.0}}})
        self.assertEqual(prices.currency, "EUR")
        self.assertEqual(prices.entry("gpt-5")["context"], 400000)
        self.assertEqual(prices.entry("gpt-5-2025-08-07")["input"], 3.0)
        self.assertEqual(prices.entry("gpt-5-mini-2025-08-07")["input"], 0.25)
        self.assertIsNone(prices.entry("gpt-4.1"))

        # 100 fresh, 600 read from the cache, 300 written to it, and 50 out.
        reply = providers.token_usage(1000, 600, 300, 50)
        self.assertAlmostEqual(usage.cost(reply, prices.entry("gpt-5")),
                               (100 * 3.0 + 600 * 0.3 + 300 * 3.75 + 50 * 15.0) / 1000000)
        # With no price of their own, cached tokens cost what fresh ones do.
        self.assertAlmostEqual(usage.cost(reply, prices.entry("gpt-5-mini")), (1000 * 0.25 + 50 * 2.0) / 1000000)

    def test_the_users_file_is_read_over_the_shipped_list(self):
        """The shipped list is replaced by every upgrade and the user's file
        never is, so the user's file holds only what the user wants to
        differ. One field set there changes that field and keeps the rest of
        the shipped entry, so correcting a price does not lose the model's
        size; a model only the user lists, such as a local one, is added.
        With no file of the user's the shipped list stands alone. Shipped
        prices are dropped when the user's file is in another currency,
        because a cost summed from two currencies would be wrong, while the
        sizes, which have no currency, stay."""
        shipped = {"currency": "USD", "models": {
            "big": {"input": 2.0, "output": 10.0, "context": 1000000, "max_input": 900000},
            "other": {"input": 1.0, "output": 4.0, "context": 200000}}}
        prices = self.lists(shipped, {"currency": "USD", "models": {
            "big": {"input": 3.0}, "local": {"context": 32000}}})
        self.assertEqual(prices.entry("big"), {"input": 3.0, "output": 10.0, "context": 1000000, "max_input": 900000})
        self.assertEqual(prices.entry("other")["input"], 1.0)
        self.assertEqual(prices.entry("local"), {"context": 32000})

        self.assertEqual(self.lists(shipped).entry("big")["input"], 2.0)
        self.assertEqual(usage.load(os.path.join(self.folder, "none.json"),
                                    os.path.join(self.folder, "models.json")).entry("big")["input"], 2.0)

        euros = self.lists(shipped, {"currency": "EUR", "models": {"big": {"input": 3.0, "output": 9.0}}})
        self.assertEqual(euros.currency, "EUR")
        self.assertEqual(euros.entry("big"), {"input": 3.0, "output": 9.0, "context": 1000000, "max_input": 900000})
        self.assertEqual(euros.entry("other"), {"context": 200000})
        self.assertFalse(usage.priced(euros.entry("other")))

    def test_a_long_prompt_is_priced_at_the_long_prices(self):
        """Some providers charge more for a request whose input passes a
        threshold, and charge the whole request at the higher prices, not
        only the tokens past it. `long` carries the threshold and those
        prices. A request at the threshold is still ordinary; one token over
        is long. A price `long` does not give stays the ordinary one, which
        is how a provider that raises only input and output is listed."""
        prices = self.lists({"currency": "USD", "models": {
            "full": {"input": 10.0, "cached_input": 1.0, "cache_write": 12.5, "output": 50.0,
                     "long": {"over": 272000, "input": 20.0, "cached_input": 2.0, "cache_write": 25.0,
                              "output": 75.0}},
            "partial": {"input": 5.0, "cached_input": 0.5, "output": 30.0,
                        "long": {"over": 272000, "input": 10.0, "output": 45.0}}}})
        # 71,000 fresh, 200,000 read from the cache, 1,000 written to it, and 1,000 out.
        under = providers.token_usage(272000, 200000, 1000, 1000)
        self.assertAlmostEqual(usage.cost(under, prices.entry("full")),
                               (71000 * 10.0 + 200000 * 1.0 + 1000 * 12.5 + 1000 * 50.0) / 1000000)
        over = providers.token_usage(272001, 200000, 1000, 1000)
        self.assertAlmostEqual(usage.cost(over, prices.entry("full")),
                               (71001 * 20.0 + 200000 * 2.0 + 1000 * 25.0 + 1000 * 75.0) / 1000000)
        # Cache writes have no price here at all, so they cost what input does: the long input price.
        self.assertAlmostEqual(usage.cost(over, prices.entry("partial")),
                               (71001 * 10.0 + 200000 * 0.5 + 1000 * 10.0 + 1000 * 45.0) / 1000000)

    def test_the_shipped_list_is_consistent(self):
        """models.json is written by hand from the providers' pages, and a
        slip in it shows as a wrong percentage or cost, not as an error.
        This holds what must be true of every entry whatever the figures:
        the list parses, every model has a size, the input limit is not
        over the window, a long-prompt threshold is under the size it could
        be reached within, and the long prices are not under the ordinary
        ones. It cannot tell whether a figure matches the provider's page."""
        prices = usage.load(None)
        self.assertEqual(prices.currency, "USD")
        self.assertGreater(len(prices.models), 0)
        for name, entry in prices.models.items():
            with self.subTest(model=name):
                self.assertTrue(usage.priced(entry))
                self.assertLessEqual(entry.get("max_input", 0), entry["context"])
                if "long" in entry:
                    self.assertLess(entry["long"]["over"], entry.get("max_input", entry["context"]))
                    for price in ("input", "cached_input", "cache_write", "output"):
                        if price in entry["long"]:
                            self.assertGreaterEqual(entry["long"][price], entry[price])


class AgentTests(TemporaryFolderTest):
    """The agent as the editor sees it: over ACP, with a scripted provider
    and a scripted client."""

    def setUp(self):
        super().setUp()
        # The events of each model reply, in order. With none left, the model says its name.
        self.replies = []

        def answer(path, body):
            if self.replies:
                return self.replies.pop(0)
            return [{"choices": [{"delta": {"content": "I am " + body["model"]}, "finish_reason": "stop"}]}]
        self.fake = fake_provider(answer, {"data": [{"id": "small"}, {"id": "large"}]})
        self.addCleanup(self.fake.close)
        # What the client answers the agent's requests with, by method, and the requests it was sent.
        self.answers = {}
        self.requests = []
        self.next_id = 0

    def start(self, *flags, client="CRLF"):
        """Starts the agent with these flags, for a client of this name, and
        returns the reply to session/new."""
        self.agent = subprocess.Popen(
            [sys.executable, os.path.join(AGENT_DIRECTORY, "crlf_agent.py"), "--provider", "openai-compatible",
             "--base-url", self.fake.url, "--model", "small"] + list(flags),
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.addCleanup(self.stop)
        # The log is read as it is written, so the agent never waits on a full pipe.
        self.log = queue.Queue()
        threading.Thread(target=lambda: [self.log.put(line.decode()) for line in self.agent.stderr],
                         daemon=True).start()
        self.call("initialize", {"protocolVersion": 1, "clientInfo": {"name": client}})
        created, _ = self.call("session/new", {"cwd": self.folder, "mcpServers": []})
        self.session = created["result"].get("sessionId") if "result" in created else None
        return created

    def stop(self):
        self.agent.stdin.close()
        self.agent.wait()
        self.agent.stdout.close()
        self.agent.stderr.close()

    def send(self, message):
        self.agent.stdin.write((json.dumps(dict(message, jsonrpc="2.0")) + "\n").encode())
        self.agent.stdin.flush()

    def call(self, method, params):
        """The reply to one request, and the session updates that came
        before it. The agent's own requests are answered from `answers`."""
        self.next_id += 1
        self.send({"id": self.next_id, "method": method, "params": params})
        updates = []
        while True:
            message = json.loads(self.agent.stdout.readline())
            if "method" not in message:
                return message, updates
            if "id" in message:
                self.requests.append((message["method"], message["params"]))
                self.send({"id": message["id"], "result": self.answers[message["method"]]})
            else:
                updates.append(message["params"]["update"])

    def prompt(self, text):
        return self.call("session/prompt", {"sessionId": self.session, "prompt": [{"type": "text", "text": text}]})

    def wait_for_log(self, start):
        """Reads the agent's log up to a line that starts with `start`."""
        while not self.log.get(timeout=30).startswith(start):
            pass

    def test_the_client_picks_the_model_and_settings(self):
        """session/new offers the provider's models and the settings, at the
        values the command line gave. A choice is answered with the whole
        list and used by the next reply; a value that was not offered is
        refused."""
        created = self.start("--max-tokens", "1234")
        session = self.session
        options = {option["id"]: option for option in created["result"]["configOptions"]}
        self.assertEqual(list(options), ["model", "max_tokens", "context_budget"])
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

        ended, updates = self.prompt("who are you")
        self.assertEqual(ended["result"], {"stopReason": "end_turn"})
        self.assertEqual(updates[0]["content"]["text"], "I am large")
        self.assertEqual(self.fake.posts[0]["max_tokens"], 1234)

    def test_the_context_option_shortens_the_conversation(self):
        """The Context option is the session's own, not the provider's. It
        starts at --context-budget, a count the list does not have is added
        to it, and the value the client sets is the budget of the next model
        call. The provider is never sent the setting, which it would refuse."""
        created = self.start("--context-budget", "75000")
        option = created["result"]["configOptions"][-1]
        self.assertEqual((option["id"], option["name"], option["currentValue"]),
                         (history.OPTION_ID, "Context", "75000"))
        self.assertEqual([choice["value"] for choice in option["options"]],
                         ["unlimited", "50000", "100000", "200000", "75000"])

        changed, _ = self.call("session/set_config_option",
                               {"sessionId": self.session, "configId": history.OPTION_ID, "value": "50000"})
        self.assertEqual(changed["result"]["configOptions"][-1]["currentValue"], "50000")

        # Eight reads of 30,000 characters are 60,000 tokens: under the 75,000 it started with, over the 50,000 set.
        self.answers["fs/read_text_file"] = {"content": "x" * 30000}
        self.replies.append(tool_calls(*[("read_file", {"path": "%d.txt" % number}) for number in range(8)]))
        ended, _ = self.prompt("read them all")
        self.assertEqual(ended["result"], {"stopReason": "end_turn"})
        sent = self.fake.posts[1]
        results = tool_results(sent)
        self.assertEqual(len(results), 8)
        self.assertTrue(results[0].startswith('[The result of read_file({"path": "0.txt"}) was dropped'))
        self.assertTrue(results[-1].startswith("     1|xxx"))
        self.assertEqual(len(sent["messages"][2]["tool_calls"]), 8)
        self.assertNotIn(history.OPTION_ID, sent)

    def test_a_turn_reports_the_context_in_use_and_the_cost(self):
        """The client's usage line is drawn from usage_update, which the
        agent sends after each model reply. `used` is what the next request
        will carry, `size` is the model's window from the prices file, the
        cost is the session's tokens at the listed prices, and the totals
        keep running across replies. Setting a Context budget makes that the
        size, because it is then the limit the conversation is held to."""
        write(self.folder, "prices.json", json.dumps({"currency": "USD", "models": {
            "small": {"input": 2.0, "cached_input": 0.5, "output": 10.0, "context": 100000}}}))
        self.start("--prices", os.path.join(self.folder, "prices.json"))
        self.answers["fs/read_text_file"] = {"content": "text"}
        self.replies.append(tool_calls(("read_file", {"path": "a.txt"})) + chat_usage(1000, 0, 100))
        self.replies.append([{"choices": [{"delta": {"content": "done"}}]}] + chat_usage(3000, 1000, 200))

        _, updates = self.prompt("read it")
        first, second = usage_updates(updates)
        self.assertEqual((first["used"], first["size"]), (1100, 100000))
        self.assertEqual(first["cost"], {"amount": 0.003, "currency": "USD"})
        self.assertEqual(first["_meta"]["crlf"], {"inputTokens": 1000, "cachedTokens": 0, "outputTokens": 100})
        self.assertEqual((second["used"], second["size"]), (3200, 100000))
        # The second reply: 2,000 fresh, 1,000 cached, 200 out.
        self.assertAlmostEqual(second["cost"]["amount"], 0.003 + (2000 * 2.0 + 1000 * 0.5 + 200 * 10.0) / 1000000)
        self.assertEqual(second["_meta"]["crlf"], {"inputTokens": 4000, "cachedTokens": 1000, "outputTokens": 300})

        _, updates = self.call("session/set_config_option",
                               {"sessionId": self.session, "configId": history.OPTION_ID, "value": "50000"})
        self.assertEqual([(update["used"], update["size"]) for update in usage_updates(updates)], [(3200, 50000)])

    def test_the_size_is_the_most_input_the_model_accepts(self):
        """The percentage says how near the conversation is to being
        refused. A model's window also holds its reply, so the input limit
        is reached first, and the size is `max_input` when the model lists
        one and its `context` when it does not. --context-window is the
        user's word for this run and comes before either."""
        write(self.folder, "prices.json", json.dumps({"currency": "USD", "models": {
            "small": {"context": 1050000, "max_input": 922000}, "large": {"context": 128000}}}))
        prices = os.path.join(self.folder, "prices.json")
        self.start("--prices", prices)
        self.replies.append([{"choices": [{"delta": {"content": "done"}}]}] + chat_usage(1000, 0, 100))
        _, updates = self.prompt("hello")
        self.assertEqual([update["size"] for update in usage_updates(updates)], [922000])
        _, updates = self.call("session/set_config_option",
                               {"sessionId": self.session, "configId": "model", "value": "large"})
        self.assertEqual([update["size"] for update in usage_updates(updates)], [128000])
        self.stop()

        self.start("--prices", prices, "--context-window", "64000")
        self.replies.append([{"choices": [{"delta": {"content": "done"}}]}] + chat_usage(1000, 0, 100))
        _, updates = self.prompt("hello")
        self.assertEqual([update["size"] for update in usage_updates(updates)], [64000])

    def test_usage_without_a_size_goes_to_crlf_alone(self):
        """usage_update has no way to say the context's size is unknown, and
        a percentage of a guess would mislead. For a model no list has, with
        no --context-window and no Context budget, the agent sends no
        usage_update. CRLF is told the tokens and totals anyway, as
        _crlf/usage, so its line shows them without a percentage; another
        client, which would not know the method, is sent nothing. A budget
        set later gives the context a size, and usage_update takes over."""
        self.start()
        self.replies.append([{"choices": [{"delta": {"content": "done"}}]}] + chat_usage(1000, 0, 100))
        _, updates = self.prompt("hello")
        self.assertEqual(usage_updates(updates), [])
        self.assertEqual(unsized_usage(updates), [
            {"used": 1100, "_meta": {"crlf": {"inputTokens": 1000, "cachedTokens": 0, "outputTokens": 100}}}])

        _, updates = self.call("session/set_config_option",
                               {"sessionId": self.session, "configId": history.OPTION_ID, "value": "50000"})
        reported = usage_updates(updates)
        self.assertEqual([(update["used"], update["size"]) for update in reported], [(1100, 50000)])
        # The model has no listed price, so no cost is claimed.
        self.assertNotIn("cost", reported[0])
        self.assertEqual(unsized_usage(updates), [])
        self.stop()

        self.start(client="Another editor")
        self.replies.append([{"choices": [{"delta": {"content": "done"}}]}] + chat_usage(1000, 0, 100))
        _, updates = self.prompt("hello")
        self.assertEqual([update["sessionUpdate"] for update in updates], ["agent_message_chunk"])

    def test_the_budget_counts_what_the_provider_counted(self):
        """The context budget is held against the tokens the provider counts,
        not only the size of the history. The system prompt and the tool
        definitions are sent with every request and are in no message, so a
        budget measured on the history alone would be passed long before it
        acted. Here the provider counts 45,000 tokens for a request whose
        history is a few words; one 30,000-character result on top passes a
        50,000 budget, and is shortened before the next request."""
        self.start("--context-budget", "50000")
        self.answers["fs/read_text_file"] = {"content": "x" * 30000}
        self.replies.append(tool_calls(("read_file", {"path": "big.txt"})) + chat_usage(45000, 0, 20))
        ended, _ = self.prompt("read it")
        self.assertEqual(ended["result"], {"stopReason": "end_turn"})
        self.assertTrue(tool_results(self.fake.posts[1])[0].startswith(
            '[The result of read_file({"path": "big.txt"}) was dropped'))

    def test_a_background_command_returns_before_it_exits(self):
        """A server or a watcher never exits, so a turn that waited for it
        would never end. The command's id comes back at once, its output is
        read later by that id, and its terminal is released when the
        session ends, so the editor is not left holding it."""
        self.start()
        self.answers = {"terminal/create": {"terminalId": "term-1"},
                        "terminal/output": {"output": "listening on 8080\n", "truncated": False}}
        self.replies += [tool_calls(("run_terminal_cmd", {"command": "python -m http.server", "background": True})),
                         tool_calls(("read_command_output", {"id": "term-1"}))]
        ended, _ = self.prompt("start the server")
        self.assertEqual(ended["result"], {"stopReason": "end_turn"})
        # Nothing waited for the command to exit, and nothing released it.
        self.assertEqual([method for method, _ in self.requests], ["terminal/create", "terminal/output"])
        self.assertIn("the id term-1", tool_results(self.fake.posts[1])[0])
        self.assertEqual(tool_results(self.fake.posts[2])[1], "Still running.\nlistening on 8080\n")

        self.agent.stdin.close()
        last = [json.loads(line) for line in self.agent.stdout]
        self.assertEqual([(message["method"], message["params"]["terminalId"]) for message in last],
                         [("terminal/release", "term-1")])

    @needs_fts5
    def test_codebase_search_asks_the_index_built_in_the_background(self):
        """With --index fts, session/new answers at once and the index is
        built behind it, with its progress in the log as the status. Once
        it is built, codebase_search answers from it: "parsing" finds
        "parse", which matching the words in the files would not."""
        write(self.folder, "parser.py", "def parse_header(line):\n    return line.split(':')\n")
        self.start("--index", "fts")
        self.wait_for_log("CRLF_STATUS: indexing 1/1")
        # The status goes back to what it was when the refresh ends.
        self.wait_for_log("CRLF_STATUS: ready: small")
        self.replies.append(tool_calls(("codebase_search", {"query": "parsing headers"})))
        ended, _ = self.prompt("where are headers parsed")
        self.assertEqual(ended["result"], {"stopReason": "end_turn"})
        self.assertEqual(tool_results(self.fake.posts[1]),
                         ["parser.py:1-2\ndef parse_header(line):\n    return line.split(':')"])

    @needs_fts5
    def test_the_default_index_and_rules_are_under_the_crlf_folder(self):
        """A project keeps what the agent uses in its .crlf folder, so an
        agent started with no folder options must find both there: the
        index is made in .crlf/data, along with the .crlf folder above it,
        and keeps itself out of git, and the rules are read from
        .crlf/rules. Nothing is left at the old .crlf-data."""
        write(self.folder, ".crlf/rules/style.md", "# Short lines\n")
        self.start()
        self.assertEqual(sorted(name for name in os.listdir(os.path.join(self.folder, ".crlf", "data"))
                                if not name.startswith("index.sqlite-")), [".gitignore", "index.sqlite"])
        with open(os.path.join(self.folder, ".crlf", "data", ".gitignore")) as handle:
            self.assertEqual(handle.read(), "*\n")
        self.assertEqual(sorted(os.listdir(self.folder)), [".crlf"])

        self.replies.append(tool_calls(("fetch_rules", {})))
        ended, _ = self.prompt("what are the rules")
        self.assertEqual(ended["result"], {"stopReason": "end_turn"})
        self.assertIn("The project has rules: style.", self.fake.posts[0]["messages"][0]["content"])
        self.assertEqual(tool_results(self.fake.posts[1]), ["style: Short lines"])

    def test_rules_come_from_every_folder_and_the_first_wins_a_name(self):
        """--rules is given twice so a project's rules and the
        application's are both offered: the first folder relative to the
        working directory, the second absolute. Each folder adds the names
        only it has, and a name in both is the first folder's rule, so a
        project can replace an application rule."""
        application = tempfile.TemporaryDirectory()
        self.addCleanup(application.cleanup)
        write(self.folder, "rules/style.md", "# Project style\n")
        write(self.folder, "rules/tests.md", "# Project tests\n")
        write(application.name, "style.md", "# Application style\n")
        write(application.name, "commits.md", "# Application commits\n")
        self.start("--index", "grep", "--rules", "rules", "--rules", application.name)
        self.replies += [tool_calls(("fetch_rules", {})),
                         tool_calls(("fetch_rules", {"name": "style"}), ("fetch_rules", {"name": "commits"}))]
        ended, _ = self.prompt("what are the rules")
        self.assertEqual(ended["result"], {"stopReason": "end_turn"})
        self.assertIn("The project has rules: commits, style, tests.", self.fake.posts[0]["messages"][0]["content"])
        self.assertEqual(tool_results(self.fake.posts[2]),
                         ["commits: Application commits\nstyle: Project style\ntests: Project tests",
                          "# Project style\n", "# Application commits\n"])

    def test_a_refresh_that_fails_leaves_search_on_word_matches(self):
        """An embedding server that is down must not take the agent with
        it. The failure is logged, the session goes on, and codebase_search
        answers with word matches and says the index is not ready."""
        def refuse(path, body):
            return 500, "application/json", json.dumps({"error": {"message": "down"}}).encode()
        embedder = FakeServer(refuse)
        self.addCleanup(embedder.close)
        write(self.folder, "parser.py", "def parse_header(line):\n    return line.split(':')\n")
        self.start("--index", "vector", "--embed-provider", "openai-compatible", "--embed-base-url", embedder.url,
                   "--embed-model", "fake-model")
        self.wait_for_log("The index could not be refreshed")
        self.replies.append(tool_calls(("codebase_search", {"query": "parse_header"})))
        ended, _ = self.prompt("where are headers parsed")
        self.assertEqual(ended["result"], {"stopReason": "end_turn"})
        result = tool_results(self.fake.posts[1])[0]
        self.assertTrue(result.startswith("[The index is still being built"))
        self.assertIn("parser.py:1-2\n", result)
        # The search started another refresh. It is let fail before the agent is stopped under it.
        self.wait_for_log("The index could not be refreshed")


if __name__ == "__main__":
    unittest.main()
