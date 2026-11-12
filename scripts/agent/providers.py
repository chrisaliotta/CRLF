"""Model providers for the reference agent.

Three wire formats cover every target: the OpenAI Responses API (OpenAI),
the Chat Completions API (xAI Grok, Ollama, LM Studio, llama.cpp, vLLM, and
any other OpenAI-compatible server), and the Anthropic Messages API. Each
adapter lists the provider's models, names the settings a model takes as
ACP config options, and streams one reply: the text, the thinking, the tool
calls, and why the model stopped.

    adapter = create(name, base_url, api_key_env, api_key_file)
    adapter.models()                 -> [(id, display name)]
    adapter.config_options(model)    -> [ACP select option, without currentValue]
    adapter.defaults(model)          -> {option id: value}
    adapter.stream(system, history, tools, settings, on_text, on_thought, cancel) -> Reply

`settings` is {"model": id, option id: value}; every value is a string. The
conversation is kept in one neutral form and converted for each request:

    {"role": "user", "content": str}
    {"role": "assistant", "content": str, "tool_calls": [{"id", "name", "arguments"}],
     "provider_state": {"model": str, "items": [...]}}
    {"role": "tool", "tool_call_id": str, "name": str, "content": str, "is_error": bool}

`provider_state` is the adapter's own: the thinking a provider wants handed
back with the reply it led to. It is dropped when the model has changed.
"""

import json
import os
import re
import sys
import urllib.error
import urllib.request

# Where each provider listens and which variable holds its key. A local
# server needs no key.
PRESETS = {
    "openai": ("responses", "https://api.openai.com/v1", "OPENAI_API_KEY"),
    "xai": ("chat", "https://api.x.ai/v1", "XAI_API_KEY"),
    "ollama": ("chat", "http://localhost:11434/v1", None),
    "lmstudio": ("chat", "http://localhost:1234/v1", None),
    "openai-compatible": ("chat", None, None),
    "anthropic": ("anthropic", "https://api.anthropic.com", "ANTHROPIC_API_KEY"),
}

TIMEOUT_SECONDS = 600
ANTHROPIC_VERSION = "2023-06-01"

# The value of a setting the request leaves out, so the provider decides.
DEFAULT = "default"

# Ids a provider lists that are not chat models, and dated snapshots of ones it also lists undated.
_NOT_CHAT = re.compile(r"embedding|tts|transcribe|realtime|audio|image|search|moderation|whisper|dall-e|live|sora|"
                       r"babbage|davinci")
_DATED = re.compile(r"-\d{4}-\d{2}-\d{2}$")


class ProviderError(Exception):
    def __init__(self, message, status=0):
        super().__init__(message)
        # The HTTP status, or 0 when the server was not reached.
        self.status = status


class Cancelled(Exception):
    pass


class Reply:
    def __init__(self):
        self.text = ""
        self.tool_calls = []
        # "end", "tool_use", or "max_tokens".
        self.stop = "end"
        # Kept on the assistant message for this adapter's next request.
        self.provider_state = None


def read_key(key_file, key_name, file_flag="--api-key-file"):
    """A key file wins over the environment. Its path is relative to the
    agent's working directory, and surrounding whitespace is dropped.
    `file_flag` is the flag the error names. embeddings.py reads its key here too."""
    if key_file:
        path = os.path.abspath(key_file)
        try:
            with open(path, encoding="utf-8") as handle:
                key = handle.read().strip()
        except OSError as error:
            raise ProviderError("Could not read the key file %s: %s" % (path, error.strerror))
        if not key:
            raise ProviderError("The key file %s is empty." % path)
        return key
    key = os.environ.get(key_name, "") if key_name else ""
    if key_name and not key:
        raise ProviderError("%s is not set. Set it in the environment CRLF starts from, or pass %s."
                            % (key_name, file_flag))
    return key


def create(name, base_url=None, api_key_env=None, api_key_file=None):
    """Raises ProviderError when the provider is unknown or its key is missing."""
    if name not in PRESETS:
        raise ProviderError("Unknown provider %r. Use one of: %s" % (name, ", ".join(sorted(PRESETS))))
    wire, default_url, default_key = PRESETS[name]
    base_url = (base_url or default_url or "").rstrip("/")
    if not base_url:
        raise ProviderError("--base-url is required for %s" % name)
    key = read_key(api_key_file, api_key_env or default_key)
    if wire == "anthropic":
        return Anthropic(base_url, key)
    if wire == "responses":
        return OpenAIResponses(base_url, key)
    return ChatCompletions(base_url, key, name == "xai")


