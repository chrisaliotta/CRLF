# Agent: design notes

This document records how the Agent panel works and why the editor holds none of the AI logic.

The editor is a client of the [Agent Client Protocol](https://agentclientprotocol.com) (ACP). An agent is a program named in a `.crlf` file. It owns everything an agent decides: the model and its provider, the API key, the prompt, the tool loop, and the conversation. The editor shows the conversation and answers the agent's requests for files, commands, and its own state. Swapping OpenAI for Anthropic, Grok, or a local model is a different command line. Nothing in the editor changes.

The setup keys are in [crlf.md](crlf.md#agents). The reference agent's tools, its code index, and worked examples of each are in [agent-tools.md](agent-tools.md). Threading is in [processes.md](processes.md).

## Choosing an agent

```json
"agents": [
  { "name": "Claude", "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider anthropic --model claude-sonnet-4-5" },
  { "name": "Grok", "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider xai --model grok-4" },
  { "name": "Local", "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider ollama --model qwen3-coder" },
  { "name": "Claude Code", "command": "npx @zed-industries/claude-code-acp" }
]
```

The first three run the reference agent that ships beside the program; the model names are examples to replace with the ones your account offers. The last one is Claude Code behind Zed's ACP adapter. Any program that speaks ACP on standard input and output works the same way; Gemini CLI, for one, has an ACP mode. Check each adapter's own instructions for its command and sign-in.

View > Agent (Ctrl+Shift+A) opens the panel. The picker at its top lists the entries in merge order: the workspace files' first, then those inherited from `application.crlf`. The chosen agent starts when the panel opens, or when the first message is sent, and runs until the project changes it, another agent is picked, or New starts a fresh session. One agent runs at a time.

An entry can live in a workspace file or in the application file. The `application.crlf` that ships already lists four agents that run the reference agent: Claude, GPT, Grok, and Local. Each works once its key file exists in the application folder, such as `%APPDATA%\CRLF\CRLF\.crlf\keys\openai`; Local needs none. The application's entries are offered in every folder that has no settings of its own, and in a workspace that inherits `agents`; a name defined in both is the workspace's. An application entry names its script and key with `${programDir}` and `${appDir}`; see [crlf.md](crlf.md#application-settings).

## The panel

| Part | What it does |
| --- | --- |
| Picker | The agents from the `.crlf` files. Choosing one starts a new session with it. The choice is remembered |
| Thinking | On: the agent's thinking is shown in full as it streams, above the reply it led to. Off: one "Thinking…" line while it streams. The choice is remembered |
| New | Ends the session and starts another with an empty transcript |
| Settings | One list or check box for each setting the agent offers, such as its model. See [Settings](#settings) |
| Status | Starting, ready, working, or the last error |
| Transcript | Your messages, the agent's replies as Markdown, its thinking, each tool call with its state, and its plan. A file named by a tool call is a link that opens it at that line |
| Cards | Questions waiting for you: a permission, a command, or a write to review. A card stays until it is answered or the turn ends |
| Prompt | Enter sends; Shift+Enter starts a new line. Send is Stop while a turn runs |
| Include the current file | Sends the file in front as a link, and the selection as text |

Stop sends `session/cancel`. Cards still open are answered as cancelled, and the agent ends the turn with `cancelled`.

## Settings

The editor does not know what a model or a reasoning level is. The agent says which settings its session has, as ACP [session config options](https://agentclientprotocol.com/protocol/session-config-options), and the panel draws one control for each, in the agent's order: a list for a `select`, a check box for a `boolean`. An option of the `model` category gets a row to itself. The option's description is the control's tooltip. A type the editor cannot draw is left out, and the agent keeps that option's value.

- The list arrives with the `session/new` reply. A choice is sent as `session/set_config_option`, and the agent answers with the whole list again, because one choice can change the others: another model can take other settings. The agent can also send the list itself, as a `config_option_update`.
- A choice may be made while a turn runs. The agent decides when it applies; the reference agent uses it for its next model reply.
- A refused choice shows the agent's error, and the control goes back to the value the agent still has.
- Each choice is remembered per agent name. A new session is sent every remembered value it still offers and does not already have, one at a time, before any waiting message. So the command line in the `.crlf` file sets where an agent starts, and the panel's choices replace that once made.

Thinking arrives as `agent_thought_chunk` updates. Whether an agent sends any is the agent's business; how much is shown is the Thinking button's.

## Permissions

Each request is checked once, where it enters the editor, by `AgentPolicy`. The paths an agent sends come from a model, so they are the one input the editor checks; nothing after that point does.

| Action | Covers | Default |
| --- | --- | --- |
| `read` | `fs/read_text_file`, open files, problems, git status, the terminal's text | `allow` |
| `write` | `fs/write_text_file`, `_crlf/fs/delete` | `review` |
| `terminal` | `terminal/create`, typing into your terminal | `ask` |

`allow` answers at once. `ask` shows a card with Allow, Always Allow, and Reject. `review` is for writes: the card shows how many lines change, and Open Diff shows the change in the Diff tab. `deny` answers with an error, which the agent sees.

A deletion has no diff to review, so under `review` and under `ask` its card is the one with Allow, Always Allow, and Reject. The card says whether the file moves to the Recycle Bin or is deleted for good. Always Allow there allows every write for the session, not only deletions.

A path outside the session's working directory is never allowed without a card, whatever the setting. Always Allow and Accept All last for the session and apply only inside the working directory. A relative path is resolved against the working directory, and `..` is resolved before the check, so `src/../../secrets` is outside.

These checks cover what an agent asks the editor for. An agent is a program on the same machine, and what it reads from the disk itself never reaches the editor. The reference agent's `list_dir`, `file_search`, `grep_search`, `codebase_search` and its index, `fetch_rules`, and `diff_history` read the disk in the agent's own process, so they do not pass through the `read` permission.

An agent's own `session/request_permission` is a card with the agent's buttons, such as Allow once and Always allow. Claude Code asks this way before it edits or runs anything.

## What the editor answers

| Method | Answer |
| --- | --- |
| `fs/read_text_file` | The open editor's text, unsaved edits included, otherwise the file read on the pool. `line` and `limit` select lines |
| `fs/write_text_file` | After review: an open file is replaced as one undo step and saved; any other file is written on the pool, keeping its encoding and line endings. A new file's folders are created |
| `session/request_permission` | The button you press, or `cancelled` |
| `terminal/create` | Starts the command and returns its id |
| `terminal/output`, `terminal/wait_for_exit`, `terminal/kill`, `terminal/release` | Answered on the orchestrator thread from the running command |

The editor also answers six methods of its own. ACP reserves names that start with `_` for these:

| Method | Answer |
| --- | --- |
| `_crlf/editor/state` | `files` (path and whether it has unsaved edits), and for the file in front `activeFile`, `line`, `column`, and `selection` |
| `_crlf/problems` | `problems`: each check span with `path`, `line`, `column`, `severity`, and `message` |
| `_crlf/git/status` | `enabled` and `repositories`: root, branch, head, upstream, ahead, behind, merging, and `changes` with git's two status letters |
| `_crlf/terminal/read` | `text`: the scrollback and screen of the terminal on screen |
| `_crlf/terminal/send` | Types `text` into that terminal, after a `terminal` check |
| `_crlf/fs/delete` | Deletes the file at `path`, after a `write` check: to the Recycle Bin, or for good inside a WSL distro. Files only: a folder is answered as a path that does not exist. An open tab keeps its text, and the explorer row stays until its folder is listed again. The answer is `{}` |

An agent that does not know these names never calls them. The reference agent offers them to its model only when `initialize` names the client `CRLF`.

## Writes

The agent's write waits for its reply while the card is open, so the agent does not run the tests against the old file. Accept writes the file and replies; Reject replies with an error the agent can act on. With `"write": "allow"` the write happens at once and git is the undo.

An open file is changed in its editor, so its undo history keeps the change and its tab never asks whether to reload. Line endings in the agent's text are dropped before the write; the file keeps the ones it had.

## Commands

`terminal/create` runs through `ProcessLaunch`, as a menu command does: `cmd.exe /d /s /c` on Windows, `bash -lc` inside a WSL distro, each in a kill-on-close job. The command and its arguments are joined into one line, with an argument quoted when it needs it. `outputByteLimit` keeps the end of the output. Each command is a row on the Processes page, `agent-terminal:<id>`, which Cancel stops, and an Output log named `agent: <command>`.

## Paths and WSL

An agent whose working directory is inside a WSL distro runs in that distro and sees Linux paths. The session converts `path` and `cwd` in the agent's requests to the `\\wsl.localhost` form, converts the editor's links and its own answers back, and gives `session/new` the Linux working directory.

The programs the agent's command needs have to be installed inside the distro. For the reference agent that is `python3`, any package installed with that distro's `pip`, and `rg` and `git` when they are wanted.

The files it needs do not have to be copied there. `${programDir}` and `${appDir}` are given to the command as `/mnt/<drive>/...` paths, so the shipped scripts in the install folder and the key and rule files in the application folder are read from the Windows side. That depends on WSL mounting the Windows drives, which it does by default. An entry used in both environments gives `command` as an object with a `windows` and a `wsl` line, because the interpreter is `python` in one and `python3` in the other. [crlf.md](crlf.md#windows-and-wsl) has the details, and File > Copy Application Files to Workspace is the alternative that puts the scripts in the project. None of this has been run in a distro.

## Where it runs

`AgentSession` lives on the orchestrator thread with its process, through the same `JsonLineProcess` that runs the autocomplete session. It sends `initialize` and `session/new`, sends each prompt, and passes every other request to the UI thread as an `AgentRequest`. `AgentHost`, on the UI thread, answers each method from a table of handlers, after `AgentPolicy`. A file it has to read or write goes to the pool. `AgentController` starts and stops the agent, carries each turn between the panel and the session, and remembers the settings chosen for each agent. `AgentPanel` only draws what it is told and reports what you do.

The session is the Processes row `agent`. Cancel stops the agent; Reload starts a new session. A file listed under the agent's `watch` starts a new session when it is saved, so an edit to an agent script takes effect.

## The reference agent

`scripts/agent/crlf_agent.py` is an ACP agent in Python. It needs no packages beyond the standard library. Vector search with local embeddings is the one feature that needs a package, and two more packages are used for speed when they are installed; see [agent-tools.md](agent-tools.md#optional-speed-ups).

| Provider | Server | Key |
| --- | --- | --- |
| `openai` | `https://api.openai.com/v1` | `OPENAI_API_KEY` |
| `anthropic` | `https://api.anthropic.com` | `ANTHROPIC_API_KEY` |
| `xai` | `https://api.x.ai/v1` | `XAI_API_KEY` |
| `ollama` | `http://localhost:11434/v1` | none |
| `lmstudio` | `http://localhost:1234/v1` | none |
| `openai-compatible` | `--base-url` | `--api-key-file` or `--api-key-env`, when the server wants one |

`providers.py` has one adapter per wire format: the Responses API for `openai`, the Messages API for `anthropic`, and Chat Completions for the rest. `--base-url` moves any of them.

### Models and settings

A session lists the provider's models (`GET /models`) and offers them as the `model` option. OpenAI's list is cut to chat models: embeddings, speech, image, realtime, search, and moderation ids are left out, and so are dated snapshots. When the list cannot be read, only `--model` is offered and the log says why. The other options depend on the provider and the model:

| Provider | Option | Values | Sent as |
| --- | --- | --- | --- |
| `openai` | Reasoning | Default, minimal, low, medium, high | `reasoning.effort` |
| `openai` | Thinking | Summary, off | `reasoning.summary: "auto"` |
| `openai` | Verbosity | Default, low, medium, high | `text.verbosity` |
| `anthropic` | Thinking | Off, 4k, 16k, 32k tokens, adaptive | `thinking` with `budget_tokens`, or `{"type": "adaptive"}` |
| `xai` | Reasoning | Default, low, high | `reasoning_effort`, for `grok-3-mini` models only |
| all | Max tokens | A few sizes; Default except for `anthropic` | `max_output_tokens`, or `max_tokens` |
| all | Context | Unlimited, 50k, 100k, 200k tokens | Not sent. The agent shortens the oldest tool results before a model call when its estimate of the conversation passes the budget |

Context (id `context_budget`) is the agent's own option and is listed last. `--context-budget` sets where it starts, and the default is `unlimited`. [agent-tools.md](agent-tools.md#the-context-option) shows what shortening looks like.

Default leaves the field out, so the provider decides. OpenAI's reasoning options are offered for the o-series and for GPT-5 and later; not every model takes every level, and the provider's error says so when one does not. `--model`, `--max-tokens`, `--reasoning-effort`, and `--thinking` are the values a session starts with. A value that is not in a list, such as `--max-tokens 1234`, is added to it.

### Thinking

- **OpenAI.** A reasoning model's thinking is only available through the Responses API, as a summary, not the raw reasoning. It streams as the agent's thinking. An organization that is not verified is refused summaries; the agent then asks once more without one and logs the refusal. Requests set `store: false`, so nothing is kept at OpenAI; the reasoning item comes back encrypted and is sent again with the next request, ahead of the tool call it led to.
- **Anthropic.** Thinking is off until the Thinking option turns it on. The budget is added to Max tokens, so the answer keeps its room; a model with a lower output limit rejects a sum above it. While thinking is on, each reply's thinking blocks are kept with their signatures and sent back first in that reply, which the API requires before a tool result. Newer Claude models take Adaptive instead of a budget.
- **xAI and local servers.** `reasoning_content` in a Chat Completions stream is shown as thinking when the server sends it.

What a provider must be handed back stays in the conversation history as that adapter's own `provider_state`, and is dropped when the model changes.

`--max-tokens` and the rest only start a session:

```json
{ "name": "GPT", "command": "python scripts/agent/crlf_agent.py --provider openai --model gpt-5 --reasoning-effort low --api-key-file .crlf/keys/openai" }
```

### Keys

`--api-key-file` names a file that holds the key, relative to the agent's working directory; whitespace around the key is dropped. It wins over the environment, and `--api-key-env` names another variable. The key is read again for every session, so a rotated key is used after New, with no restart. Keep key files out of git: this repository ignores `.crlf/keys/`. An agent in `application.crlf` keeps its key in the application folder and names it `"${appDir}/keys/openai"`, so one file serves every workspace and is in no repository.

```json
{ "name": "GPT", "command": "python scripts/agent/crlf_agent.py --provider openai --model gpt-5 --api-key-file .crlf/keys/openai" }
```

A key file keeps the key out of the `.crlf` files, which are usually committed, and out of the command line, which the Processes page and the system's process list show. A missing, unreadable, or empty key fails `session/new`, so the panel names the file or variable before the first message.

### Tools

The model's tools are in the `tools/` package, one module for each area. [agent-tools.md](agent-tools.md#the-tools) has an example call and result for each.

| Tool | What it does | Goes through |
| --- | --- | --- |
| `read_file` | Reads a file with numbered lines, 400 at a time, or with `outline` only the lines that define classes and functions | `fs/read_text_file`, so it sees unsaved edits |
| `write_file` | Creates or replaces a whole file | `fs/write_text_file`, so it is reviewed |
| `edit_file` | Replaces an exact passage that has to occur once, or several passages given as `edits`, sent as one write | `fs/read_text_file` and `fs/write_text_file` |
| `delete_file` | Deletes a file. Offered only to CRLF | `_crlf/fs/delete` |
| `list_dir` | Lists a folder as a tree | The disk, in the agent's process |
| `file_search` | Finds files by name: the best 20 paths | The disk, in the agent's process |
| `codebase_search` | Finds code by what it does: the best 8 passages from the index | The disk and the index, in the agent's process |
| `grep_search` | Finds lines by regular expression, 50 at most, with `rg` when it is installed | The disk, in the agent's process |
| `run_terminal_cmd` | Runs a command and waits up to `timeout_seconds`, then reads the output and releases the terminal. With `background` it returns the terminal's id at once and keeps the terminal | `terminal/create` and the other `terminal/` methods |
| `read_command_output`, `stop_command` | Read and end a background command | `terminal/output`, `terminal/kill`, `terminal/release` |
| `fetch_rules` | Lists the project's rule files, or reads one. Offered only when at least one rules folder exists as the session starts | The disk, in the agent's process |
| `diff_history` | The session's own writes, then `git status --short` and `git log --oneline -n 10` | Its own record, and `git` run in the agent's process |
| `editor_state`, `problems`, `git_status`, `read_terminal` | What the editor shows. Offered only to CRLF | The `_crlf/` methods |

These replace `list_directory`, `search`, and `run_command`, which are now `list_dir`, `grep_search`, and `run_terminal_cmd`.

- **Files on disk.** The tools that read the disk list the files git tracks or would add when the folder is a repository, so `.gitignore` is respected. Elsewhere they skip dot folders, `node_modules`, and `__pycache__`. They do not pass through the `read` permission; see [Permissions](#permissions).
- **Result limits.** Each tool's result is cut to its own limit before the model gets it, and the transcript shows the first 2,000 characters.
- **Background commands.** A command started with `background` runs until `stop_command` or the end of the session, when the agent releases every terminal it still holds.

### The code index

`codebase_search` asks an index that `--index` chooses: `grep` keeps nothing and reads the files for each search, `fts` (the default) keeps a full-text index in SQLite, and `vector` keeps an embedding of each passage and needs `--embed-provider`. The index is kept in `--index-dir`, by default `.crlf/data` in the session's working directory. The `.crlf` folder is a dot folder, so the explorer hides it while Hide Dot Folders is on. It is refreshed on a background thread when the session starts and before each search, and a search does not wait for it. Progress is the row's Activity, such as `indexing 120/480`. Choosing an index, the embedding providers, and their install steps are in [agent-tools.md](agent-tools.md#choosing-an-index).

```json
{ "name": "Claude", "command": "python scripts/agent/crlf_agent.py --provider anthropic --model claude-sonnet-4-5 --api-key-file .crlf/keys/anthropic --index vector --embed-provider local" }
```

### Rules

`--rules` names a folder of `.md` files, by default `.crlf/rules` in the session's working directory. It may be given more than once, and giving it replaces the default, so an entry that wants the workspace's rules and the application's names both: `--rules .crlf/rules --rules "${appDir}/rules"`. A rule name found in two folders is the earlier folder's. When at least one folder exists, the system prompt lists the rule names and the model reads the ones it needs with `fetch_rules`. See [agent-tools.md](agent-tools.md#rules).

### Prompt caching

The system prompt and the tool definitions are built once for each session and keep their order, so every request of a session starts with the same text. OpenAI discounts a repeated start by itself. Anthropic caches only up to a marked block, so the Anthropic adapter marks the system block, the last tool definition, and the newest message with `cache_control`.

### Turns and logs

A turn ends when the model answers without a tool call, after 60 model replies (`max_turn_requests`), or when it is cancelled. Logs go to standard error, which is the Processes page log; `CRLF_STATUS:` lines set the row's Activity.

## Tests

`AgentTests` covers `AgentPolicy` and the reading of JSON-RPC messages, session updates, and config options. `AgentSessionTests` drives `tests/fake_acp_agent.py` through a whole turn: a file link, a read the editor answers, a command it runs, and the stop reason. A second case sets a config option and has one refused. `ReferenceAgentTests` runs `tests/reference_agent_tests.py`, which plays each provider from a local HTTP server: the three stream formats, the thinking each provider must be handed back, the model list, and a setting chosen over ACP reaching the request. It also covers the default index and rules folders under `.crlf`, and two rules folders with the first winning a shared name. The last two need `python` on `PATH`. `ProjectTests` covers what the editor does with an agent entry before it starts: the merge of `agents` across files, the `windows` and `wsl` command lines, and `${appDir}` and `${programDir}` as `/mnt` paths.
