# Source control

CRLF tracks the git repositories in the scoped folder. It shows what changed, compares versions of a file, and runs the everyday operations: stage, commit, fetch, pull, push, branch, merge, and stash. Everything runs the `git` program; nothing is reimplemented. The `git` key in [crlf.md](crlf.md) turns it on.

```json
"git": { "enabled": true, "depth": 2 }
```

## Which repositories

The search starts at the folder the explorer is scoped to. When no folder is scoped, it starts at the folder that holds `.crlf`.

- The repository that contains that folder is tracked, even when its root is above it. Scoping `src` inside a checkout tracks the checkout.
- Folders up to `depth` levels below it are checked for a `.git` entry. A `.git` directory and the `.git` file of a submodule or worktree both count. A repository's own subfolders are searched too, so nested repositories are found. `depth` 0 checks only the scoped folder itself.
- The walk skips folders whose names start with `.`, `node_modules`, and names in the `.crlf` `hide` list.

The search runs again when the scoped folder, `depth`, or `hide` changes.

## Seeing changes

- **Source Control** (View > Source Control, Ctrl+Shift+G) lists every repository with its branch, how far it is ahead (↑) or behind (↓) its upstream, and its changes in three groups: Merge Conflicts, Staged Changes, and Changes. Clicking a file opens its diff. A conflicted file opens in the editor instead.
- **Explorer**: a changed file's name takes its change color, and its letter is drawn at the right of the row. U is untracked, A added, M modified, R renamed, D deleted, and ! conflicted. A folder shows a dot in the strongest color among the changes inside it.
- **Status bar**: the branch of the repository that holds the file in front, its ahead and behind counts, and how many files changed. Clicking it opens the branch menu.
- **Gutter**: an editor on a tracked file draws a bar beside each line that differs from the staged copy: green for added, blue for changed, and a red wedge where lines were deleted. The bars follow typing after a short pause.

The colors are the theme's `gitAdded`, `gitModified`, `gitDeleted`, and `gitConflicted` editor keys. A theme that leaves them out uses its terminal green, blue, red, and magenta.

## Comparing versions

The Diff tab shows two versions of one file side by side, with syntax highlighting on both. Removed rows are tinted on the left and added rows on the right. Blank padding rows keep unchanged lines level, so both sides scroll together. Only one Diff tab exists; opening another comparison replaces it.

| Opened from | Left | Right |
|---|---|---|
| A file under Changes | Staged copy (empty for an untracked file) | Working copy, including unsaved edits |
| A file under Staged Changes | Last commit (`HEAD`) | Staged copy |
| Git > Open Changes (Ctrl+Alt+D) | Staged copy | The file in front, including unsaved edits |
| A commit in File History | The commit's parent | The commit |

Alt+F5 and Shift+Alt+F5, or the arrows in the Diff tab header, move to the next and previous change. Open File opens the working copy.

Line endings are not differences. Text is compared after CRLF and LF are made the same, so a checkout with `core.autocrlf` does not mark every line.

## Operations

| Action | Where | git |
|---|---|---|
| Stage, Stage All | File or group menu | `add -- <paths>` |
| Unstage, Unstage All | File or group menu | `reset -q -- <paths>` |
| Discard Changes | File or group menu, after a confirmation | `restore --worktree -- <paths>`; untracked files `clean -f -- <paths>` |
| Commit | Commit button, or Ctrl+Enter in the message | `commit -F -`; the message goes in on standard input |
| Amend | Amend, then Commit | `commit -F - --amend` |
| Fetch | Panel, repository menu | `fetch --all --prune` |
| Fetch All | Panel header, Git menu | The same, in every repository |
| Pull | Panel, repository menu, Git menu | `pull --ff-only` |
| Push | Panel, repository menu, Git menu | `push`; the first push of a branch is `push --set-upstream origin <branch>` |
| Switch branch | Branch menu | `switch <branch>`; a remote branch is `switch --track <remote>/<branch>` |
| New Branch | Branch menu | `switch -c <name>` |
| Merge Into Current | Branch menu | `merge --no-edit <branch>` |
| Delete Branch | Branch menu, after a confirmation | `branch -d <name>` |
| Abort Merge | Repository menu, while merging | `merge --abort` |
| Stash, Pop Stash | Repository menu, Git menu | `stash push --include-untracked`, `stash pop` |
| File History | File menu, Git menu | `log -n 200 -- <path>` |

Commit with nothing staged asks whether to stage everything first (`add -A`). Pull only fast-forwards; a branch that has diverged reports git's message and is left as it was. A merge that stops on conflicts lists them under Merge Conflicts; fix each file, stage it, and commit, or choose Abort Merge.

The branch menu, the Git menu, and the panel's buttons act on one repository: the one holding the file in front, otherwise the one picked above the commit message. That picker is shown only when more than one repository is tracked.

Each operation writes the command and git's output to the Output log `git: <folder>`. A failed operation opens that log. The panel does not open for one that succeeded.

## Authentication

git never prompts: `GIT_TERMINAL_PROMPT=0` is set, so a missing credential fails with git's message instead of waiting on a pipe. On Windows, Git Credential Manager opens its own sign-in window. Inside a WSL distro, configure a credential helper in that distro.

## How it runs

The rules in [processes.md](processes.md) apply. Nothing touches git on the UI thread.

- `GitClient` lives on the orchestrator thread and starts each `git` through `ProcessLaunch::startProgram`, with arguments and no shell. A repository under `\\wsl.localhost` runs `git` inside that distro with `wsl.exe --exec`, so the distro's git, config, and credentials are used.
- Jobs for one repository run one at a time, in the order they were queued, because two git processes writing the same index fail on `index.lock`. Different repositories run side by side. A status read that is already waiting is not queued twice. After each operation, a status read of that repository runs before the next queued job.
- Each job is a record on the Processes page, keyed `git:<n>`. Cancel stops it. The Output panel's Stop stops the repository's running operation.
- `GIT_OPTIONAL_LOCKS=0` keeps status reads from taking the index lock away from a git the user runs at the same time. `GIT_LITERAL_PATHSPECS=1` keeps a file named with `*` or `[` from matching other files. WSLENV carries these into a distro.
- Status is read again when the window is activated, when a file is saved, when the explorer moves or renames something, after each operation, and from Refresh. Nothing watches `.git`; a watcher on a WSL share is unreliable, and an operation would fire it dozens of times.
- An editor's change bars compare it with the staged copy (`cat-file blob :<path>`). That copy is read when the editor opens, when its file is renamed, and when the repository's index or `HEAD` moves. The diff runs on the pool, 300ms after typing stops, and is dropped if the buffer changed in the meantime.
- `git/` holds the parts with no widgets: the status, branch, and log parsers, the repository walk, and the line diff, a linear-space Myers diff. `tests/GitTests.cpp` covers them, and `tests/GitClientTests.cpp` runs a real git against a temporary repository.
