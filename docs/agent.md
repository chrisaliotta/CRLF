# Agent: design notes

This document records how the Agent panel works and why the editor holds none of the AI logic.

The editor is a client of the [Agent Client Protocol](https://agentclientprotocol.com) (ACP). An agent is a program named in `.crlf`. It owns everything an agent decides: the model and its provider, the API key, the prompt, the tool loop, and the conversation. The editor shows the conversation and answers the agent's requests for files, commands, and its own state. Swapping OpenAI for Anthropic, Grok, or a local model is a different command line. Nothing in the editor changes.

The setup keys are in [crlf.md](crlf.md#agents). Threading is in [processes.md](processes.md).

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

View > Agent (Ctrl+Shift+A) opens the panel. The picker at its top lists the entries in file order. The chosen agent starts when the panel opens, or when the first message is sent, and runs until the project changes it, another agent is picked, or New starts a fresh session. One agent runs at a time.

## The panel

| Part | What it does |
| --- | --- |
| Picker | The `.crlf` agents. Choosing one starts a new session with it. The choice is remembered |
| New | Ends the session and starts another with an empty transcript |
| Status | Starting, ready, working, or the last error |
| Transcript | Your messages, the agent's replies as Markdown, its thinking, each tool call with its state, and its plan. A file named by a tool call is a link that opens it at that line |
| Cards | Questions waiting for you: a permission, a command, or a write to review. A card stays until it is answered or the turn ends |
| Prompt | Enter sends; Shift+Enter starts a new line. Send is Stop while a turn runs |
| Include the current file | Sends the file in front as a link, and the selection as text |

Stop sends `session/cancel`. Cards still open are answered as cancelled, and the agent ends the turn with `cancelled`.

## Permissions

Each request is checked once, where it enters the editor, by `AgentPolicy`. The paths an agent sends come from a model, so they are the one input the editor checks; nothing after that point does.

| Action | Covers | Default |
| --- | --- | --- |
| `read` | `fs/read_text_file`, open files, problems, git status, the terminal's text | `allow` |
| `write` | `fs/write_text_file` | `review` |
| `terminal` | `terminal/create`, typing into your terminal | `ask` |

`allow` answers at once. `ask` shows a card with Allow, Always Allow, and Reject. `review` is for writes: the card shows how many lines change, and Open Diff shows the change in the Diff tab. `deny` answers with an error, which the agent sees.

A path outside the session's working directory is never allowed without a card, whatever the setting. Always Allow and Accept All last for the session and apply only inside the working directory. A relative path is resolved against the working directory, and `..` is resolved before the check, so `src/../../secrets` is outside.

An agent's own `session/request_permission` is a card with the agent's buttons, such as Allow once and Always allow. Claude Code asks this way before it edits or runs anything.

## What the editor answers

| Method | Answer |
| --- | --- |
| `fs/read_text_file` | The open editor's text, unsaved edits included, otherwise the file read on the pool. `line` and `limit` select lines |
| `fs/write_text_file` | After review: an open file is replaced as one undo step and saved; any other file is written on the pool, keeping its encoding and line endings. A new file's folders are created |
| `session/request_permission` | The button you press, or `cancelled` |
| `terminal/create` | Starts the command and returns its id |
| `terminal/output`, `terminal/wait_for_exit`, `terminal/kill`, `terminal/release` | Answered on the orchestrator thread from the running command |

The editor also answers five methods of its own. ACP reserves names that start with `_` for these:

| Method | Answer |
| --- | --- |
| `_crlf/editor/state` | `files` (path and whether it has unsaved edits), and for the file in front `activeFile`, `line`, `column`, and `selection` |
| `_crlf/problems` | `problems`: each check span with `path`, `line`, `column`, `severity`, and `message` |
| `_crlf/git/status` | `enabled` and `repositories`: root, branch, head, upstream, ahead, behind, merging, and `changes` with git's two status letters |
| `_crlf/terminal/read` | `text`: the scrollback and screen of the terminal on screen |
| `_crlf/terminal/send` | Types `text` into that terminal, after a `terminal` check |

An agent that does not know these names never calls them. The reference agent offers them to its model only when `initialize` names the client `CRLF`.

## Writes

The agent's write waits for its reply while the card is open, so the agent does not run the tests against the old file. Accept writes the file and replies; Reject replies with an error the agent can act on. With `"write": "allow"` the write happens at once and git is the undo.

An open file is changed in its editor, so its undo history keeps the change and its tab never asks whether to reload. Line endings in the agent's text are dropped before the write; the file keeps the ones it had.

## Commands

`terminal/create` runs through `ProcessLaunch`, as a menu command does: `cmd.exe /d /s /c` on Windows, `bash -lc` inside a WSL distro, each in a kill-on-close job. The command and its arguments are joined into one line, with an argument quoted when it needs it. `outputByteLimit` keeps the end of the output. Each command is a row on the Processes page, `agent-terminal:<id>`, which Cancel stops, and an Output log named `agent: <command>`.

## Paths and WSL

An agent whose working directory is inside a WSL distro runs in that distro and sees Linux paths. The session converts `path` and `cwd` in the agent's requests to the `\\wsl.localhost` form, converts the editor's links and its own answers back, and gives `session/new` the Linux working directory.

## Where it runs

`AgentSession` lives on the orchestrator thread with its process, through the same `JsonLineProcess` that runs the autocomplete session. It sends `initialize` and `session/new`, sends each prompt, and passes every other request to the UI thread as an `AgentRequest`. `AgentHost`, on the UI thread, answers each method from a table of handlers, after `AgentPolicy`. A file it has to read or write goes to the pool. `AgentController` starts and stops the agent and carries each turn between the panel and the session. `AgentPanel` only draws what it is told and reports what you do.

The session is the Processes row `agent`. Cancel stops the agent; Reload starts a new session. A file listed under the agent's `watch` starts a new session when it is saved, so an edit to an agent script takes effect.

## The reference agent

`scripts/agent/crlf_agent.py` is an ACP agent in Python with no packages beyond the standard library.

| Provider | Server | Key |
| --- | --- | --- |
| `openai` | `https://api.openai.com/v1` | `OPENAI_API_KEY` |
| `anthropic` | `https://api.anthropic.com` | `ANTHROPIC_API_KEY` |
| `xai` | `https://api.x.ai/v1` | `XAI_API_KEY` |
| `ollama` | `http://localhost:11434/v1` | none |
| `lmstudio` | `http://localhost:1234/v1` | none |
| `openai-compatible` | `--base-url` | `--api-key-env`, when the server wants one |

Everything but Anthropic speaks the Chat Completions API, so `providers.py` has two adapters. `--base-url` moves any of them, and `--api-key-env` names another variable. `--max-tokens` caps a reply. A key that is not set fails `session/new`, so the panel says which variable is missing before the first message.

The model's tools are in `tools.py`. Files go through the editor: `read_file` sees unsaved edits, and `write_file` and `edit_file` go through review. `edit_file` replaces an exact passage that has to occur once. `run_command` is `terminal/create`, waits up to `timeout_seconds`, then reads the output and releases the terminal. `list_directory` and `search` read the disk in the agent's own process and skip dot folders and `node_modules`. `editor_state`, `problems`, `git_status`, and `read_terminal` are the `_crlf/` methods.

A turn ends when the model answers without a tool call, after 60 model replies (`max_turn_requests`), or when it is cancelled. Logs go to standard error, which is the Processes page log; `CRLF_STATUS:` lines set the row's Activity.

## Tests

`AgentTests` covers `AgentPolicy` and the reading of JSON-RPC messages and session updates. `AgentSessionTests` drives `tests/fake_acp_agent.py` through a whole turn: a file link, a read the editor answers, a command it runs, and the stop reason. It needs `python` on `PATH`.