def _open(request):
    try:
        return urllib.request.urlopen(request, timeout=TIMEOUT_SECONDS)
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", "replace")
        try:
            detail = json.loads(detail).get("error", {}).get("message", detail)
        except (ValueError, AttributeError):
            pass
        raise ProviderError("%s returned %d: %s" % (request.full_url, error.code, detail), error.code)
    except urllib.error.URLError as error:
        raise ProviderError("Could not reach %s: %s" % (request.full_url, error.reason))


def _get(url, headers):
    with _open(urllib.request.Request(url, headers=headers)) as response:
        return json.loads(response.read().decode("utf-8"))


def _post(url, headers, body):
    return _open(urllib.request.Request(url, data=json.dumps(body).encode("utf-8"),
                                        headers=dict(headers, **{"Content-Type": "application/json"}), method="POST"))


def post(url, headers, body):
    """The JSON a POST answers with. embeddings.py sends its requests here."""
    with _post(url, headers, body) as response:
        return json.loads(response.read().decode("utf-8"))


def _events(url, headers, body, cancel):
    """Yields the JSON of each server-sent `data:` line of a streamed POST."""
    with _post(url, headers, body) as response:
        for raw in response:
            if cancel.is_set():
                raise Cancelled()
            line = raw.decode("utf-8", "replace").strip()
            if not line.startswith("data:"):
                continue
            data = line[5:].strip()
            if data == "[DONE]":
                return
            yield json.loads(data)


def _arguments(text):
    try:
        value = json.loads(text) if text.strip() else {}
        return value if isinstance(value, dict) else {"_invalid": text}
    except ValueError:
        return {"_invalid": text}


def _chat_models(ids):
    return [(id, id) for id in sorted(ids) if not _NOT_CHAT.search(id) and not _DATED.search(id)]


def select(id, name, category, description, values):
    return {"id": id, "name": name, "description": description, "category": category, "type": "select",
            "options": [{"value": value, "name": label} for value, label in values]}


def _levels(*values):
    return [(DEFAULT, "Default")] + [(value, value.capitalize()) for value in values]


def _max_tokens_option(values):
    return select("max_tokens", "Max tokens", "model_config", "The longest reply the model may write",
                  [(value, "Default" if value == DEFAULT else "{:,}".format(int(value))) for value in values])


def _chosen(settings, id):
    """The setting's value, or None when the provider is left to decide."""
    value = settings.get(id, DEFAULT)
    return None if value == DEFAULT else value


def _state(message, model):
    """The items this adapter kept on an assistant message, for the same model."""
    state = message.get("provider_state")
    return state["items"] if state and state["model"] == model else []


