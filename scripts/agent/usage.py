"""What a session has used: tokens, and what they cost.

Every provider reports the tokens of each reply, and providers.py gives
them one shape. None reports a price or the size of a model's context
window, so both come from two lists of models:

    models.json, beside this file    shipped with the agent and replaced by
                                     each upgrade: the models the providers
                                     publish, checked against their pages
    the --prices file                the user's own, read over the shipped
                                     list: a field it sets wins, a field it
                                     leaves out is the shipped one, and a
                                     model only it lists is added

Both have one shape:

    {
      "currency": "USD",
      "models": {
        "gpt-6-astra": {
          "input": 10.0, "cached_input": 1.0, "cache_write": 12.5, "output": 50.0,
          "context": 1050000, "max_input": 922000,
          "long": {"over": 272000, "input": 20.0, "cached_input": 2.0, "cache_write": 25.0, "output": 75.0}
        }
      }
    }

Prices are for a million tokens. `cached_input` is the price of input read
from the provider's cache and `cache_write` of input written to it; each is
the `input` price when left out. `context` is the model's window in tokens
and `max_input` the most input it accepts, which is less: the window also
holds the reply. `long` is for a provider that charges more for a long
prompt: a request whose input is over `over` tokens is priced, all of it,
at the prices `long` gives, and a price `long` leaves out stays as it was.
Every field is optional: an entry with only `context` gives a percentage
and no cost.

    prices = load(path)              -> Prices
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
SHIPPED = os.path.join(os.path.dirname(os.path.abspath(__file__)), "models.json")
PRICE_FIELDS = ("input", "cached_input", "cache_write", "output", "long")


class Prices:
    def __init__(self, currency, models):
        self.currency = currency
        self.models = models

    def entry(self, model):
        """The entry named exactly, or else the longest listed name the
        model starts with, so a dated snapshot such as gpt-5-2025-08-07
        takes the entry of gpt-5."""
        if model in self.models:
            return self.models[model]
        listed = [name for name in self.models if model.startswith(name)]
        return self.models[max(listed, key=len)] if listed else None


def _read(path):
    """Raises ProviderError for a file that cannot be read as a list of models."""
    try:
        with open(path, encoding="utf-8") as handle:
            listed = json.load(handle)
        return listed.get("currency", "USD"), dict(listed.get("models") or {})
    except (OSError, ValueError, AttributeError) as error:
        raise providers.ProviderError("Could not read the prices file %s: %s" % (os.path.abspath(path), error))


def load(path, shipped=SHIPPED):
    """The shipped list with the user's file at `path` over it. A missing
    `path` or file leaves the shipped list as it is.

    The shipped prices are in the shipped list's currency. A user's file in
    another currency keeps the shipped sizes and drops the shipped prices,
    so no cost mixes two currencies."""
    currency, models = _read(shipped)
    if not path or not os.path.isfile(path):
        return Prices(currency, models)
    own_currency, own = _read(path)
    if own_currency != currency:
        models = {name: {field: value for field, value in entry.items() if field not in PRICE_FIELDS}
                  for name, entry in models.items()}
    for name, entry in own.items():
        models[name] = dict(models.get(name, {}), **entry)
    return Prices(own_currency, models)


def priced(entry):
    return entry is not None and "input" in entry and "output" in entry


def cost(usage, entry):
    """What one reply cost, in the prices' currency."""
    long = entry.get("long")
    rates = dict(entry, **long) if long and usage["input"] > long["over"] else entry
    fresh = usage["input"] - usage["cached"] - usage["cache_write"]
    return (fresh * rates["input"]
            + usage["cached"] * rates.get("cached_input", rates["input"])
            + usage["cache_write"] * rates.get("cache_write", rates["input"])
            + usage["output"] * rates["output"]) / PER


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
