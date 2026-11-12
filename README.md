# CRLF

A lightweight tabbed text editor for Windows built with Qt Widgets, intended as a
minimalist replacement for Notepad and Notepad++. It provides syntax highlighting driven by
bundled JSON language definitions and collapsible (foldable) text regions. Users can also define
custom menu commands that will run scripts or shell commands.

## Features

### Editing

- Tabbed documents. The `+` button opens an untitled tab. Drag tabs to reorder them; a right-click offers Pin, Close, Close Others, Close to the Right, and Close All. Pinned tabs stay on the left, are marked with a diamond, and are skipped by the batch close commands. Closing the last document opens a fresh untitled tab, and that tab is Untitled 1 again
- Inactive tab titles are muted and their close buttons stay hidden until the tab is hovered or selected. A modified document's title is italic until it is saved
- Save and Save As. Scoping a folder sets the directory Save As suggests; a successful save remembers that file's directory for the next one
- Find and replace along the top of the window, with wrap-around, match highlighting, and Find All results that jump to each match. Go to Line is Ctrl+G
- Tab follows the indentation already used above the cursor. When no line shows one, a `.crlf` file sets spaces or a tab character, the width, and a different rule per file extension. Shift+Tab removes one level. `indent.keep` copies the prior line's own indentation when you press Enter
- Shift+Home selects back to the line's first non-whitespace character. Pressing it again extends the selection to column 0, and a third press returns to the indentation
- Word wrap, and zoom in, out, or reset (Ctrl+0)
- Ctrl+click a type, function, method, component, or variable to jump to its definition. Ctrl+hover underlines a name that can be opened. `"navigation": false` in a `.crlf` file turns the links off. Opening a definition in another file selects that file in the explorer
- Stylesheet classes, ids, custom properties, Sass variables, mixins, and keyframes link to their definitions, including a class named in a `class` or `className` attribute. A package import, or a stylesheet name the project does not define, is drawn in a different color and opened from `node_modules` when clicked. `"externalNavigation": false` in a `.crlf` file turns those external links off
- External code definitions: a class, enum, interface, type, function, or constant imported from a package opens its declaration under `node_modules` (following re-exports and `@types`). `React.FC` on a default import and `z.infer` on `import * as z` open that member. An undefined PascalCase name such as `HTMLDivElement`, `Promise`, or `JSX.Element` opens its ambient declaration in `@types` or TypeScript's `lib` files
- React: class strings inside `className={...}` and helpers such as `clsx`, `cn`, `cva`, and `twMerge` are class names, and `styles.card` on an imported CSS module opens `.card` (or `.card-name` for `cardName`) in that file
- Tailwind: a utility class opens the theme variable it reads, such as `bg-brand-500` to `--color-brand-500` and `p-4` to `--spacing`, in the project's `@theme` or in the `tailwindcss` package. A breakpoint such as `md:` opens `--breakpoint-md`. `@utility` names and `@apply` classes link too, and a Tailwind 3 project falls back to the key in `tailwind.config`. pnpm layouts, including packages kept only under `node_modules/.pnpm`, are followed
- Line-number gutter with clickable fold markers. View > Line Numbers hides the numbers and leaves the fold markers. Fold All and Unfold All are on the View menu
- Status bar with cursor position, language, encoding, and line endings
- Encoding (UTF-8 and UTF-16, with BOM detection) and CRLF/LF preservation on save
- One instance per user. Opening a file from Explorer, or passing paths on the command line, forwards them to the window that is already running
- Session persistence, on by default: unsaved edits, open tabs (including pins), and cursor positions survive restarts and crashes. Snapshots run every 30 seconds and on exit. Toggle it with File > Remember Unsaved Edits

### Explorer

File > Open Folder (Ctrl+Shift+O), or View > Explorer (Ctrl+B). The tree is scoped to one folder.

