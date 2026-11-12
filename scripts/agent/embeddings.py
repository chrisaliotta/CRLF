"""Embedding clients for the vector index.

An embedder turns texts into vectors, one per text and in the same order.
The index stores them and compares a query's vector with them.

    embedder = create(provider, model, base_url, api_key_env, api_key_file)
    embedder.provider, embedder.model    the names the index stores, to rebuild when either changes
    embedder.embed(texts)                -> [[float]]

`local` runs a small model in this process through the fastembed package:
nothing leaves the machine and no key is needed. The others POST to an
OpenAI-compatible /embeddings endpoint. Ollama's is on this machine too;
`openai` and `openai-compatible` send the project's text to that server.
"""

import sys
import threading

import providers

LOCAL = "local"
# A small general-purpose model of 384 dimensions that runs on a CPU.
DEFAULT_LOCAL_MODEL = "BAAI/bge-small-en-v1.5"

# Where each remote provider listens and which variable holds its key.
PRESETS = {
    "ollama": ("http://localhost:11434/v1", None),
    "openai": ("https://api.openai.com/v1", "OPENAI_API_KEY"),
    "openai-compatible": (None, None),
}
PROVIDERS = [LOCAL] + sorted(PRESETS)

# Texts in one request to a remote provider.
BATCH = 64


def create(provider, model=None, base_url=None, api_key_env=None, api_key_file=None):
    """Raises ProviderError when the provider is unknown, a package or key
    is missing, or a remote provider has no model named."""
    if provider == LOCAL:
        return LocalEmbedder(model or DEFAULT_LOCAL_MODEL)
    if provider not in PRESETS:
        raise providers.ProviderError("Unknown embedding provider %r. Use one of: %s"
                                      % (provider, ", ".join(PROVIDERS)))
    if not model:
        raise providers.ProviderError("--embed-model is required for --embed-provider %s" % provider)
    default_url, default_key = PRESETS[provider]
    base_url = (base_url or default_url or "").rstrip("/")
    if not base_url:
        raise providers.ProviderError("--embed-base-url is required for --embed-provider %s" % provider)
    key = providers.read_key(api_key_file, api_key_env or default_key, "--embed-api-key-file")
    return HttpEmbedder(provider, model, base_url, key)


class LocalEmbedder:
    """fastembed, on the CPU. The package is imported here and not at the
    top, so only --embed-provider local needs it."""

    provider = LOCAL

    def __init__(self, model):
        try:
            import fastembed
        except ImportError:
            raise providers.ProviderError("--embed-provider local needs the fastembed package. "
                                          "Install it with: %s -m pip install fastembed" % sys.executable)
        self.model = model
        self._fastembed = fastembed
        self._loaded = None
        # The index embeds on its refresh thread while a search embeds its query.
        self._lock = threading.Lock()

    def embed(self, texts):
        with self._lock:
            if self._loaded is None:
                # Loading is left until now, because the first load downloads the model.
                sys.stderr.write("Loading the embedding model %s. The first use downloads it.\n" % self.model)
                self._loaded = self._fastembed.TextEmbedding(model_name=self.model)
            return [vector.tolist() for vector in self._loaded.embed(texts)]


class HttpEmbedder:
    """An OpenAI-compatible /embeddings endpoint."""

    def __init__(self, provider, model, base_url, key):
        self.provider = provider
        self.model = model
        self.url = base_url + "/embeddings"
        self.headers = {"Authorization": "Bearer " + key} if key else {}

    def embed(self, texts):
        vectors = []
        for start in range(0, len(texts), BATCH):
            reply = providers.post(self.url, self.headers, {"model": self.model, "input": texts[start:start + BATCH]})
            # The reply numbers its vectors; the order is not promised.
            vectors += [entry["embedding"] for entry in sorted(reply["data"], key=lambda entry: entry["index"])]
        return vectors
