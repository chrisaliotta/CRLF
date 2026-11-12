"""The size of a conversation, and the budget that keeps it down.

Every model call sends the whole conversation again, and old tool results
are most of it. The Context option sets a budget in tokens. When the
conversation passes it, the oldest tool results are replaced with a line
that names the call, until the conversation is at 70 percent of the
budget. The messages stay, so each tool call keeps its result and the
thinking a provider wants handed back stays valid.

    option = config_option()         -> ACP select option, without currentValue
    estimate(history)                -> tokens
    shorten(history, budget)         -> how many tool results were shortened

`history` is the neutral form providers.py documents. `budget` is a value
of the option: UNLIMITED, or a count of tokens as a string.

Shortening happens in steps, not on every call: nothing changes until the
budget is passed again. A provider's prompt cache holds the start of the
conversation, and it stays good for as long as that start is not rewritten.
"""

import json

import providers

OPTION_ID = "context_budget"
UNLIMITED = "unlimited"
BUDGETS = [(UNLIMITED, "Unlimited"), ("50000", "50k tokens"), ("100000", "100k tokens"), ("200000", "200k tokens")]

# Shortening stops at this share of the budget, which leaves room before the next time.
TARGET = 0.7
# There is no tokenizer here. Four characters to a token is close enough for a budget.
CHARACTERS_PER_TOKEN = 4
# Kept of a call's arguments in the line that replaces its result.
MAX_NOTE_ARGUMENTS = 200


def config_option():
    """A new object each time, because the session adds the current value to it."""
    return providers.select(OPTION_ID, "Context", "model_config",
                            "How large the conversation may grow, in tokens, before old tool results are shortened",
                            BUDGETS)


def _size(message):
    return len(json.dumps(message, ensure_ascii=False))


def estimate(history):
    return sum(_size(message) for message in history) // CHARACTERS_PER_TOKEN


def _note(call):
    arguments = json.dumps(call["arguments"], ensure_ascii=False)
    if len(arguments) > MAX_NOTE_ARGUMENTS:
        arguments = arguments[:MAX_NOTE_ARGUMENTS] + "..."
    return "[The result of %s(%s) was dropped to save context. Call it again if you need it.]" % (call["name"],
                                                                                                arguments)


def shorten(history, budget):
    """Changes only the `content` of tool results, oldest first, so the
    newest turn's results are the last to go."""
    if budget == UNLIMITED:
        return 0
    size = sum(_size(message) for message in history)
    if size <= int(budget) * CHARACTERS_PER_TOKEN:
        return 0
    target = int(budget) * CHARACTERS_PER_TOKEN * TARGET
    calls = {call["id"]: call for message in history for call in message.get("tool_calls") or []}
    shortened = 0
    for message in history:
        if size <= target:
            break
        if message["role"] != "tool":
            continue
        note = _note(calls[message["tool_call_id"]])
        # A result no longer than its note, which one shortened earlier is, has nothing to give.
        if len(message["content"]) <= len(note):
            continue
        before = _size(message)
        message["content"] = note
        size -= before - _size(message)
        shortened += 1
    return shortened