- Click a file to open it, or a folder to expand it. The header scopes up to the parent and expands or collapses the whole tree
- Listings are read off the UI thread. The panel shows Loading… until the folder's contents arrive, and an expanded folder shows the same state on its row
- The tree follows the folder. A file or folder added, removed, or renamed by the editor, an agent, a command, or another program appears without a refresh, and the tree stays expanded and selected as it was. A folder on a WSL share, where Windows reports no changes, is read again every few seconds while the explorer is showing
- Context menu: scope to the folder or its parent, New File and New Folder (the name is typed on the new row), Reveal in File Explorer, Cut, Copy, Paste, Rename, and Delete. Windows files go to the Recycle Bin; files on a WSL share are deleted directly
- Cut rows are faded. Dragging a file or folder onto another folder asks before the move, and holding the drag over a collapsed folder expands it
- Hide Dot Folders hides directories whose names start with `.` (`.git`, `.cursor`, and so on). Dot files such as `.gitignore` stay visible. The project's `.crlf` folder is a dot folder, so it is hidden with the rest. A `hide` list in a `.crlf` file drops other names from the tree, such as `node_modules` or `*.log`
- File icons come from a theme in the `icons` folder (View > File Icons). The shipped themes are Seti and Material. A file uses the theme's file name, then its file extension, then the VS Code language id from `icons/languages.json` (`jsx` is `javascriptreact`, `md` is `markdown`). Another theme is a subdirectory with a VS Code icon-theme JSON file; image paths and font files are resolved next to that JSON. A folder with no `name` uses the directory name. An `icons` object in a `.crlf` file can name the theme for that project and add file, extension, and directory rules that reuse glyphs from any installed theme
- View > File Icons > Templates has a Rules tab that lists every icon key, and a Mappings tab that adds a file, extension, or folder association. A pattern may contain `*` or `?`. A folder mapping can set a separate open icon. Double-click a rule to copy its key
- A `.crlf` file can define virtual folders that group files without creating a directory. A group can be limited to one path or applied in its subfolders. A group with no matching files is hidden unless `showIfEmpty` is true. New Virtual Folder on the explorer menu creates one, and dragging a file onto it records the file in a workspace `.crlf` file. Changing those rules leaves the explorer expanded where it was. See [docs/commands.md](docs/commands.md)
- The chosen folder is restored on the next launch

### Source control

`"git": true` in a `.crlf` file tracks the git repositories in the scoped folder. `"git": { "depth": 2 }` also finds nested repositories up to two folder levels below it. See [docs/git.md](docs/git.md).

- View > Source Control (Ctrl+Shift+G) lists each repository with its branch, ahead and behind counts, merge conflicts, staged changes, and working changes. Stage, unstage, discard, and commit from there; Ctrl+Enter commits the message
- Changed files are colored in the explorer with a letter (U, A, M, R, D, or ! for a conflict), and folders show a dot. The status bar shows the branch of the file in front; clicking it opens the branch menu: switch, new branch, merge into current, delete
- An editor on a tracked file draws gutter bars beside lines that differ from the staged copy, and they follow typing
- The Diff tab compares two versions side by side with both sides highlighted: working copy against the index, the index against `HEAD`, or a commit from File History against its parent. Git > Open Changes (Ctrl+Alt+D) compares the file in front
- Fetch, Pull (fast-forward only), Push, Stash, and Pop Stash are on the panel and the Git menu. Each operation's output is in the Output log `git: <folder>`, which opens when an operation fails
- A repository inside a WSL distro runs that distro's git

### Agent