class ChatCompletions:
    """The OpenAI-compatible Chat Completions API."""

    def __init__(self, base_url, key, xai):
        self.base_url = base_url
        self.headers = {"Authorization": "Bearer " + key} if key else {}
        self.xai = xai

    def models(self):
        return _chat_models(entry["id"] for entry in _get(self.base_url + "/models", self.headers)["data"])

    def _takes_effort(self, model):
        # Of xAI's models, only the grok-3-mini family accepts reasoning_effort.
        return self.xai and model.startswith("grok-3-mini")

    def config_options(self, model):
        options = []
        if self._takes_effort(model):
            options.append(select("reasoning_effort", "Reasoning", "thought_level",
                                  "How much the model thinks before it answers", _levels("low", "high")))
        options.append(_max_tokens_option([DEFAULT, "4096", "8192", "16384", "32768"]))
        return options

    def defaults(self, model):
        return {option["id"]: DEFAULT for option in self.config_options(model)}

    @staticmethod
    def _messages(system, history):
        messages = [{"role": "system", "content": system}]
        for message in history:
            if message["role"] == "assistant":
                entry = {"role": "assistant", "content": message["content"] or None}
                if message.get("tool_calls"):
                    entry["tool_calls"] = [
                        {"id": call["id"], "type": "function",
                         "function": {"name": call["name"], "arguments": json.dumps(call["arguments"])}}
                        for call in message["tool_calls"]
                    ]
                messages.append(entry)
            elif message["role"] == "tool":
                messages.append({"role": "tool", "tool_call_id": message["tool_call_id"], "content": message["content"]})
            else:
                messages.append({"role": "user", "content": message["content"]})
        return messages

    def stream(self, system, history, tools, settings, on_text, on_thought, cancel):
        model = settings["model"]
        body = {
            "model": model,
            "messages": self._messages(system, history),
            "tools": [{"type": "function", "function": {"name": t["name"], "description": t["description"],
                                                         "parameters": t["parameters"]}} for t in tools],
            "stream": True,
        }
        if _chosen(settings, "max_tokens"):
            body["max_tokens"] = int(settings["max_tokens"])
        if self._takes_effort(model) and _chosen(settings, "reasoning_effort"):
            body["reasoning_effort"] = settings["reasoning_effort"]

        reply = Reply()
        calls = {}
        for chunk in _events(self.base_url + "/chat/completions", self.headers, body, cancel):
            if chunk.get("error"):
                raise ProviderError(chunk["error"].get("message", str(chunk["error"])))
            for choice in chunk.get("choices", []):
                delta = choice.get("delta") or {}
                if delta.get("content"):
                    reply.text += delta["content"]
                    on_text(delta["content"])
                thought = delta.get("reasoning_content") or delta.get("reasoning")
                if thought:
                    on_thought(thought)
                for part in delta.get("tool_calls") or []:
                    call = calls.setdefault(part.get("index", 0), {"id": "", "name": "", "arguments": ""})
                    call["id"] = part.get("id") or call["id"]
                    function = part.get("function") or {}
                    call["name"] += function.get("name") or ""
                    call["arguments"] += function.get("arguments") or ""
                if choice.get("finish_reason") == "length":
                    reply.stop = "max_tokens"
        for index in sorted(calls):
            call = calls[index]
            reply.tool_calls.append({"id": call["id"] or "call_%d" % index, "name": call["name"],
                                     "arguments": _arguments(call["arguments"])})
        if reply.tool_calls:
            reply.stop = "tool_use"
        return reply


