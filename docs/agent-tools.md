# Agent tools and the code index

This guide is for someone setting up the reference agent, `scripts/agent/crlf_agent.py`. It covers the tools the agent gives its model, the code index behind `codebase_search`, the embedding choices for vector search, project rules, and the settings that keep a session fast and cheap. Each part has an example to copy.

The Agent panel, permissions, and the protocol are in [agent.md](agent.md). The `agents` key is in [crlf.md](crlf.md#agents).

Model names in the examples, for chat and for embeddings, are placeholders. Replace them with names your provider offers.

## The flags

An agent is one entry under `agents` in a `.crlf` file. Everything in this guide is a flag on that entry's command line.

```json
"agents": [
  {
    "name": "Claude",
    "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider anthropic --model claude-sonnet-4-5 --api-key-file .crlf/keys/anthropic --index fts"
  }
]
```

| Flag | Values | Default | What it sets |
| --- | --- | --- | --- |
| `--index` | `grep`, `fts`, `vector` | `fts` | What `codebase_search` asks. See [Choosing an index](#choosing-an-index) |
| `--index-dir` | A folder, relative to the session's working directory, or absolute | `.crlf/data` | Where the index is kept |
| `--embed-provider` | `local`, `ollama`, `openai`, `openai-compatible` | none | Where the vector index gets its embeddings. Required with `--index vector` |
| `--embed-model` | A model name | `BAAI/bge-small-en-v1.5` for `local`; required for the others | The embedding model |
| `--embed-base-url` | A server address | The provider's own; required for `openai-compatible` | The embedding server |
| `--embed-api-key-file` | A file, relative to the agent's working directory | none | The embedding server's key. It wins over the environment |
| `--embed-api-key-env` | A variable name | `OPENAI_API_KEY` for `openai` | The variable that holds the embedding server's key |
| `--rules` | A folder, relative to the session's working directory, or absolute. May be given more than once | `.crlf/rules` | The rule folders. Giving the flag replaces the default. See [Rules](#rules) |
| `--context-budget` | `unlimited`, or a count of tokens | `unlimited` | Where the Context option starts. See [Keeping it fast and cheap](#keeping-it-fast-and-cheap) |
| `--prices` | A JSON file, relative to the agent's working directory, or absolute | none | Your own prices and context sizes by model, read over the shipped `models.json`, for the panel's usage line. See [Usage and prices](#usage-and-prices) |
| `--context-window` | A count of tokens | none | The model's context size, for a model no list has. It comes before a listed size |

The chat flags (`--provider`, `--model`, `--api-key-file`, `--max-tokens`, `--reasoning-effort`, `--thinking`) are in [agent.md](agent.md#the-reference-agent). The chat provider and the embedding provider are independent: Claude can answer while a local model embeds.

`--index vector` without `--embed-provider` makes the agent exit as it starts. The message, `--index vector needs --embed-provider`, is in the agent's log on the Processes page.

## Choosing an index

`codebase_search` finds code from a description, such as "where the bulk discount is applied". The index is what it searches. A file is cut into passages of about 60 lines, called chunks, and a search returns the best 8 chunks.

| `--index` | Finds | Costs | Stored on disk |
| --- | --- | --- | --- |
| `grep` | Chunks that contain the query's words as written. A chunk with more of the words ranks higher | Nothing to build. Every search reads every file in the project | Nothing |
| `fts` | Chunks that share words with the query, ranked by BM25, a standard relevance score that favors rare words. Words are stemmed, which means reduced to their root, so `parsing` finds `parse` | Each file is read once to build it, and after that only the files that changed. No model, no package, no network | `.crlf/data/index.sqlite` |
| `vector` | Chunks whose meaning is near the query's, even when no word is shared. Each chunk is turned into a list of numbers, called an embedding, by an embedding model | The first build embeds every chunk. A remote provider charges for it and receives the code. Each search embeds the query | `.crlf/data/index.sqlite` |

Start with `fts`. Move to `vector` when the model keeps searching for ideas that the code names differently, such as "retry logic" in a project that calls it `backoff`.

### grep

```json
{
  "name": "Claude (grep)",
  "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider anthropic --model claude-sonnet-4-5 --api-key-file .crlf/keys/anthropic --index grep"
}
```

Nothing is written to the project. Use it for a small project, or when the project must not gain a `.crlf/data` folder.

### fts

```json
{
  "name": "Claude",
  "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider anthropic --model claude-sonnet-4-5 --api-key-file .crlf/keys/anthropic --index fts"
}
```

`fts` is the default, so leaving `--index` out does the same. It uses FTS5, the full-text search built into SQLite. When the Python in use has an SQLite without FTS5, the agent logs `This Python's SQLite has no FTS5, so the index is grep.` and runs as `--index grep`.

### vector

```json
{
  "name": "Claude (vector)",
  "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider anthropic --model claude-sonnet-4-5 --api-key-file .crlf/keys/anthropic --index vector --embed-provider local"
}
```

`--embed-provider` is required. The choices and their install steps are in [Embedding choices for vector search](#embedding-choices-for-vector-search).

### What appears under .crlf/data

The folder is created in the session's working directory, which is the project root unless the entry sets `cwd`, the first time an `fts` or `vector` session starts.

```
.crlf/
  data/
    .gitignore
    index.sqlite
    index.sqlite-shm
    index.sqlite-wal
```

- `.gitignore` contains `*`, so git ignores the whole folder. Nothing in it needs to be committed.
- `index.sqlite` is the index. The `-shm` and `-wal` files are SQLite's own working files and exist while an agent has the index open.
- The editor has no rule of its own that hides the folder. `.crlf` is a dot folder, so the explorer hides it while Hide Dot Folders is on, and the definition scan and the repository search skip dot folders. See [crlf.md](crlf.md#hidden-names).
- The folder cannot be created while a file named `.crlf`, from an earlier version, has the name. The editor renames that file when it loads the project, before any agent starts. See [crlf.md](crlf.md#the-legacy-crlf-file).

`--index-dir` moves the folder. The path is relative to the session's working directory, or absolute.

```json
{
  "name": "Claude",
  "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider anthropic --model claude-sonnet-4-5 --api-key-file .crlf/keys/anthropic --index fts --index-dir build/agent-index"
}
```

A folder outside `.crlf` is shown in the explorer unless its own name starts with `.`. Add its name to `hide` if it should not be.

The tables in `index.sqlite`:

| Table | Holds | Used by |
| --- | --- | --- |
| `files` | Each indexed file's path, size, and modified time | `fts`, `vector` |
| `chunks` | Each chunk's path, first and last line, text, and the SHA-256 hash of its text | `fts`, `vector` |
| `fts` | The FTS5 full-text table over the chunks' text | `fts` |
| `vectors` | One embedding for each distinct chunk text, stored under that text's hash | `vector` |
| `meta` | The backend that used the file last, and the embedding provider, model, and dimensions | `fts`, `vector` |

### How the index is built and kept current

- **Which files.** In a git repository, the files git tracks or would add, so `.gitignore` is respected. Elsewhere, every file except those in folders whose names start with `.`, `node_modules`, and `__pycache__`. Binary files and files over 1 MB are skipped.
- **Chunks.** About 60 lines each. A chunk ends at a blank line when one is within 10 lines of the boundary, and each chunk repeats the last 10 lines of the one before it.
- **Refresh.** The index is brought up to date on a background thread when the session starts, and again before each `codebase_search`. A refresh compares each file's size and modified time with what the index has and reads only the files that changed. Rows for deleted files are removed.
- **Searches do not wait.** A search made while a refresh runs sees the index as it was. Until the first refresh of a session finishes, `codebase_search` matches words the way `--index grep` does, and its result starts with a note that says so. See [codebase_search](#codebase_search).
- **Progress.** While files are read, the agent's Activity on the Processes page counts them, for example `indexing 120/480`. A vector index then counts again while it embeds. The Activity returns to `ready` or `working` when the refresh ends.
- **Failures.** A refresh that fails, such as one whose embedding server is not running, writes `The index could not be refreshed:` and the reason to the agent's log. The session continues.

In the sample project used for the tool examples below, the first refresh reported `indexing 1/11` through `indexing 11/11`. After the agent changed three files, the next refresh reported `indexing 1/3` through `indexing 3/3`.

## Embedding choices for vector search

An embedding model is separate from the chat model. It only turns text into numbers. There are three ways to run one.

| `--embed-provider` | Runs | Key | The project's code |
| --- | --- | --- | --- |
| `local` | In the agent's own process, on the CPU, through the `fastembed` Python package | None | Stays on the machine |
| `ollama` | In Ollama on this machine, at `http://localhost:11434/v1` | None | Stays on the machine |
| `openai` | At `https://api.openai.com/v1` | `OPENAI_API_KEY`, or `--embed-api-key-file` | Is sent to OpenAI |
| `openai-compatible` | At the server `--embed-base-url` names | `--embed-api-key-file` or `--embed-api-key-env`, when the server wants one | Is sent to that server |

With a remote provider, the text of every indexed file is sent to that provider once, and the text of every `codebase_search` query is sent each time. Use `local` or `ollama` when the code must stay on the machine.

**Text is never embedded twice.** An embedding is stored under the hash of its chunk's text, not under its file.

- An edit embeds only the chunks whose text changed.
- The same text in two files is embedded once.
- Embeddings that no chunk uses are kept, up to the number in use. Switching to another branch and back embeds nothing.
- A refresh that is stopped keeps what it finished. Chunks are embedded and saved 64 at a time.

**What has been run.** The `ollama`, `openai`, and `openai-compatible` paths share one client. The automated tests run it, as `openai-compatible`, against a local stand-in server. No request has been made to a live embedding provider. The `local` path has not been run at all; see the note below.

### Local, with fastembed

`fastembed` is an open-source Python package that runs small embedding models on the CPU through `onnxruntime`. The default model is `BAAI/bge-small-en-v1.5`, which produces 384 numbers for each text.

**This path has not been run.** On the development machine, `pip install fastembed` fails because `onnxruntime` has no build for that Python version (3.15). `fastembed` needs a Python version that `onnxruntime` supports. Check the `onnxruntime` page on PyPI for the versions it lists, and point the agent's command at such a Python.

Install on Windows, in PowerShell. Use the same `python` the agent's command runs.

```powershell
python --version
python -m pip install fastembed
python -c "import fastembed"
```

The last line prints nothing when the package is installed.

Install inside a WSL distro, in bash. An agent whose project is inside a distro runs in that distro, so the package has to be installed there.

```bash
python3 --version
python3 -m pip install fastembed
python3 -c "import fastembed"
```

Some distros refuse `pip install` into the system Python. In that case create a virtual environment and name its Python in the agent's command.

```bash
python3 -m venv ~/.venvs/crlf-agent
~/.venvs/crlf-agent/bin/python -m pip install fastembed
```

The entry:

```json
{
  "name": "Claude (local vectors)",
  "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider anthropic --model claude-sonnet-4-5 --api-key-file .crlf/keys/anthropic --index vector --embed-provider local"
}
```

Another model that `fastembed` supports is named with `--embed-model`:

```json
{
  "name": "Claude (local vectors)",
  "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider anthropic --model claude-sonnet-4-5 --api-key-file .crlf/keys/anthropic --index vector --embed-provider local --embed-model BAAI/bge-base-en-v1.5"
}
```

- **A missing package.** The session does not start. The panel shows the error, which names the command to run: `--embed-provider local needs the fastembed package. Install it with: <python> -m pip install fastembed`, where `<python>` is the Python the agent runs under.
- **The first run downloads the model.** The model is loaded when the first chunks are embedded, not when the session starts. The agent's log then says `Loading the embedding model BAAI/bge-small-en-v1.5. The first use downloads it.` `fastembed` keeps the download, so later sessions load it from disk. Until the first refresh finishes, `codebase_search` returns word matches.

### Local, with Ollama

Ollama is a program that runs models on this machine and answers on `http://localhost:11434`. Install it from [ollama.com](https://ollama.com), then pull an embedding model. The command is the same in PowerShell and in bash.

```
ollama pull nomic-embed-text
ollama list
```

The entry names the model that was pulled:

```json
{
  "name": "Local",
  "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider ollama --model qwen3-coder --index vector --embed-provider ollama --embed-model nomic-embed-text"
}
```

This entry uses Ollama for the chat model too, so nothing leaves the machine. `--embed-provider ollama` works with any chat provider.

`--embed-base-url` names an Ollama on another port or machine. The address ends in `/v1`:

```json
{
  "name": "Local",
  "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider ollama --model qwen3-coder --index vector --embed-provider ollama --embed-model nomic-embed-text --embed-base-url http://localhost:11500/v1"
}
```

The session starts whether or not Ollama is running. When it is not, the refresh fails, the log says `The index could not be refreshed: Could not reach http://localhost:11434/v1/embeddings:` and the reason, and `codebase_search` returns word matches.

### Remote, with OpenAI or an OpenAI-compatible server

**The project's code is sent to the provider.** Every chunk of every indexed file is sent once to be embedded, and every search query is sent each time.

OpenAI, with the key in a file:

```json
{
  "name": "GPT (vectors)",
  "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider openai --model gpt-5 --api-key-file .crlf/keys/openai --index vector --embed-provider openai --embed-model text-embedding-3-small --embed-api-key-file .crlf/keys/openai"
}
```

The chat key and the embedding key are read separately, so `--embed-api-key-file` is named even when it is the same file. Without it, the key comes from `OPENAI_API_KEY`, or from the variable `--embed-api-key-env` names. A missing key fails the session with `OPENAI_API_KEY is not set. Set it in the environment CRLF starts from, or pass --embed-api-key-file.` Keep key files out of git; see [Keys](agent.md#keys).

Another server that answers `POST <base>/embeddings` in OpenAI's format:

```json
{
  "name": "Claude (team vectors)",
  "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider anthropic --model claude-sonnet-4-5 --api-key-file .crlf/keys/anthropic --index vector --embed-provider openai-compatible --embed-base-url https://embeddings.example.com/v1 --embed-model example-embedding-model --embed-api-key-file .crlf/keys/embeddings"
}
```

`--embed-base-url` and `--embed-model` are both required for `openai-compatible`. Leave the key flags out for a server that wants no key. Requests carry 64 texts each.

## Optional speed-ups

The agent needs nothing beyond Python's standard library for `--index grep` and `--index fts`. Three optional installs make parts of it faster.

| Install | Speeds up | Without it |
| --- | --- | --- |
| `ripgrep` (the `rg` program) | `grep_search` | A search written in Python reads each file |
| `sqlite-vec` (a Python package) | Comparing the query with the stored embeddings, inside SQLite | `numpy` is used |
| `numpy` (a Python package) | The same comparison, on an array held in memory | Plain Python compares one embedding at a time |

`sqlite-vec` is chosen first, then `numpy`, then plain Python. `sqlite-vec` also needs a Python whose SQLite can load extensions; when it cannot, the next choice is used. The two packages matter only for `--index vector`.

Windows, in PowerShell:

```powershell
winget install BurntSushi.ripgrep.MSVC
python -m pip install sqlite-vec numpy
```

Inside a WSL distro, in bash. The first line is for Debian and Ubuntu.

```bash
sudo apt install ripgrep
python3 -m pip install sqlite-vec numpy
```

The two packages are looked for when a session starts, so press New afterwards. `rg` is looked for on each `grep_search`, on the `PATH` of the environment CRLF was started from, so restart CRLF after installing it.

`ripgrep` changes two things about `grep_search` besides speed:

- The pattern is in Rust regular expression syntax. Without `rg` it is in Python's. The common parts are the same. Look-behind and backreferences work only in Python's.
- `rg` skips hidden files and folders, those whose names start with `.`, and reads `.gitignore` itself. The Python search uses the same file list as the index.

## The tools

The model chooses the tools. You ask in plain words, and each call appears in the transcript with its state. The examples below show a request, the call the model makes, and the text the model gets back.

| Tool | Does | Result limit |
| --- | --- | --- |
| [`codebase_search`](#codebase_search) | Finds code by what it does | 8 passages, 6,000 characters |
| [`grep_search`](#grep_search) | Finds lines that match a regular expression | 50 lines, each cut at 200 characters |
| [`file_search`](#file_search) | Finds files by name | 20 paths |
| [`list_dir`](#list_dir) | Lists a folder as a tree | 4,000 characters |
| [`read_file`](#read_file) | Reads a file, part of one, or its outline | 400 lines unless `limit` says otherwise; 30,000 characters |
| [`edit_file`](#edit_file) | Replaces exact text, in one place or several | One line |
| `write_file` | Creates or replaces a whole file | One line |
| [`delete_file`](#delete_file) | Deletes a file. CRLF only | One line |
| [`run_terminal_cmd`](#run_terminal_cmd) | Runs a command and waits, or starts it in the background | The last 8,000 characters of output |
| [`read_command_output`](#read_command_output-and-stop_command) | Reads a background command's output so far | The last 8,000 characters |
| [`stop_command`](#read_command_output-and-stop_command) | Stops a background command | The last 8,000 characters |
| [`fetch_rules`](#fetch_rules) | Lists the project's rules, or reads one. Offered when a rules folder exists | 8,000 characters |
| [`diff_history`](#diff_history) | Lists what the session wrote and what git sees | 4,000 characters |
| `editor_state`, `problems`, `git_status`, `read_terminal` | Return what the editor shows. CRLF only. See [agent.md](agent.md#what-the-editor-answers) | 4,000 to 8,000 characters |

A result over its limit is cut, and the cut is marked `[cut: 1234 more characters]` so the model knows to narrow the request. The transcript shows the first 2,000 characters of each result.

The results below are what the tool functions returned for these calls, run against a small sample project with a stand-in answering for the editor. Three things were changed afterwards: the project's folder is written as `C:\work\inventory`, the background command's id as `t1`, and the errors for a rejected or denied request quote the editor's source, because the stand-in shows no cards. The sample project is a git repository:

```
.crlf/
  rules/
    style.md
    testing.md
.gitignore
README.md
scratch/
  notes.txt
src/
  inventory/
    __init__.py
    pricing.py
    stock.py
    storage.py
tests/
  test_pricing.py
  test_stock.py
```

Paths in arguments are relative to the session's working directory, or absolute. Paths in results are relative to it, with forward slashes.

### codebase_search

Request: "Where do we work out which items need reordering?"

```json
{ "name": "codebase_search", "arguments": { "query": "which items need reordering" } }
```

Result, with `--index fts`:

```
src/inventory/stock.py:1-22
"""The items a warehouse holds and how many of each."""


class OutOfStock(Exception):
    pass


class Stock:
    def __init__(self):
        self.counts = {}

    def add(self, item, quantity):
        self.counts[item] = self.counts.get(item, 0) + quantity

    def remove(self, item, quantity):
        if self.counts.get(item, 0) < quantity:
            raise OutOfStock(item)
        self.counts[item] -= quantity

    def low(self, threshold=5):
        """The items that need reordering."""
        return sorted(item for item, count in self.counts.items() if count < threshold)
```

Each passage is a heading of `path:first-last` followed by the chunk's text. Up to 8 passages come back, best first, separated by a blank line. The passages share the 6,000 characters equally, so a long one is cut and the last one is not lost. Each passage's file is a link in the transcript.

A search made before the session's first refresh has finished starts with a note. The passages are then word matches:

```
[The index is still being built, so these are word matches. Search again later for better ones.]

src/inventory/pricing.py:1-15
"""What an order costs."""

BULK_QUANTITY = 100
...
```

A search that matches nothing returns `Nothing found.`

How each index reads the query:

- `grep` and `fts` use the query's words of two letters or more. A chunk needs only one of them to match. `fts` ranks rare words above common ones; `grep` ranks by how many of the words a chunk has.
- `vector` embeds the whole query and ignores no word. It returns the 8 nearest chunks whatever the query is, so it does not return `Nothing found.` for a project that has code.

### grep_search

Request: "Which methods does the project define?"

```json
{ "name": "grep_search", "arguments": { "pattern": "def \\w+\\(self", "glob": "*.py" } }
```

Result:

```
tests/test_stock.py:10: def test_remove_more_than_held(self):
tests/test_stock.py:16: def test_low(self):
tests/test_pricing.py:10: def test_small_order_pays_full_price(self):
tests/test_pricing.py:13: def test_bulk_order(self):
src/inventory/stock.py:9: def __init__(self):
src/inventory/stock.py:12: def add(self, item, quantity):
src/inventory/stock.py:15: def remove(self, item, quantity):
src/inventory/stock.py:20: def low(self, threshold=5):
```

Each line is `path:line: text`, with the text's leading whitespace removed and at most 200 characters kept. This run used `rg`, which does not sort files; the Python search lists them in path order.

| Argument | Meaning |
| --- | --- |
| `pattern` | Required. A regular expression |
| `path` | Folder to search. The default is the project |
| `glob` | File name pattern, such as `*.cpp` |
| `ignore_case` | `true` matches without case |

Narrowed to a folder, without case:

```json
{ "name": "grep_search", "arguments": { "pattern": "outofstock", "path": "tests", "ignore_case": true } }
```

```
tests/test_stock.py:6: from inventory.stock import OutOfStock, Stock
tests/test_stock.py:13: with self.assertRaises(OutOfStock):
```

- More than 50 matching lines end with `[stopped at 50 matches]`. The model then narrows the pattern, the path, or the glob.
- No match returns `No matches.`
- A pattern that is not a regular expression returns the error. With `rg`, for the pattern `(`:

```
Error: rg: regex parse error:
    (?:()
    ^
error: unclosed group
```

Without `rg` the error starts with `Not a regular expression:`.

`grep_search` reads the files on disk, so it does not see unsaved edits.

### file_search

Request: "Open the pricing module."

```json
{ "name": "file_search", "arguments": { "query": "pricing" } }
```

Result:

```
tests/test_pricing.py
src/inventory/pricing.py
```

The query's letters have to appear in the path in order. They do not have to sit together, so `tstock` finds `tests/test_stock.py`:

```json
{ "name": "file_search", "arguments": { "query": "tstock" } }
```

```
tests/test_stock.py
src/inventory/stock.py
```

Letters in the file name and letters next to each other rank a path higher, and a shorter path wins a tie. The case of the query does not matter, spaces in it are dropped, and `\` is read as `/`. The best 20 paths come back. No match returns `No files match.`

### list_dir

Request: "What is in this project?"

```json
{ "name": "list_dir", "arguments": {} }
```

Result:

```
.crlf/
  rules/
.gitignore
README.md
scratch/
  notes.txt
src/
  inventory/
tests/
  test_pricing.py
  test_stock.py
```

The default shows two levels, so `.crlf/rules/` and `src/inventory/` are listed and their files are not. `path` names another folder and `depth` sets how many levels to show:

```json
{ "name": "list_dir", "arguments": { "path": "src", "depth": 3 } }
```

```
inventory/
  __init__.py
  pricing.py
  stock.py
  storage.py
```

In a git repository the list is the files git tracks or would add, so ignored files, and with them `.crlf/data`, are left out. A folder that holds no such file is not listed. An empty result is `(empty)`.

### read_file

Request: "Show me pricing.py."

```json
{ "name": "read_file", "arguments": { "path": "src/inventory/pricing.py" } }
```

Result:

```
     1|"""What an order costs."""
     2|
     3|BULK_QUANTITY = 100
     4|BULK_DISCOUNT = 0.1
     5|
     6|
     7|def unit_price(base, quantity):
     8|    """Large orders get ten percent off each unit."""
     9|    if quantity >= BULK_QUANTITY:
    10|        return round(base * (1 - BULK_DISCOUNT), 2)
    11|    return base
    12|
    13|
    14|def order_total(base, quantity):
    15|    return round(unit_price(base, quantity) * quantity, 2)
    16|
```

Every line is numbered. The text comes from the editor, so unsaved edits are included. Line 16 is the empty line after the file's last line break.

`line` and `limit` read part of a file. When the file goes on, the result says where to continue:

```json
{ "name": "read_file", "arguments": { "path": "src/inventory/stock.py", "line": 12, "limit": 4 } }
```

```
    12|    def add(self, item, quantity):
    13|        self.counts[item] = self.counts.get(item, 0) + quantity
    14|
    15|    def remove(self, item, quantity):
[The file goes on: read again with line 16, or with outline.]
```

Without `limit`, 400 lines are read, and the same note follows a longer file.

#### With outline

Request: "What does the Stock class offer?"

```json
{ "name": "read_file", "arguments": { "path": "src/inventory/stock.py", "outline": true } }
```

Result:

```
     4|class OutOfStock(Exception):
     8|class Stock:
     9|    def __init__(self):
    12|    def add(self, item, quantity):
    15|    def remove(self, item, quantity):
    20|    def low(self, threshold=5):
```

An outline is the lines of the whole file that open a class, a function, or another named block, with their line numbers. The model reads the outline of a long file first, then reads the part it needs with `line` and `limit`. `line` and `limit` are ignored when `outline` is set.

| File type | Lines kept |
| --- | --- |
| `.py`, `.pyi` | `class`, `def`, `async def` |
| `.c`, `.h`, `.cc`, `.cpp`, `.cxx`, `.hh`, `.hpp`, `.hxx`, `.m`, `.mm` | `class`, `struct`, `enum`, `union`, `namespace`, `template`, `#define`, and function definitions |
| `.js`, `.jsx`, `.mjs`, `.ts`, `.tsx` | `function`, `class`, `interface`, `enum`, `type`, and functions assigned to `const`, `let`, or `var` |
| `.java`, `.cs`, `.kt`, `.scala` | `class`, `interface`, `enum`, `struct`, `record`, `namespace`, `object`, `fun`, and method definitions |
| `.go` | `func`, `type` |
| `.rs` | `fn`, `struct`, `enum`, `trait`, `impl`, `mod`, `type` |
| `.rb` | `def`, `class`, `module` |
| `.sh`, `.bash` | Function definitions |
| `.md` | Headings |
| Anything else | The first line of each block that starts at the left margin |

The lines are found with patterns, not a parser, so an unusual layout can be missed. A Markdown file's outline is its headings:

```json
{ "name": "read_file", "arguments": { "path": "README.md", "outline": true } }
```

```
     1|# Inventory
     5|## Running the tests
```

A file with no such lines returns `No definitions found.`

### edit_file

#### One edit

Request: "Raise the bulk discount to fifteen percent."

```json
{
  "name": "edit_file",
  "arguments": {
    "path": "src/inventory/pricing.py",
    "old_text": "BULK_DISCOUNT = 0.1",
    "new_text": "BULK_DISCOUNT = 0.15"
  }
}
```

Result, after the write is accepted:

```
Wrote src/inventory/pricing.py
```

`old_text` has to appear in the file exactly, and only once. The write goes through the editor's review: with the default `"write": "review"`, a card shows the lines that change and nothing is saved until Accept. See [Writes](agent.md#writes).

Text that appears more than once is refused, and the file is left alone:

```json
{
  "name": "edit_file",
  "arguments": { "path": "src/inventory/stock.py", "old_text": "quantity", "new_text": "amount" }
}
```

```
Error: old_text appears 5 times in src/inventory/stock.py. Add surrounding lines, or set replace_all. Nothing was changed.
```

The model then sends more of the surrounding text, or adds `"replace_all": true` to change every match.

#### Several edits

Request: "Rename BULK_QUANTITY to BULK_ORDER and fix the docstring to match the new discount."

```json
{
  "name": "edit_file",
  "arguments": {
    "path": "src/inventory/pricing.py",
    "edits": [
      { "old_text": "ten percent", "new_text": "fifteen percent" },
      { "old_text": "BULK_QUANTITY", "new_text": "BULK_ORDER", "replace_all": true }
    ]
  }
}
```

Result:

```
Wrote src/inventory/pricing.py
```

The edits are applied in order to the text in memory and sent as one write, so there is one card and one diff to review. Each edit is checked against the text as the edits before it left it.

One edit that does not match fails the whole call, and none is made:

```json
{
  "name": "edit_file",
  "arguments": {
    "path": "src/inventory/pricing.py",
    "edits": [
      { "old_text": "fifteen percent", "new_text": "15 percent" },
      { "old_text": "BULK_LIMIT", "new_text": "BULK_ORDER" }
    ]
  }
}
```

```
Error: old_text of edit 2 was not found in src/inventory/pricing.py. Nothing was changed.
```

A rejected write returns `Error: The user rejected this change.` The model is told to ask what you want instead.

`write_file` takes `path` and `content`, replaces or creates the whole file, and goes through the same review. Its result is the same one line.

### delete_file

Request: "Delete the scratch notes file."

```json
{ "name": "delete_file", "arguments": { "path": "scratch/notes.txt" } }
```

Result, after the deletion is allowed:

```
Deleted scratch/notes.txt
```

`delete_file` calls `_crlf/fs/delete`, a method only CRLF answers, so the tool is offered only when the client is CRLF. It counts as a write:

| `write` permission | What happens |
| --- | --- |
| `review` (the default) or `ask` | A card titled `Delete scratch/notes.txt` with Allow, Always Allow, and Reject |
| `allow` | The file is deleted at once |
| `deny` | The model gets `Error: This project does not allow that.` |

- **Always Allow covers every write.** It allows all of the agent's writes inside the working directory for the rest of the session, edits included, without a card. Press Allow to permit one deletion.
- **Where the file goes.** A Windows file moves to the Recycle Bin, and the card says so. A file inside a WSL distro is deleted for good, because that folder has no Recycle Bin, and the card says that instead.
- **Files only.** A folder is answered as a path that does not exist.
- **Open tabs.** A tab open on the file stays open with its text.
- **The explorer.** The file's row stays until its folder is listed again.

Reject returns `Error: The user rejected this request.` A path that is not a file returns an error that names the full path and ends with `does not exist`.

### run_terminal_cmd

#### In the foreground

Request: "Run the tests."

```json
{ "name": "run_terminal_cmd", "arguments": { "command": "python -m unittest discover -s tests" } }
```

Result:

```
Exit code 0.
....
----------------------------------------------------------------------
Ran 4 tests in 0.000s

OK
```

The first line says how the command ended. The rest is its output, standard output and standard error together. With the default `"terminal": "ask"`, a card shows the command and its folder first. The command is a row on the Processes page and has its own Output log; see [Commands](agent.md#commands).

| Argument | Meaning |
| --- | --- |
| `command` | Required. One command line, run in `cmd.exe` on Windows and in `bash` inside a WSL distro |
| `cwd` | Working directory, relative to the project. The session remembers it: the next command that names none runs in the same folder |
| `timeout_seconds` | Stops a foreground command after this long. The default is 120 |
| `background` | `true` starts the command and returns without waiting |

A command that reaches its timeout is stopped:

```json
{ "name": "run_terminal_cmd", "arguments": { "command": "python -c \"import time; time.sleep(30)\"", "timeout_seconds": 2 } }
```

```
Stopped after 2 seconds.
```

Only the last 8,000 characters of output are returned. When more was printed, the first line ends with `The start of the output was cut.`

#### In the background

Request: "Start a web server in src so I can look at the files."

```json
{
  "name": "run_terminal_cmd",
  "arguments": { "command": "python -u -m http.server 8731", "cwd": "src", "background": true }
}
```

Result:

```
Started in the background with the id t1. Read what it prints with read_command_output, and end it with stop_command.
```

Use the background for a command that does not end by itself, such as a server or a file watcher. The call returns at once with the command's id. The command keeps running across turns until `stop_command` ends it or the session ends. It can also be stopped with Cancel on its Processes row.

Because `cwd` is remembered, a later command with no `cwd` also runs in `src`:

```json
{ "name": "run_terminal_cmd", "arguments": { "command": "cd" } }
```

```
Exit code 0.
C:\work\inventory\src
```

### read_command_output and stop_command

Request: "Is the server up?"

```json
{ "name": "read_command_output", "arguments": { "id": "t1" } }
```

Result:

```
Still running.
Serving HTTP on :: port 8731 (http://[::]:8731/) ...
::1 - - [12/Nov/2026 12:20:37] "GET /inventory/ HTTP/1.1" 200 -
```

The first line is `Still running.`, or how the command ended if it has. The rest is the end of its output so far.

Request: "Stop the server."

```json
{ "name": "stop_command", "arguments": { "id": "t1" } }
```

Result:

```
Ended by SIGKILL.
Serving HTTP on :: port 8731 (http://[::]:8731/) ...
::1 - - [12/Nov/2026 12:20:37] "GET /inventory/ HTTP/1.1" 200 -
```

`stop_command` ends the command and everything it started, returns the end of its output, and forgets the id. An id the session did not start, or one already stopped, returns:

```
Error: No background command has the id t1.
```

### fetch_rules

Request: "Fix the rounding bug in order_total." The system prompt tells the model which rules exist and to read the ones that bear on the task, so it starts with the list.

```json
{ "name": "fetch_rules", "arguments": {} }
```

Result:

```
style: Code style
testing: Testing
```

Each line is a rule's name and the first line of its file. The model then reads one by name:

```json
{ "name": "fetch_rules", "arguments": { "name": "testing" } }
```

```
# Testing

- Tests live in `tests/` and use `unittest`.
- Run `python -m unittest discover -s tests` before you say a change is done.
- A bug fix comes with a test that fails without it.
```

A name that is not a rule returns the names that are:

```
Error: There is no rule named security. The rules are: style, testing
```

Setting the folder up is in [Rules](#rules).

### diff_history

Request: "What have you changed so far?"

```json
{ "name": "diff_history", "arguments": {} }
```

Result:

```
Written in this session:
src/inventory/pricing.py +1 -1
src/inventory/pricing.py +3 -3
tests/test_pricing.py +1 -1
src/inventory/report.py +5 -0

git status --short:
 M src/inventory/pricing.py
 M tests/test_pricing.py
?? src/inventory/report.py

git log --oneline -n 10:
0280870 Add pricing, storage, and tests
a4959cc Add the stock module
```

- The first part is every write the session made that was accepted, in order, with the lines added and removed. A file written twice is listed twice. Deletions are not listed here; they show in git's part.
- The second and third parts appear when the project is a git repository and `git` is installed. They are git's changed files and its last 10 commits.
- A session that has written nothing starts with `Nothing was written in this session.`

## Rules

Rules are how the project's owner wants the work done: style, testing, what not to touch. They are Markdown files in a folder, `.crlf/rules` in the session's working directory by default. A rule's name is its file name without `.md`.

```
.crlf/
  rules/
    style.md
    testing.md
```

`.crlf/rules/style.md`:

```markdown
# Code style

- Four spaces, no tabs.
- Every public function has a one-line docstring.
- No new packages without asking.
```

`.crlf/rules/testing.md`:

```markdown
# Testing

- Tests live in `tests/` and use `unittest`.
- Run `python -m unittest discover -s tests` before you say a change is done.
- A bug fix comes with a test that fails without it.
```

How the agent uses them:

1. When a session starts and a rules folder exists, the system prompt gains one sentence: `The project has rules: style, testing. Read the ones that bear on the task with fetch_rules before you start.`
2. The model calls `fetch_rules` with no argument and gets each name with the first line of its file. A leading `#` is dropped, so a heading works as the summary. Make the first line say what the rule is about.
3. The model calls `fetch_rules` with a name and gets that file.

Only the names are sent with every request. A rule's text is sent when the model asks for it, so a long rule costs nothing in a session that does not need it. At most 8,000 characters of one rule are returned; split a longer one.

The folders are looked for when the session starts:

- `fetch_rules` is offered only when at least one of them exists at that moment. After creating the folder, press New.
- A rule file added later can be read by name, and shows in the list, without a new session. The sentence in the system prompt keeps the names it started with.

`--rules` names another folder, relative to the session's working directory, or absolute:

```json
{
  "name": "Claude",
  "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider anthropic --model claude-sonnet-4-5 --api-key-file .crlf/keys/anthropic --rules docs/agent-rules"
}
```

### More than one folder

`--rules` may be given more than once. Giving it at all replaces the default, so an entry that wants the workspace's rules and the application's names both:

```json
{
  "name": "Claude",
  "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider anthropic --model claude-sonnet-4-5 --api-key-file \"${appDir}/keys/anthropic\" --rules .crlf/rules --rules \"${appDir}/rules\""
}
```

- The folders are used in the order given. A rule name found in two folders is the earlier folder's, so here a workspace's `style` replaces the application's `style`.
- A name only one folder has is offered whichever folder it is in. The list the model sees is all of them, in name order.
- A folder that does not exist has no rules. `fetch_rules` is offered when at least one exists.
- A name the model gives is read only when it is in that list, so a rule cannot name a file outside the folders.
- `${appDir}/rules` is the `rules` folder under `%APPDATA%\CRLF\CRLF\.crlf`, shared by every workspace. Inside a WSL distro the variable is a `/mnt/<drive>/...` path.

This entry suits `application.crlf`, where one agent serves every workspace; see [crlf.md](crlf.md#application-settings). The `application.crlf` that ships has four entries of this shape already, so an agent is available once its key file is in the application folder's `.crlf/keys`; see [The shipped files](crlf.md#the-shipped-files).

### Committing rules

Commit `.crlf/rules`, so everyone's agent reads the same rules. Keep `.crlf/keys` out of git; `.crlf/data` ignores itself. `rg` skips `.crlf/rules` in `grep_search`, because the `.crlf` folder's name starts with `.`.

## Keeping it fast and cheap

Chat tokens are the larger share of an agent's cost. Every model call sends the system prompt, the tool definitions, and the whole conversation again. Four things keep that down.

### Result limits

Each tool returns a bounded result; the limits are in the table under [The tools](#the-tools). They are fixed in the scripts and have no flag. The model is told how to get more: `read_file` names the next line, `grep_search` says it stopped at 50, and a cut result says how many characters were left out.

### The Context option

A long session's conversation is mostly old tool results. The Context option sets a budget for the conversation, in tokens. It appears in the Agent panel's settings row, after the provider's own options.

| Value | Meaning |
| --- | --- |
| Unlimited | Nothing is shortened. The default |
| 50k tokens, 100k tokens, 200k tokens | The conversation is shortened when it passes this size |

Before each model call the agent works out the size of the request. There is no tokenizer, so it starts from the conversation's characters divided by four. Each reply then corrects it: the provider reports how many tokens the request really was, and the difference from the estimate is added to the next one. That difference is mostly the system prompt and the tool definitions, which go with every request and are in no message, so the budget is held against what the provider counts and not only the conversation. A server that reports no token counts leaves the plain estimate in use.

When the size is over the budget, the oldest tool results are replaced, one at a time, with a line that names the call. This stops when the size is at 70 percent of the budget.

What shortening looks like: a session at the 50k setting read three large files, about 25,000 tokens each. Before the next model call the estimate was 75,212 tokens. The two oldest results were replaced and the estimate fell to 25,274. Each replaced result now reads:

```
[The result of read_file({"path": "src/inventory/file1.py"}) was dropped to save context. Call it again if you need it.]
```

- Your messages, the model's replies, and the tool calls themselves stay. Only the text of old results goes.
- The newest results are the last to go.
- The model can make the call again when it needs the result.
- The agent's log on the Processes page records each time: `The conversation passed the context budget: 2 old tool results were shortened.`
- Nothing changes again until the budget is passed again. Shortening in steps keeps the start of the conversation the same between steps, which is what a provider's prompt cache needs.

`--context-budget` sets where the option starts. The panel's choice replaces it once made, and is remembered for that agent.

```json
{
  "name": "GPT",
  "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider openai --model gpt-5 --api-key-file .crlf/keys/openai --context-budget 100000"
}
```

A count that is not one of the listed values, such as `--context-budget 30000`, is added to the list.

Set the budget below the model's own context size. A lower budget costs less for each call and makes the model read files again more often.

### Usage and prices

The Agent panel shows a usage line under the status after each model reply; [agent.md](agent.md#usage) describes the line. The agent takes the token counts from each provider reply. Two things no provider reports come from lists of models: what a token costs, and how large a model's context is.

There are two lists, with one shape:

| List | Where | Who keeps it |
| --- | --- | --- |
| The shipped list | `models.json`, beside `crlf_agent.py` | It ships with the agent and every upgrade replaces it. It is always read and needs no flag. Copy Application Files to Workspace carries it with the scripts |
| Your own | The file `--prices` names. The shipped agents name `${appDir}/prices.json`, which is `%APPDATA%\CRLF\CRLF\.crlf\prices.json` | You. It ships with no models in it, is copied there once, and is never replaced |

Your file is read over the shipped list, field by field: a field you set wins, a field you leave out is the shipped one, and a model only you list is added. So one line corrects one price and keeps the model's sizes:

```json
{
  "currency": "USD",
  "models": {
    "gpt-6-astra": { "output": 45.0 },
    "qwen3-coder": { "context": 32768 }
  }
}
```

A whole entry, as the shipped list has it:

```json
{
  "currency": "USD",
  "models": {
    "gpt-6-astra": {
      "input": 10.0, "cached_input": 1.0, "cache_write": 12.5, "output": 50.0,
      "context": 1050000, "max_input": 922000,
      "long": { "over": 272000, "input": 20.0, "cached_input": 2.0, "cache_write": 25.0, "output": 75.0 }
    }
  }
}
```

| Field | Meaning | When no list has it |
| --- | --- | --- |
| `currency` | The ISO 4217 code the prices are in | `USD` |
| `input` | The price of a million input tokens | The model has no cost |
| `output` | The price of a million output tokens | The model has no cost |
| `cached_input` | The price of a million input tokens read from the provider's cache | The `input` price |
| `cache_write` | The price of a million input tokens written to the cache | The `input` price |
| `context` | The model's context window, in tokens. The window holds the conversation and the reply | See the sizes below |
| `max_input` | The most input tokens the model accepts, which is less than the window | The size is `context` |
| `long` | The prices of a long prompt: `over`, a count of input tokens, and any of the four prices | Every request is priced the same |

- **`long`.** Some providers charge more for a request with a long prompt, and charge all of that request at the higher prices. A reply whose input, cached tokens included, is more than `over` is priced with the prices `long` gives; a price `long` leaves out stays the ordinary one. With the entry above, a request of 300,000 input tokens costs $20 a million for input and $75 for output, and one of 200,000 costs $10 and $50. Each request is priced on its own.
- **Matching.** A model uses the entry with its exact id, or else the longest listed id it starts with. `gpt-5-2025-08-07` uses the `gpt-5` entry. `gpt-5-mini` uses its own entry when it has one, and the `gpt-5` entry when it does not, so list any model that should not take a shorter name's prices.
- **An entry with only `context`,** as for the local model above, gives a percentage and no cost.
- **A model no list has** has no cost and, unless `--context-window` or a Context budget gives a size, no percentage: the line reads `Context 84k tokens`.
- **Another currency.** The shipped prices are in US dollars. If your file names another currency, the shipped prices are dropped and the shipped sizes kept, so no cost mixes two currencies. Only the models your file prices have a cost.
- **Both lists are read when a session starts,** so an edit applies after New. A `--prices` file that is not there leaves the shipped list as it is. A file that is there and is not valid JSON fails the session with the file's path in the message.
- **A `prices.json` from an earlier version** lists `gpt-5`, `claude-sonnet-4-5`, and `grok-4.7` in full. Those entries still win, field by field, over the shipped ones. Delete them to get the shipped figures.

What the percentage is measured against, first match first:

| Setting | The context's size |
| --- | --- |
| A Context budget is set | The budget, because that is the limit the conversation is held to |
| Unlimited, and `--context-window` is given | That count |
| Unlimited, and the model's entry has `max_input` | That count: it is the limit a long conversation reaches first |
| Unlimited, and the model's entry has `context` | That count |
| None of these | Unknown. CRLF shows the tokens without a percentage. Another ACP client is sent nothing, because ACP's `usage_update` cannot say the size is unknown |

The cost is an estimate: the tokens each reply reported, at the listed prices. It leaves out anything a provider bills that is not a token of a chat reply, embedding calls for a vector index, and any discount or surcharge for batch, priority, or regional processing. A provider's own usage page is the bill.

A local model has a window and no price. `--context-window` gives its size with no file:

```json
{
  "name": "Local",
  "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider ollama --model qwen3-coder --context-window 32768"
}
```

#### The shipped models

`models.json` lists 53 models. Every figure was read from the provider's own page on 12 November 2026: each OpenAI model's page under `platform.openai.com/docs/models/`, Anthropic's models overview, pricing, and model deprecations pages, and xAI's models page. Prices are US dollars for a million tokens. An empty cell is a figure the page does not give, and the entry leaves that field out.

| Model | Window | Most input | Input | Cached | Cache write | Output | Long prompt |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `gpt-4.1` | 1,047,576 |  | $2 | $0.5 |  | $8 |  |
| `gpt-4.1-mini` | 1,047,576 |  | $0.4 | $0.1 |  | $1.6 |  |
| `gpt-4o` | 128,000 |  | $2.5 | $1.25 |  | $10 |  |
| `gpt-4o-mini` | 128,000 |  | $0.15 | $0.075 |  | $0.6 |  |
| `gpt-5` | 400,000 | 272,000 | $1.25 | $0.125 |  | $10 |  |
| `gpt-5-mini` | 400,000 | 272,000 | $0.25 | $0.025 |  | $2 |  |
| `gpt-5-nano` | 400,000 | 272,000 | $0.05 | $0.005 |  | $0.4 |  |
| `gpt-5-pro` | 400,000 |  | $15 |  |  | $120 |  |
| `gpt-5.1` | 400,000 |  | $1.25 | $0.125 |  | $10 |  |
| `gpt-5.2` | 400,000 |  | $1.75 | $0.175 |  | $14 |  |
| `gpt-5.2-pro` | 400,000 |  | $21 |  |  | $168 |  |
| `gpt-5.3-codex` | 400,000 | 272,000 | $1.75 | $0.175 |  | $14 |  |
| `gpt-5.4` | 1,050,000 |  | $2.5 | $0.25 |  | $15 | over 272,000: input $5, output $22.5 |
| `gpt-5.4-mini` | 400,000 | 272,000 | $0.75 | $0.075 |  | $4.5 |  |
| `gpt-5.4-nano` | 400,000 | 272,000 | $0.2 | $0.02 |  | $1.25 |  |
| `gpt-5.4-pro` | 1,050,000 |  | $30 |  |  | $180 | over 272,000: input $60, output $270 |
| `gpt-5.5` | 1,050,000 |  | $5 | $0.5 |  | $30 | over 272,000: input $10, output $45 |
| `gpt-5.5-pro` | 1,050,000 |  | $30 |  |  | $180 |  |
| `gpt-5.6-luna` | 1,050,000 | 922,000 | $0.2 | $0.02 | $0.25 | $1.2 | over 272,000: input $0.4, output $1.8 |
| `gpt-5.6-sol` | 1,050,000 | 922,000 | $4 | $0.4 | $5 | $20 | over 272,000: input $8, output $30 |
| `gpt-5.6-terra` | 1,050,000 | 922,000 | $2 | $0.2 | $2.5 | $12 | over 272,000: input $4, output $18 |
| `gpt-6-astra` | 1,050,000 | 922,000 | $10 | $1 | $12.5 | $50 | over 272,000: input $20, cached $2, write $25, output $75 |
| `gpt-6-luna` | 1,050,000 | 922,000 | $0.1 | $0.01 | $0.125 | $0.5 | over 272,000: input $0.2, cached $0.02, write $0.25, output $0.75 |
| `gpt-6-sol` | 1,050,000 | 922,000 | $2 | $0.2 | $2.5 | $10 | over 272,000: input $4, cached $0.4, write $5, output $15 |
| `gpt-6.1-sol` | 1,050,000 | 922,000 | $2 | $0.1 | $2.5 | $10 | over 272,000: input $4, cached $0.2, write $5, output $15 |
| `gpt-6.1-terra` | 1,050,000 | 922,000 | $1 | $0.05 | $1.25 | $5 | over 272,000: input $2, cached $0.1, write $2.5, output $7.5 |
| `o3` | 200,000 |  | $2 | $0.5 |  | $8 |  |
| `claude-fable-5-2` | 1,000,000 |  | $8 | $0.2 | $10 | $40 |  |
| `claude-opus-5-5` | 1,000,000 |  | $4 | $0.2 | $5 | $20 |  |
| `claude-sonnet-5-5` | 1,000,000 |  | $2 | $0.1 | $2.5 | $10 |  |
| `claude-haiku-5-5` | 1,000,000 |  | $0.1 | $0.01 | $0.125 | $0.5 | over 100,000: input $0.5, cached $0.05, write $0.625, output $2.5 |
| `claude-mythos-5-2` | 1,000,000 |  | $8 | $0.2 | $10 | $40 |  |
| `claude-fable-5-1` | 1,000,000 |  | $10 | $0.25 | $12.5 | $50 |  |
| `claude-mythos-5-1` | 1,000,000 |  | $10 | $0.25 | $12.5 | $50 |  |
| `claude-fable-5` | 1,000,000 |  | $10 | $1 | $12.5 | $50 |  |
| `claude-mythos-5` | 1,000,000 |  | $10 | $1 | $12.5 | $50 |  |
| `claude-opus-5` | 1,000,000 |  | $5 | $0.5 | $6.25 | $25 |  |
| `claude-opus-4-8` | 1,000,000 |  | $5 | $0.5 | $6.25 | $25 |  |
| `claude-opus-4-7` | 1,000,000 |  | $5 | $0.5 | $6.25 | $25 |  |
| `claude-opus-4-6` | 1,000,000 |  | $5 | $0.5 | $6.25 | $25 |  |
| `claude-opus-4-5` | 200,000 |  | $5 | $0.5 | $6.25 | $25 |  |
| `claude-sonnet-5` | 1,000,000 |  | $2 | $0.2 | $2.5 | $10 |  |
| `claude-sonnet-4-6` | 1,000,000 |  | $3 | $0.3 | $3.75 | $15 |  |
| `claude-sonnet-4-5` | 200,000 |  | $3 | $0.3 | $3.75 | $15 |  |
| `claude-haiku-4-5` | 200,000 |  | $1 | $0.1 | $1.25 | $5 |  |
| `grok-4.7` | 500,000 |  | $2 | $0.5 |  | $6 | over 199,999: input $4, cached $1, output $12 |
| `grok-4.6` | 500,000 |  | $2 | $0.5 |  | $6 | over 199,999: input $4, cached $1, output $12 |
| `grok-4.5` | 500,000 |  | $2 | $0.3 |  | $6 | over 199,999: input $4, cached $0.6, output $12 |
| `grok-4.3` | 1,000,000 |  | $1.25 | $0.2 |  | $2.5 | over 199,999: input $2.5, cached $0.4, output $5 |
| `grok-4.20-0309-reasoning` | 1,000,000 |  | $1.25 | $0.2 |  | $2.5 | over 199,999: input $2.5, cached $0.4, output $5 |
| `grok-4.20-0309-non-reasoning` | 1,000,000 |  | $1.25 | $0.2 |  | $2.5 | over 199,999: input $2.5, cached $0.4, output $5 |
| `grok-build-0.1` | 256,000 |  | $1 | $0.2 |  | $2 | over 199,999: input $2, cached $0.4, output $4 |
| `grok-4.20-multi-agent-0309` | 1,000,000 |  | $1.25 | $0.2 |  | $2.5 | over 199,999: input $2.5, cached $0.4, output $5 |

What the pages left open, and how the list reads them:

- **Most input** is stated only on the OpenAI pages that have a "Maximum input tokens" line. For every other model, all of Anthropic's and xAI's included, the percentage is against the whole window, so the model refuses input somewhat before the line reaches 100 percent.
- **The cache write price of the three GPT-5.6 models** is not a row on their pages. It is 1.25 times the input price, from the pages' sentence "Cache writes are billed at 1.25x the uncached input token rate."
- **`gpt-5.6-sol`** is listed at a promotional price that its page says ends on 30 November 2026. The page does not give the price after that.
- **The cache write price of `gpt-6.1-terra`** is the one its page gives for 30-minute retention. 12-hour retention costs twice as much.
- **The long-prompt rule of `gpt-5.4`, `gpt-5.4-pro`, `gpt-5.5`, and the GPT-5.6 models** names input and output only, so their cached price stays the ordinary one. The GPT-5.4 and GPT-5.5 pages say the higher prices apply "for the full session"; the agent prices each request on its own, so a session that passes 272,000 tokens and then shortens may read low.
- **xAI** charges the higher prices at 200,000 prompt tokens or more, so its `over` is 199,999.
- **`o3`** has two unlabeled price tables on its page. The list has the first, which matches the page's comparison row; the second is half of it.
- **`gpt-5-pro`** has no "Maximum input tokens" line; its page gives 272,000 as the most output.
- **The four `-pro` models** have no cached price on their pages, so cached input is priced as input.
- **Anthropic's ids.** The overview gives ids for `claude-fable-5-2`, `claude-opus-5-5`, `claude-sonnet-5-5`, and `claude-haiku-5-5`. The other fourteen are named there without one, and their ids come from the model deprecations page. That page gives `claude-opus-4-5`, `claude-sonnet-4-5`, and `claude-haiku-4-5` only with a date on the end, such as `claude-sonnet-4-5-20250929`; they are listed without it so that both forms match.
- **Deprecated models** are listed while their pages still give figures: the GPT-4 models, `gpt-5` and its mini, nano, and pro, `gpt-5.1`, `gpt-5.3-codex`, `gpt-5.4-nano`, `o3`, and `claude-sonnet-4-5`.

Prices change and models are replaced. A wrong price gives a wrong cost and nothing else. To correct a figure or add a model before the next version of the editor does, put it in `prices.json` under its id as the provider lists it, and start a new session.

### Prompt caching

A provider can charge less for the start of a request when it is the same as the start of an earlier one. The agent builds the system prompt and the tool definitions once for each session and never reorders them, so every request of a session starts with the same text.

| Provider | What happens |
| --- | --- |
| `anthropic` | Anthropic caches only up to a marked block. The agent marks three: the system prompt, the last tool definition, and the newest message. The next request reads the system prompt, the tools, and the conversation so far from the cache |
| `openai` | OpenAI discounts a repeated start by itself. Nothing is marked |
| `xai`, `ollama`, `lmstudio`, `openai-compatible` | The request starts with the same text each time. Whether that is cached or discounted is up to the server |

Three things change the start of the request, and so miss the cache once: choosing another model, a shortening step of the Context option, and a new session. Whether a cached request is billed at a lower rate, and how long a cache lasts, are the provider's terms.

### A low-cost entry and a high-effort one

Two entries in one settings file. Pick between them in the panel's picker.

```json
"agents": [
  {
    "name": "GPT (everyday)",
    "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider openai --model gpt-5-mini --reasoning-effort low --api-key-file .crlf/keys/openai --index fts --context-budget 50000"
  },
  {
    "name": "Claude (hard problems)",
    "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider anthropic --model claude-sonnet-4-5 --thinking 16k --max-tokens 16384 --api-key-file .crlf/keys/anthropic --index vector --embed-provider local --context-budget 200000"
  }
]
```

| Setting | Everyday | Hard problems |
| --- | --- | --- |
| Model | A small one | A large one |
| Reasoning or thinking | `--reasoning-effort low` | `--thinking 16k` |
| Index | `fts`: free, built in seconds | `vector` with local embeddings: free, slower to build, finds by meaning |
| Context | 50k tokens | 200k tokens |

`workspace.crlf` at the root of this repository starts its GPT entry at low reasoning effort and names `--index fts` on every agent entry.

To raise a setting for one hard question without editing the settings file:

1. Open the Agent panel (View > Agent, Ctrl+Shift+A).
2. In the settings row, set Reasoning to High, or Thinking to 16k tokens for an Anthropic model.
3. Set Context to 200k tokens or Unlimited.

A change applies to the model's next reply, even during a turn. Each choice is remembered for that agent name, so set it back afterwards. The index is not a panel setting. It is chosen by `--index` on the agent's command line.

## Rebuilding or removing the index

The index is rebuilt from the project's files, so it is always safe to delete.

**Rebuild.** Stop the agent, delete the folder, and start a new session. The first refresh builds the index again.

PowerShell, in the project folder:

```powershell
Remove-Item -Recurse -Force .crlf/data
```

bash, inside a WSL distro:

```bash
rm -rf .crlf/data
```

Stop the agent first, with Cancel on the `agent` row of the Processes page. On Windows a file an agent has open may refuse to be deleted. For a vector index, deleting the folder also deletes the embeddings, so every chunk is embedded again. With a remote provider that is charged again.

**Remove.** Change the entry to `--index grep`, start a new session, and delete the folder. Nothing recreates it.

**When the embedding model changes.** The index records the embedding provider and model that made its vectors. Vectors from two models cannot be compared. When a session starts with a different `--embed-provider` or `--embed-model`, the stored vectors are deleted and the next refresh embeds every chunk again. The files and chunks are kept, so no file is read again. There is nothing to delete by hand.

**When the index kind changes.** `fts` and `vector` share the one `index.sqlite`. Changing `--index` from one to the other keeps the files and chunks, and the first refresh adds what the new kind needs. Stored vectors are kept while `fts` is in use.

**When nothing changed.** A session that starts on an index that matches the disk reads no file and embeds nothing.

**Folders from an earlier version.** The index used to be kept in `.crlf-data` and the rules in `.crlf-rules`. Neither folder is read now, and nothing is moved. The index is built again under `.crlf/data`, and the old `.crlf-data` can be deleted. Move rule files from `.crlf-rules` to `.crlf/rules` by hand.

## WSL

An agent whose project is inside a WSL distro runs in that distro; see [Paths and WSL](agent.md#paths-and-wsl). Programs have to be installed there. Files on the Windows side are reached through the drive mounts and need no copy.

- `python3`, installed in the distro.
- The agent's scripts stay where they are. `${programDir}` is given to the command as a `/mnt/<drive>/...` path, so `python3 "${programDir}/scripts/agent/crlf_agent.py"` runs the shipped scripts from the install folder.
- `fastembed`, `sqlite-vec`, and `numpy`, installed with that distro's `pip`.
- `rg` and `git`, installed in the distro.
- Key files. `${appDir}` is a `/mnt` path too, so a key at `"${appDir}/keys/anthropic"` in the application folder serves Windows and the distro. A key in the project's own `.crlf/keys` is inside the distro already.
- An Ollama that answers on `localhost:11434` from inside the distro, or `--embed-base-url` with the address that does.

An entry used from Windows folders and from a distro gives `command` as an object, because the interpreter's name differs:

```json
{
  "name": "Claude",
  "command": {
    "windows": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider anthropic --model claude-sonnet-4-5 --api-key-file \"${appDir}/keys/anthropic\"",
    "wsl": "python3 \"${programDir}/scripts/agent/crlf_agent.py\" --provider anthropic --model claude-sonnet-4-5 --api-key-file \"${appDir}/keys/anthropic\""
  }
}
```

The `/mnt` paths depend on WSL mounting the Windows drives, which it does by default. With mounting turned off, or to keep the scripts with the project, File > Copy Application Files to Workspace copies the shipped and application scripts and the application's rules into the project's `.crlf` folder, and the entry then names `.crlf/scripts/agent/crlf_agent.py`. Key files are not copied; put them inside the distro. See [crlf.md](crlf.md#copy-application-files-to-workspace).

The index folder is created inside the distro, at `.crlf/data` in the project.

Nothing in this section has been run in a distro. The path conversion and the choice of the `wsl` line are covered by `ProjectTests`.

## Limits

- Tools that read the disk in the agent's own process do not pass through the editor's `read` permission: `list_dir`, `file_search`, `grep_search`, `codebase_search` and the index, `fetch_rules`, and `diff_history`. `"read": "deny"` does not stop them. See [Permissions](agent.md#permissions).
- Those tools see the files as saved. Only `read_file`, `edit_file`, and `write_file` see unsaved edits.
- The index covers the session's working directory and nothing outside it.
- `--embed-provider local` has not been run; see [Local, with fastembed](#local-with-fastembed). The other embedding providers have been run only against a local stand-in server.
