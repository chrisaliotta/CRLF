"""Model providers for the reference agent.

Two wire formats cover every target: the OpenAI-compatible Chat Completions
API (OpenAI, xAI Grok, Ollama, LM Studio, llama.cpp, vLLM) and the Anthropic
Messages API. Both stream, and both are reduced to one Reply: the text, the
tool calls, and why the model stopped. The conversation is kept in one
neutral form and converted for each request:

    {"role": "user", "content": str}
    {"role": "assistant", "content": str, "tool_calls": [{"id", "name", "arguments"}]}
    {"role": "tool", "tool_call_id": str, "name": str, "content": str, "is_error": bool}
"""

import json
import os
import urllib.error
import urllib.request

# Where each provider listens and which variable holds its key. A local
# server needs no key.
PRESETS = {
    "openai": ("openai", "https://api.openai.com/v1", "OPENAI_API_KEY"),
    "xai": ("openai", "https://api.x.ai/v1", "XAI_API_KEY"),
    "ollama": ("openai", "http://localhost:11434/v1", None),
    "lmstudio": ("openai", "http://localhost:1234/v1", None),
    "openai-compatible": ("openai", None, None),
    "anthropic": ("anthropic", "https://api.anthropic.com", "ANTHROPIC_API_KEY"),
}

TIMEOUT_SECONDS = 600


class ProviderError(Exception):
    pass


class Cancelled(Exception):
    pass


class Reply:
    def __init__(self):
        self.text = ""
        self.tool_calls = []
        # "end", "tool_use", or "max_tokens".
        self.stop = "end"


def create(name, model, base_url=None, api_key_env=None, max_tokens=None):
    """Raises ProviderError when the provider is unknown or its key is missing."""
    if name not in PRESETS:
        raise ProviderError("Unknown provider %r. Use one of: %s" % (name, ", ".join(sorted(PRESETS))))
    wire, default_url, default_key = PRESETS[name]
    base_url = (base_url or default_url or "").rstrip("/")
    if not base_url:
        raise ProviderError("--base-url is required for %s" % name)
    key_name = api_key_env or default_key
    key = os.environ.get(key_name, "") if key_name else ""
    if key_name and not key:
        raise ProviderError("%s is not set. Set it in the environment CRLF starts from." % key_name)
    if wire == "anthropic":
        return Anthropic(model, base_url, key, max_tokens or 8192)
    return OpenAICompatible(model, base_url, key, max_tokens, name == "openai")


def _post(url, headers, body, cancel):
    """Yields the server-sent event lines of a streamed POST."""
    request = urllib.request.Request(url, data=json.dumps(body).encode("utf-8"), headers=headers, method="POST")
    try:
        response = urllib.request.urlopen(request, timeout=TIMEOUT_SECONDS)
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", "replace")
        try:
            detail = json.loads(detail).get("error", {}).get("message", detail)
        except (ValueError, AttributeError):
            pass
        raise ProviderError("%s returned %d: %s" % (url, error.code, detail))
    except urllib.error.URLError as error:
        raise ProviderError("Could not reach %s: %s" % (url, error.reason))
    with response:
        for raw in response:
            if cancel.is_set():
                raise Cancelled()
            line = raw.decode("utf-8", "replace").rstrip("\r\n")
            if line:
                yield line


def _arguments(text):
    try:
        value = json.loads(text) if text.strip() else {}
        return value if isinstance(value, dict) else {"_invalid": text}
    except ValueError:
        return {"_invalid": text}


class OpenAICompatible:
    def __init__(self, model, base_url, key, max_tokens, openai):
        self.model = model
        self.url = base_url + "/chat/completions"
        self.key = key
        self.max_tokens = max_tokens
        # OpenAI's reasoning models take max_completion_tokens; the others max_tokens.
        self.token_field = "max_completion_tokens" if openai else "max_tokens"

    def label(self):
        return self.model

    def _messages(self, system, history):
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

    def stream(self, system, history, tools, on_text, on_thought, cancel):
        body = {
            "model": self.model,
            "messages": self._messages(system, history),
            "tools": [{"type": "function", "function": {"name": t["name"], "description": t["description"],
                                                         "parameters": t["parameters"]}} for t in tools],
            "stream": True,
        }
        if self.max_tokens:
            body[self.token_field] = self.max_tokens
        headers = {"Content-Type": "application/json"}
        if self.key:
            headers["Authorization"] = "Bearer " + self.key

        reply = Reply()
        calls = {}
        for line in _post(self.url, headers, body, cancel):
            if not line.startswith("data:"):
                continue
            data = line[5:].strip()
            if data == "[DONE]":
                break
            chunk = json.loads(data)
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
                reason = choice.get("finish_reason")
                if reason == "length":
                    reply.stop = "max_tokens"
        for index in sorted(calls):
            call = calls[index]
            reply.tool_calls.append({"id": call["id"] or "call_%d" % index, "name": call["name"],
                                     "arguments": _arguments(call["arguments"])})
        if reply.tool_calls:
            reply.stop = "tool_use"
        return reply


class Anthropic:
    def __init__(self, model, base_url, key, max_tokens):
        self.model = model
        self.url = base_url + "/v1/messages"
        self.key = key
        self.max_tokens = max_tokens

    def label(self):
        return self.model

    @staticmethod
    def _messages(history):
        """Tool results travel in a user message, and roles must alternate."""
        messages = []

        def add(role, blocks):
            if messages and messages[-1]["role"] == role:
                messages[-1]["content"].extend(blocks)
            else:
                messages.append({"role": role, "content": list(blocks)})

        for message in history:
            if message["role"] == "assistant":
                blocks = [{"type": "text", "text": message["content"]}] if message["content"] else []
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

    def stream(self, system, history, tools, on_text, on_thought, cancel):
        body = {
            "model": self.model,
            "system": system,
            "max_tokens": self.max_tokens,
            "messages": self._messages(history),
            "tools": [{"name": t["name"], "description": t["description"], "input_schema": t["parameters"]}
                      for t in tools],
            "stream": True,
        }
        headers = {"Content-Type": "application/json", "x-api-key": self.key, "anthropic-version": "2023-06-01"}

        reply = Reply()
        blocks = {}
        for line in _post(self.url, headers, body, cancel):
            if not line.startswith("data:"):
                continue
            event = json.loads(line[5:].strip())
            kind = event.get("type")
            if kind == "error":
                raise ProviderError(event.get("error", {}).get("message", "Anthropic returned an error"))
            if kind == "content_block_start":
                block = event["content_block"]
                blocks[event["index"]] = {"type": block["type"], "id": block.get("id", ""),
                                          "name": block.get("name", ""), "json": ""}
            elif kind == "content_block_delta":
                delta = event["delta"]
                if delta["type"] == "text_delta":
                    reply.text += delta["text"]
                    on_text(delta["text"])
                elif delta["type"] == "thinking_delta":
                    on_thought(delta["thinking"])
                elif delta["type"] == "input_json_delta":
                    blocks[event["index"]]["json"] += delta["partial_json"]
            elif kind == "message_delta":
                reason = event.get("delta", {}).get("stop_reason")
                if reason == "max_tokens":
                    reply.stop = "max_tokens"
        for index in sorted(blocks):
            block = blocks[index]
            if block["type"] == "tool_use":
                reply.tool_calls.append({"id": block["id"], "name": block["name"],
                                         "arguments": _arguments(block["json"])})
        if reply.tool_calls:
            reply.stop = "tool_use"
        return reply