class OpenAIResponses:
    """The OpenAI Responses API. It is the one that streams a reasoning
    model's summary, and takes the reasoning it produced back on the next
    request, encrypted, without storing the conversation at OpenAI."""

    def __init__(self, base_url, key):
        self.base_url = base_url
        self.headers = {"Authorization": "Bearer " + key}

    def models(self):
        return _chat_models(entry["id"] for entry in _get(self.base_url + "/models", self.headers)["data"])

    @staticmethod
    def _reasons(model):
        """The o-series, and GPT-5 and later. GPT-4 models take none of the reasoning settings."""
        return re.match(r"o\d|gpt-[5-9]", model) is not None

    def config_options(self, model):
        options = []
        if self._reasons(model):
            options += [
                select("reasoning_effort", "Reasoning", "thought_level",
                       "How much the model thinks before it answers. Not every model takes every level",
                       _levels("minimal", "low", "medium", "high")),
                select("reasoning_summary", "Thinking", "model_config",
                       "Ask for a summary of the model's reasoning, shown as its thinking",
                       [("auto", "Summary"), ("off", "Off")]),
                select("verbosity", "Verbosity", "model_config", "How long the model's answers run",
                       _levels("low", "medium", "high")),
            ]
        options.append(_max_tokens_option([DEFAULT, "4096", "8192", "16384", "32768"]))
        return options

    def defaults(self, model):
        values = {option["id"]: DEFAULT for option in self.config_options(model)}
        if "reasoning_summary" in values:
            values["reasoning_summary"] = "auto"
        return values

    @staticmethod
    def _input(history, model):
        items = []
        for message in history:
            if message["role"] == "assistant":
                kept = _state(message, model)
                if kept:
                    items += kept
                    continue
                if message["content"]:
                    items.append({"role": "assistant", "content": message["content"]})
                for call in message.get("tool_calls") or []:
                    items.append({"type": "function_call", "call_id": call["id"], "name": call["name"],
                                  "arguments": json.dumps(call["arguments"])})
            elif message["role"] == "tool":
                items.append({"type": "function_call_output", "call_id": message["tool_call_id"],
                              "output": message["content"]})
            else:
                items.append({"role": "user", "content": message["content"]})
        return items

    def stream(self, system, history, tools, settings, on_text, on_thought, cancel):
        model = settings["model"]
        body = {
            "model": model,
            "instructions": system,
            "input": self._input(history, model),
            "tools": [{"type": "function", "name": t["name"], "description": t["description"],
                       "parameters": t["parameters"], "strict": False} for t in tools],
            "stream": True,
            "store": False,
        }
        if _chosen(settings, "max_tokens"):
            body["max_output_tokens"] = int(settings["max_tokens"])
        if self._reasons(model):
            reasoning = {}
            if _chosen(settings, "reasoning_effort"):
                reasoning["effort"] = settings["reasoning_effort"]
            if settings.get("reasoning_summary", "auto") == "auto":
                reasoning["summary"] = "auto"
            if reasoning:
                body["reasoning"] = reasoning
            body["include"] = ["reasoning.encrypted_content"]
            if _chosen(settings, "verbosity"):
                body["text"] = {"verbosity": settings["verbosity"]}
        try:
            return self._run(body, model, on_text, on_thought, cancel)
        except ProviderError as error:
            # An organization that is not verified is refused the summary, before
            # anything streams. The reply is still wanted, so ask once without it.
            if error.status != 400 or "summary" not in body.get("reasoning", {}) or "verif" not in str(error).lower():
                raise
            sys.stderr.write("No reasoning summary: %s\n" % error)
            del body["reasoning"]["summary"]
            return self._run(body, model, on_text, on_thought, cancel)

    def _run(self, body, model, on_text, on_thought, cancel):
        reply = Reply()
        output = []
        for event in _events(self.base_url + "/responses", self.headers, body, cancel):
            kind = event.get("type")
            if kind == "response.output_text.delta":
                reply.text += event["delta"]
                on_text(event["delta"])
            elif kind == "response.reasoning_summary_text.delta":
                on_thought(event["delta"])
            elif kind == "response.reasoning_summary_part.added" and event.get("summary_index", 0) > 0:
                on_thought("\n\n")
            elif kind in ("response.completed", "response.incomplete"):
                response = event["response"]
                output = response.get("output") or []
                if (response.get("incomplete_details") or {}).get("reason") == "max_output_tokens":
                    reply.stop = "max_tokens"
            elif kind == "response.failed":
                raise ProviderError((event["response"].get("error") or {}).get("message", "OpenAI could not answer"))
            elif kind == "error":
                raise ProviderError(event.get("message", "OpenAI returned an error"))

        # What goes back next time: the reasoning and the calls as they came,
        # and the answer as plain text in its place among them.
        kept = []
        for item in output:
            if item["type"] == "function_call":
                reply.tool_calls.append({"id": item["call_id"], "name": item["name"],
                                         "arguments": _arguments(item["arguments"])})
                kept.append(item)
            elif item["type"] == "reasoning":
                kept.append(item)
            elif item["type"] == "message":
                text = "".join(part.get("text", "") for part in item["content"] if part["type"] == "output_text")
                kept.append({"role": "assistant", "content": text})
        if reply.tool_calls:
            reply.stop = "tool_use"
        reply.provider_state = {"model": model, "items": kept}
        return reply


