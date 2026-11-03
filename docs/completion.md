# Completion: design notes

This document records how external completion works and why the editor does not speak to `clangd` or Roslyn itself.

User-facing behavior is in [commands.md](commands.md). Threading is in [processes.md](processes.md).

## Why a session

A check is one process per buffer: standard input is the text, standard output is a JSON array, and the process exits. `clangd` and the C# language server spend their time building an index and a preamble. Starting one for every keystroke would not complete in time. `complete` in `.crlf` is the same kind of command string as `check`, and the process stays up for the project.

The editor writes one JSON object per line and reads one JSON object per line. `scripts/complete.py` is the only place that speaks LSP. Adding a language means teaching that script which server to start. The popup, the document, and the orchestrator stay as they are.

## Where it runs

`CompletionSession` is moved onto the orchestrator thread with `CheckRunner`. `QProcess` is created there. `start`, `request`, and `stop` are queued from the UI thread and return. The reply signal is queued back, and `MainWindow::applyCompletion` is the only place that fills the popup.

The 100ms timer stays on the editor. Ctrl+Space emits immediately. `.`, `->`, and `::` start the timer. Identifier characters restart it only while the list is open. The timer does not kill or start a process. It asks `MainWindow` to queue a request.

The request carries the buffer. A tool that opened `${file}` would complete the saved copy and miss unsaved edits. `revision` is `QTextDocument::revision()` at send time. A reply whose id or revision is older than the buffer is dropped.

The session is one `Hold` record named `complete` on the Processes page. It stays until the command is turned off, the project changes, or the process exits. Stopping it closes the kill-on-close job and calls `QProcess::kill()` on the orchestrator thread. It does not wait.

## The script

`scripts/complete.sh` execs `python3` on `scripts/complete.py`. C and C++ use `clangd` when it is on `PATH`. C# uses `OmniSharp -lsp` when that is on `PATH`, otherwise `Microsoft.CodeAnalysis.LanguageServer.dll` from the newest .NET SDK. Any other extension replies with an empty item list. A missing server replies once with `error` and then with an empty list, so one language without a toolchain does not stop completion for the others.

`compile_commands.json` and the C# project are the server's concern. The editor does not look for them.
