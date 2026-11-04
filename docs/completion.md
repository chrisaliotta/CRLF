# Completion: design notes

This document records how external completion works and why the editor does not speak to `clangd` or Roslyn itself.

User-facing behavior is in [commands.md](commands.md). Threading is in [processes.md](processes.md).

## Why a session

A check is one process per buffer: standard input is the text, standard output is a JSON array, and the process exits. `clangd` and the C# language server spend their time building an index and a preamble. Starting one for every keystroke would not complete in time. `complete` in `.crlf` is the same kind of command string as `check`, and the process stays up for the project.

The editor writes one JSON object per line and reads one JSON object per line. `scripts/complete.py` is the only place that speaks LSP. Adding a language means teaching that script which server to start. The popup, the document, and the orchestrator stay as they are.

## Where it runs

`CompletionSession` is moved onto the orchestrator thread with `CheckRunner`. `QProcess` is created there. `start`, `request`, and `stop` are queued from the UI thread and return. The reply signal is queued back, and `MainWindow::applyCompletion` is the only place that fills the popup.

The 100ms timer stays on the editor. Ctrl+Space asks for names. `.`, `->`, and `::` start the timer for names. While the caret is inside parentheses, the same timer asks for the signature instead, and the list stays closed. The timer does not kill or start a process. It asks `MainWindow` to queue a request.

A completion item carries `documentation`, the summary text from the server. The list row is the label and `detail`. The selected row shows `documentation` under the list.

A signature request sets `"kind": "signature"`. The reply is `signatures`, each with `label`, `parameters`, and `active`. The editor draws the active signature above the caret and colors `parameters[active]` with the theme's function color. An empty `signatures` array hides the hint. Choosing a completion that leaves the caret inside `()` asks for the signature of that call.

The request carries the buffer. A tool that opened `${file}` would complete the saved copy and miss unsaved edits. When the session is running in a WSL distro, a `\\wsl.localhost` path is sent as the Linux path (`/home/...`), because the language server is a Linux process and cannot open the Windows UNC path. `revision` is `QTextDocument::revision()` at send time. A reply whose id or revision is older than the buffer is dropped.

The session is one `Hold` record shown as `autocomplete` on the Processes page. It stays until the command is turned off, the project changes, or the process exits. Stopping it closes the kill-on-close job and calls `QProcess::kill()` on the orchestrator thread. It does not wait.

## The script

`scripts/complete.sh` execs `python3` on `scripts/complete.py`. C and C++ use `clangd` when it is on `PATH`. C# uses `OmniSharp -lsp` when that is on `PATH`, otherwise `Microsoft.CodeAnalysis.LanguageServer.dll` from the newest .NET SDK. Any other extension replies with an empty item list. A missing server replies once with `error` and then with an empty list, so one language without a toolchain does not stop completion for the others.

`compile_commands.json` and the C# project are the server's concern. The editor does not look for them.