View > Agent (Ctrl+Shift+A) opens a chat with an AI coding agent. The editor holds none of the AI: an agent is a program listed under `agents` in a `.crlf` file, and CRLF speaks the [Agent Client Protocol](https://agentclientprotocol.com) with it. See [docs/agent.md](docs/agent.md).

- `scripts/agent/crlf_agent.py` ships beside the program and talks to OpenAI, Anthropic, xAI Grok, Ollama, LM Studio, or any OpenAI-compatible server. The key comes from a key file (`--api-key-file`) or the provider's environment variable. Claude Code behind its ACP adapter, and other ACP agents, work the same way
- The panel shows whatever settings the agent offers. The shipped agent offers the provider's models, reasoning level, verbosity, Anthropic's thinking budget, the reply size, and a context budget. Each choice is remembered per agent
- Thinking, in the panel's header, shows the model's thinking in full as it streams: Claude's extended thinking, and the reasoning summary of an OpenAI reasoning model
- The agent reads files through the editor, so it sees unsaved edits. Each write is shown as a card with the lines it changes; Open Diff shows it in the Diff tab, and nothing is saved until Accept
- Commands the agent runs ask first, appear on the Processes page, and write to an Output log. A server or a watcher can be left running in the background and stopped later. The agent can also see the open files, the Problems list, git status, and the terminal's text
- The shipped agent finds code three ways: by a description of what it does (`codebase_search`), by regular expression (`grep_search`), and by file name (`file_search`). `--index grep|fts|vector` picks what the first one searches. `fts`, the default, is a full-text index in SQLite that needs no model. `vector` ranks by meaning with embeddings from a local model (`--embed-provider local` or `ollama`) or a remote one, which receives the project's code. The index lives in `.crlf/data` in the project
- Rule files in `.crlf/rules` tell the agent how the project wants work done. An entry can also name the application's rules folder, shared by every workspace. The Context setting, or `--context-budget`, caps how large a conversation grows before old tool results are shortened
- `permissions` on each agent sets `read`, `write`, and `terminal` to `allow`, `ask`, `review`, or `deny`. A path outside the agent's folder always asks. Deleting a file counts as a write and asks first

```json
"agents": [
  { "name": "Claude", "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider anthropic --model claude-sonnet-4-5 --api-key-file .crlf/keys/anthropic --index fts" },
  { "name": "Local", "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider ollama --model qwen3-coder --index vector --embed-provider ollama --embed-model nomic-embed-text" }
]
```

The model names are examples to replace. [docs/agent-tools.md](docs/agent-tools.md) has a worked example of each tool, each index, each embedding choice, the rules folder, and the settings that keep a session cheap.

### WSL

File > Open WSL Folder… picks a distro and a Linux path (the distro's home is the default) and scopes the explorer to `\\wsl.localhost\<Distro>\…`. The tooltip shows the Linux path. Parent-folder and project search stop at the distro root.

A command whose working directory is inside that share runs through `wsl.exe` in the distro, instead of `cmd.exe`. Its path variables are Linux paths there, with `${programDir}` and `${appDir}` given as `/mnt/<drive>/...`, and `command` can be an object with a `windows` line and a `wsl` line. Stop ends the Linux process group as well as the Windows process tree. See [docs/crlf.md](docs/crlf.md#windows-and-wsl).

### Syntax and folding

Highlighting is driven by bundled JSON definitions:

- C++, C#, TypeScript/JavaScript, Python, Bash (`.sh`), JSON, HTML, CSS, SCSS, Sass, Tailwind CSS, XML, SQL, SQL Run (`.sqlrun`), Markdown, `.crlf` settings files, and plain text
- Folding follows the language: brace and bracket delimiters, indentation, or Markdown headings. Fenced code blocks in Markdown use the inner language's rules
- A line or block comment, string, or keyword match inside a string is only colored when the language says it should be

### Hex

View > Hex Editor (Ctrl+Shift+H) swaps the current tab between text and a hex view of the same bytes. View > Byte Columns… sets the row width from 1 to 64 (16 by default) for every hex view. The cursor offset and the file size are shown in the status bar.

### Project commands

Every `<name>.crlf` file in the explorer folder, or in the nearest parent folder that has one, up to the drive or distro root, is a settings file of the project. `workspace.crlf` is the usual name. The files are merged in name order and the first definition wins, so a `local.crlf` can override the shared file.

- A folder with no such file uses the active application file in `%APPDATA%\CRLF\CRLF`. `application.crlf` ships with the shipped scripts and four agents set up, and `blank.crlf` sets nothing. View > Edit Defaults > Application Settings chooses which file in that folder is active and opens it. A workspace replaces it, or takes it in with `"inherit": true` or a list of keys
- The folder named `.crlf` beside the settings files holds the agent's index (`data`), rule files (`rules`), key files (`keys`), and the project's own scripts. The application folder has one too, and `${appDir}` names it in a command
- A file named exactly `.crlf`, from an earlier version, is renamed to `workspace.crlf` the first time its project loads
- File > Copy Application Files to Workspace copies the active application file into the scoped folder as `application.crlf`, and the shipped and application scripts and the application's rules into its `.crlf` folder. A folder with no settings of its own also gets an empty `workspace.crlf`, which is read first and wins. Key files are never copied

[docs/crlf.md](docs/crlf.md) is the list of keys and the merge rules. [docs/commands.md](docs/commands.md) describes the menus and the variables. A bare array is only commands; groups, hide, icons, indent, navigation, syntax, autocomplete, agents, and git have to live in an object. Each command is a JSON object:

```json
[
  {
    "name": "Build",
    "command": "cmake --build build",
    "cwd": "build",
    "menu": "Tools/Build",
    "key": "F5"
  }
]
```

- `name` and `command` are required. `command` is a string, or an object with `windows` and `wsl` for an entry used in both environments. `cwd` is optional and is resolved against the project root. `menu` is an optional path from the menu bar (`"Tools/Build"` or `["Tools", "Build"]`); without it the command sits on Run
- `key` is an optional function key from F1 through F35, with no modifiers. The first command to claim a key keeps it
- The command string may use `${file}`, `${fileName}`, `${fileBase}`, `${fileExt}`, `${fileDir}`, `${relativeFile}`, `${line}`, `${column}`, `${selection}`, `${buffer}`, `${terminal}`, `${language}`, `${project}`, `${folder}`, `${cwd}`, `${programDir}`, and `${appDir}`, filled from the focused document when the command starts. `${buffer}` and `${terminal}` are paths to temporary UTF-8 files: the editor's current text, and the current terminal's plain text. [docs/commands.md](docs/commands.md) lists each name
- Output opens in a panel at the bottom of the window, one log per command. Clear wipes the log that is showing. Stop is shown only while that command's process is still running, and it kills the process tree. Closing the panel hides it and leaves the process running
- Terminal sessions are listed in the box above the terminal. Double-click a session's name to rename it; Enter keeps the name, Escape cancels, and an empty name restores the shell's own label
- Saving a `.crlf` file in use, or a script listed under a service's `watch` key, restarts autocomplete and runs the syntax checks again. See [docs/crlf.md](docs/crlf.md)
- `"syntax"` in the same file runs an external script against the editor buffer and marks what it reports. The Problems page lists those marks by directory and file. [docs/commands.md](docs/commands.md) describes the key. `scripts/syntax.sh` is the Linux script that drives `clang`, `clang++`, C# when `csc` or the .NET SDK is installed, and JavaScript when `node` is installed

### Appearance

Dark, Cool Dark, and light themes (View > Theme). Each theme's `terminal` colors are the ANSI palette in the terminal. Cool Dark uses the blue-black of the application icon. The editor font is the bundled Ubuntu Mono, so the window looks the same on machines that do not have those fonts installed. Editor scrollbar handles grow under the pointer, and the tracks stay unpainted.

## Building

Requires Qt 6 (tested with 6.11.1 MinGW 64-bit) and CMake 3.16+.

```
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64
cmake --build build
```

Or open `CMakeLists.txt` in Qt Creator and press Run.

The unit tests build with the program (`-DCRLF_BUILD_TESTS=OFF` skips them). Run them with Qt's `bin` folder on `PATH`; `GitClientTests` needs `git` too, and `AgentSessionTests` and `ReferenceAgentTests` need `python`:

```
ctest --test-dir build --output-on-failure
```

## Releasing

The Windows installer is built with [Inno Setup](https://jrsoftware.org/isinfo.php)
(`winget install JRSoftware.InnoSetup`). The version lives only in
`project(CRLF VERSION ...)` in `CMakeLists.txt`; bump it there for each release.

```
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64
cmake --build build/release
cmake --install build/release
& "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" build/release/CRLF.iss
```

`cmake --install` stages a self-contained deployment (Qt and MinGW runtime DLLs)
in `build/release/dist/CRLF`, and CMake generates `build/release/CRLF.iss` from
`packaging/CRLF.iss.in`. The result is `build/release/installer/CRLF-<version>-Setup.exe`.
Running a newer installer upgrades an existing install in place. The session and
`application.crlf` in `%APPDATA%\CRLF\CRLF` are kept. The `application.crlf` beside
the program is only the first-run source and is replaced.

## Architecture

| Directory | Contents |
| --- | --- |
| `Types.h` | Types shared across module boundaries (`TokenRole`, `FoldMode`, `FoldPair`) |
| `app/` | `MainWindow`, `TabWorkspace` (tab strip, pins, documents), `ExplorerPanel`, `ExplorerModel`, and `FolderWatch` (changes on disk under the scoped folder), `ProcessLaunch` (how every child process starts), `JsonLineProcess` (a long-lived JSON-lines child), `CommandRunner`, `OutputPanel`, `Actions`, `FindBar`, source control: `GitClient`, `SourceControl`, `SourceControlPanel`, `DiffView`, and the agent: `AgentSession`, `AgentTerminals`, `AgentHost`, `AgentController`, `AgentPanel` |
| `agent/` | No widgets: JSON-RPC (`JsonRpc`), the Agent Client Protocol messages (`AcpTypes`), and the access check (`AgentPolicy`) |
| `git/` | No widgets: git status, branch, and log parsers (`GitParse`), repository search (`GitDiscovery`), and the line diff (`LineDiff`) |
| `scripts/agent/` | The reference agent: the ACP loop (`crlf_agent.py`), the model providers (`providers.py`), its tools (the `tools/` package), the code index (`index.py`), the embedding clients (`embeddings.py`), and the history budget (`history.py`) |
| `tests/` | `GitTests` (parsers, search, diff), `GitClientTests` (a real git against a temporary repository), `AgentTests` (the access policy and protocol messages), `AgentSessionTests` (a whole turn with `fake_acp_agent.py`), `ProjectTests` (the `.crlf` files: merging, discovery, the legacy rename, saving, command variables, the application files, and the copy into a workspace), `ExplorerModelTests` (the tree following a real folder), and `reference_agent_tests.py` (the shipped agent against scripted providers) |
| `editor/` | `CodeEditor` (QPlainTextEdit subclass), `Gutter`, `FoldController`, `FoldingLayout`, `BlockData`, and definition navigation: `SymbolIndex`, `ImportAliases`, `ExternalLookup`, `TailwindNames` |
| `hex/` | `HexView` |
| `syntax/` | JSON-driven `LanguageDefinition`/`LanguageRegistry`, `Theme`, `Highlighter` |
| `io/` | `DocumentFile` (load/save, encoding, line endings), `SessionStore`, `ProjectCommands` (finding, reading, and saving the `.crlf` files), `ProjectMerge` (how several files become one, and `inherit`), `WorkspaceSeed` (Copy Application Files to Workspace), `CommandVariables` (the `${...}` names and their WSL paths), `WslPath`, `NamePattern` (the name and wildcard rule of `hide`, groups, and icons) |
| `resources/languages/` | One JSON file per language definition, copied to `styles` on build and install |
| `resources/themes/` | Theme JSON, copied to `themes` on build and install |
| `resources/icons/` | File-icon themes, copied to `icons` on build and install |
| `resources/fonts/` | Ubuntu fonts, copied to `fonts` on build and install |
| `packaging/` | Inno Setup installer script template |

The highlighter is the single authority on fold levels: only the lexer knows
whether a brace sits inside a string or comment, so it computes a per-block
`foldingIndent` (stored in `BlockData`) which the fold controller and gutter
consume. Brace depth is bit-packed into the highlighter's block state so edits
cascade rehighlighting exactly as far as needed.

Definition navigation (Ctrl+hover and Ctrl+click) is documented, with the
reasoning behind its threading, lookup order, and limits, in
[docs/navigation.md](docs/navigation.md).

## Adding a language or theme

Languages, themes, file icons, and fonts are files next to the program, not
data inside the executable. A dev build reads `styles`, `themes`, `icons`, and
`fonts` beside `CRLF.exe` (CMake copies them there after each build). An
installed copy reads them from the install folder, one level above `bin`
(`C:\Program Files\CRLF\styles`, `themes`, `icons`, and `fonts`), and the
installer ships the copies from `resources/`. An upgrade that replaces `bin`
leaves these folders in place and refreshes the files that share a shipped name.

Drop a JSON file into `styles` or `themes` and restart. A file whose `name`
matches one already loaded replaces it; any other name is added to the Language
or Theme menu. To change a shipped definition, edit it under `resources/` and
rebuild. See `cpp.json` for the language schema:
keywords, types, string/comment delimiters, regex patterns per token role, and
a folding mode (`delimiter` with pairs, `indent`, or `heading`). Set `"ignoreCase": true`
for languages whose keywords and types are case-insensitive (see `sql.json`), and
`"insideStrings": true` on a pattern to also color its matches inside quoted strings
(see the `@@variable` rule in `sqlrun.json`). An optional `symbols` array,
`{ "regex", "capture" }`, names local type definitions for Ctrl+hover and
Ctrl+click. `capture` defaults to 1, the patterns are matched outside strings
and comments, and `ignoreCase` applies to them too. `"modules": true` also
follows import and export declarations, so a JSX component or an imported type
jumps to the file it comes from. A directory alias in that import is resolved
too: `tsconfig` or `jsconfig` `paths`, a Vite `resolve.alias`, and a `/@utils`
or `@utils` path that names a folder in the project. An imported function,
method, component, constant, or type jumps to that export, and the same kinds
of names defined in the open file jump to their declaration there.
`"navigation": false` in a `.crlf` file turns the links off; leaving the key out leaves them on.
`"stylesheet": true` lets a name contain `-` and keeps a class's `.` or an id's `#`
in its key (see `css.json`). `"classAttributes": true` reads `class`, `className`,
and `id` values as those stylesheet names (see `html.json`). `"prefix"` on a
symbol is added to the captured name, such as `@` for a Sass mixin.
`"local": true` on a symbol keeps that name out of the project-wide index,
which is how a variable such as `i` stays in the open document. `"params": true`
means the capture is a parameter list.