class Anthropic:
    """The Anthropic Messages API."""

    # Tokens the model may spend thinking, by the value of the `thinking` setting.
    BUDGETS = {"4k": 4000, "16k": 16000, "32k": 32000}
    # Marks the end of a part of the request to cache.
    CACHED = {"type": "ephemeral"}

    def __init__(self, base_url, key):
        self.base_url = base_url
        self.headers = {"x-api-key": key, "anthropic-version": ANTHROPIC_VERSION}

    def models(self):
        listed = _get(self.base_url + "/v1/models?limit=1000", self.headers)["data"]
        return [(entry["id"], entry.get("display_name") or entry["id"]) for entry in listed]

    def config_options(self, model):
        return [
            select("thinking", "Thinking", "thought_level",
                   "Extended thinking, and how many tokens it may use. Adaptive lets the model decide, "
                   "on the models that support it",
                   [("off", "Off"), ("4k", "4k tokens"), ("16k", "16k tokens"), ("32k", "32k tokens"),
                    ("adaptive", "Adaptive")]),
            _max_tokens_option(["4096", "8192", "16384", "32000", "64000"]),
        ]

    def defaults(self, model):
        return {"thinking": "off", "max_tokens": "8192"}

    @staticmethod
    def _messages(history, model, thinking):
        """Tool results travel in a user message, and roles must alternate.
        The thinking of a reply goes back first in it, signature included,
        while thinking is on."""
        messages = []

        def add(role, blocks):
            if messages and messages[-1]["role"] == role:
                messages[-1]["content"].extend(blocks)
            else:
                messages.append({"role": role, "content": list(blocks)})

        for message in history:
            if message["role"] == "assistant":
                blocks = list(_state(message, model)) if thinking else []
                if message["content"]:
                    blocks.append({"type": "text", "text": message["content"]})
                for call in message.get("tool_calls") or []:
                    blocks.append({"type": "tool_use", "id": call["id"], "name": call["name"],
                                   "input": call["arguments"]})
                add("assistant", blocks)
            elif message["role"] == "tool":
                add("user", [{"type": "tool_result", "tool_use_id": message["tool_call_id"],
                              "content": message["content"], "is_error": bool(message.get("is_error"))}])
            else:
                add("user", [{"type": "text", "text": message["content"]}])
        return messages

    def stream(self, system, history, tools, settings, on_text, on_thought, cancel):
        model = settings["model"]
        thinking = settings.get("thinking", "off")
        max_tokens = int(settings.get("max_tokens", "8192"))
        # Anthropic caches a prompt only up to a marked block: the system
        # prompt, the tools, and the conversation so far. The next request
        # then reads all three from the cache. Each marked block is built
        # for this request, so the history and its kept thinking stay unmarked.
        messages = self._messages(history, model, thinking != "off")
        messages[-1]["content"][-1] = dict(messages[-1]["content"][-1], cache_control=self.CACHED)
        definitions = [{"name": t["name"], "description": t["description"], "input_schema": t["parameters"]}
                       for t in tools]
        definitions[-1]["cache_control"] = self.CACHED
        body = {
            "model": model,
            "system": [{"type": "text", "text": system, "cache_control": self.CACHED}],
            "max_tokens": max_tokens,
            "messages": messages,
            "tools": definitions,
            "stream": True,
        }
        if thinking == "adaptive":
            body["thinking"] = {"type": "adaptive"}
        elif thinking != "off":
            # The budget counts against max_tokens, so the reply keeps the room the setting gives it.
            budget = self.BUDGETS[thinking]
            body["thinking"] = {"type": "enabled", "budget_tokens": budget}
            body["max_tokens"] = max_tokens + budget

        reply = Reply()
        blocks = {}
        for event in _events(self.base_url + "/v1/messages", self.headers, body, cancel):
            kind = event.get("type")
            if kind == "error":
                raise ProviderError(event.get("error", {}).get("message", "Anthropic returned an error"))
            if kind == "content_block_start":
                block = dict(event["content_block"])
                if block["type"] == "tool_use":
                    block["json"] = ""
                blocks[event["index"]] = block
            elif kind == "content_block_delta":
                delta = event["delta"]
                block = blocks[event["index"]]
                if delta["type"] == "text_delta":
                    reply.text += delta["text"]
                    on_text(delta["text"])
                elif delta["type"] == "thinking_delta":
                    block["thinking"] += delta["thinking"]
                    on_thought(delta["thinking"])
                elif delta["type"] == "signature_delta":
                    block["signature"] = block.get("signature", "") + delta["signature"]
                elif delta["type"] == "input_json_delta":
                    block["json"] += delta["partial_json"]
            elif kind == "message_delta":
                if event.get("delta", {}).get("stop_reason") == "max_tokens":
                    reply.stop = "max_tokens"

        kept = []
        for index in sorted(blocks):
            block = blocks[index]
            if block["type"] == "tool_use":
                reply.tool_calls.append({"id": block["id"], "name": block["name"],
                                         "arguments": _arguments(block["json"])})
            elif block["type"] in ("thinking", "redacted_thinking"):
                kept.append(block)
        if reply.tool_calls:
            reply.stop = "tool_use"
        if kept:
            reply.provider_state = {"model": model, "items": kept}
        return reply
