"""What a session has used: tokens, and what they cost.

Every provider reports the tokens of each reply, and providers.py gives
them one shape. None reports a price or the size of a model's context
window, so both come from a prices file the user keeps:

    {
      "currency": "USD",
      "models": {
        "gpt-5": {"input": 1.25, "cached_input": 0.125, "output": 10.0, "context": 400000}
      }
    }

Prices are for a million tokens. `cached_input` is the price of input read
from the provider's cache and `cache_write` of input written to it; each is
the `input` price when left out. `context` is the model's window in tokens.
Every field is optional: an entry with only `context` gives a percentage
and no cost.

    prices = load(path)              -> Prices; a missing path or file prices nothing
    prices.entry(model)              -> the model's entry, or None
    totals = Totals()
    totals.add(usage, entry)         -> one reply's tokens, and its cost when the entry prices it

The cost is an estimate. It is the session's tokens at the listed prices,
which a provider's own bill can differ from.
"""

import json
import os

import providers

PER = 1000000


class Prices:
    def __init__(self, currency, models):
        self.currency = currency
        self.models = models

    def entry(self, model):
        """The entry named exactly, or else the longest listed name the
        model starts with, so a dated snapshot uses its model's entry."""
        if model in self.models:
            return self.models[model]
        named = [name for name in self.models if model.startswith(name)]
        return self.models[max(named, key=len)] if named else None


def load(path):
    """Raises ProviderError for a file that is there and cannot be read as prices."""
    if not path or not os.path.isfile(path):
        return Prices("USD", {})
    try:
        with open(path, encoding="utf-8") as handle:
            listed = json.load(handle)
        return Prices(listed.get("currency", "USD"), dict(listed.get("models") or {}))
    except (OSError, ValueError, AttributeError) as error:
        raise providers.ProviderError("Could not read the prices file %s: %s" % (os.path.abspath(path), error))


def priced(entry):
    return entry is not None and "input" in entry and "output" in entry


def cost(usage, entry):
    """What one reply cost, in the prices file's currency."""
    fresh = usage["input"] - usage["cached"] - usage["cache_write"]
    return (fresh * entry["input"]
            + usage["cached"] * entry.get("cached_input", entry["input"])
            + usage["cache_write"] * entry.get("cache_write", entry["input"])
            + usage["output"] * entry["output"]) / PER


class Totals:
    """A session's running counts. `input` includes the cached tokens."""

    def __init__(self):
        self.input = 0
        self.cached = 0
        self.output = 0
        self.cost = 0.0

    def add(self, usage, entry):
        self.input += usage["input"]
        self.cached += usage["cached"]
        self.output += usage["output"]
        if priced(entry):
            self.cost += cost(usage, entry)
