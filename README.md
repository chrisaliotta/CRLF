# CRLF

A lightweight tabbed text editor for Windows built with Qt Widgets, intended as a
minimalist replacement for Notepad and Notepad++. It provides syntax highlighting driven by
bundled JSON language definitions and collapsible (foldable) text regions. Users can also define
custom menu commands that will run scripts or shell commands.

## Features

### Editing

- Tabbed documents. The `+` button opens an untitled tab. Drag tabs to reorder them; a right-click offers Pin, Close, Close Others, Close to the Right, and Close All. Pinned tabs stay on the left, are marked with a diamond, and are skipped by the batch close commands. Closing the last document opens a fresh untitled tab
- Inactive tab titles are muted and their close buttons stay hidden until the tab is hovered or selected. A modified document's title is italic until it is saved
- Save and Save As. Scoping a folder sets the directory Save As suggests; a successful save remembers that file's directory for the next one
- Find and replace along the top of the window, with wrap-around, match highlighting, and Find All results that jump to each match. Go to Line is Ctrl+G
- Word wrap, and zoom in, out, or reset (Ctrl+0)
- Line-number gutter with clickable fold markers. Fold All and Unfold All are on the View menu
- Status bar with cursor position, language, encoding, and line endings
- Encoding (UTF-8 and UTF-16, with BOM detection) and CRLF/LF preservation on save
- One instance per user. Opening a file from Explorer, or passing paths on the command line, forwards them to the window that is already running
- Session persistence, on by default: unsaved edits, open tabs (including pins), and cursor positions survive restarts and crashes. Snapshots run every 30 seconds and on exit. Toggle it with File > Remember Unsaved Edits

### Explorer

File > Open Folder (Ctrl+Shift+O), or View > Explorer (Ctrl+B). The tree is scoped to one folder.

- Click a file to open it, or a folder to expand it. The header scopes up to the parent and expands or collapses the whole tree
- Listings are read off the UI thread. The panel shows Loading… until the folder's contents arrive, and an expanded folder shows the same state on its row
- Context menu: scope to the folder or its parent, New File (the name is typed on the new row), Reveal in File Explorer, Cut, Copy, Paste, Rename, and Delete. Windows files go to the Recycle Bin; files on a WSL share are deleted directly
- Cut rows are faded. Dragging a file or folder onto another folder asks before the move, and holding the drag over a collapsed folder expands it
- Hide Dot Folders hides directories whose names start with `.` (`.git`, `.cursor`, and so on). Dot files such as `.crlf` stay visible
- The chosen folder is restored on the next launch

### WSL

File > Open WSL Folder… picks a distro and a Linux path (the distro's home is the default) and scopes the explorer to `\\wsl.localhost\<Distro>\…`. The tooltip shows the Linux path. Parent-folder and project search stop at the distro root.

A `.crlf` command whose working directory is inside that share runs through `wsl.exe` in the distro, instead of `cmd.exe`. Stop ends the Linux process group as well as the Windows process tree.

### Syntax and folding

Highlighting is driven by bundled JSON definitions:

- C++, C#, Python, Bash (`.sh`), JSON, XML/HTML, SQL, SQL Run (`.sqlrun`), Markdown, `.crlf` command files, and plain text
- Folding follows the language: brace and bracket delimiters, indentation, or Markdown headings. Fenced code blocks in Markdown use the inner language's rules
- A line or block comment, string, or keyword match inside a string is only colored when the language says it should be

### Hex

View > Hex Editor (Ctrl+Shift+H) swaps the current tab between text and a hex view of the same bytes. View > Byte Columns… sets the row width from 1 to 64 (16 by default) for every hex view. The cursor offset and the file size are shown in the status bar.

### Project commands

A `.crlf` file in the explorer folder, or in a parent folder up to the drive or distro root, adds commands to the menu bar. [docs/commands.md](docs/commands.md) describes the file, the menus, and the variables. Each entry is a JSON object:

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

- `name` and `command` are required. `cwd` is optional and is resolved against the `.crlf` directory. `menu` is an optional path from the menu bar (`"Tools/Build"` or `["Tools", "Build"]`); without it the command sits on Run
- `key` is an optional function key from F1 through F35, with no modifiers. The first command to claim a key keeps it
- The command string may use `${file}`, `${fileName}`, `${fileBase}`, `${fileExt}`, `${fileDir}`, `${relativeFile}`, `${line}`, `${column}`, `${selection}`, `${language}`, `${project}`, `${folder}`, and `${cwd}`, filled from the focused document when the command starts
- Output opens in a panel at the bottom of the window, one log per command. Clear wipes the log that is showing. Stop is shown only while that command's process is still running, and it kills the process tree. Closing the panel hides it and leaves the process running

### Appearance

Dark, Cool Dark, and light themes (View > Theme). Cool Dark uses the blue-black of the application icon. The editor font is the bundled Ubuntu Mono, so the window looks the same on machines that do not have those fonts installed. Editor scrollbar handles grow under the pointer, and the tracks stay unpainted.

## Building

Requires Qt 6 (tested with 6.11.1 MinGW 64-bit) and CMake 3.16+.

```
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64
cmake --build build
```

Or open `CMakeLists.txt` in Qt Creator and press Run.

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
Running a newer installer upgrades an existing install in place; user settings
and sessions in `%APPDATA%\CRLF` are kept.

## Architecture

| Directory | Contents |
| --- | --- |
| `Types.h` | Types shared across module boundaries (`TokenRole`, `FoldMode`, `FoldPair`) |
| `app/` | `MainWindow`, `TabWorkspace` (tab strip, pins, documents), `ExplorerPanel` and `ExplorerModel`, `CommandRunner`, `OutputPanel`, `Actions`, `FindBar` |
| `editor/` | `CodeEditor` (QPlainTextEdit subclass), `Gutter`, `FoldController`, `FoldingLayout`, `BlockData` |
| `hex/` | `HexView` |
| `syntax/` | JSON-driven `LanguageDefinition`/`LanguageRegistry`, `Theme`, `Highlighter` |
| `io/` | `DocumentFile` (load/save, encoding, line endings), `SessionStore`, `ProjectCommands`, `WslPath` |
| `resources/languages/` | One JSON file per language definition |
| `resources/themes/` | Dark and light theme JSON |
| `packaging/` | Inno Setup installer script template |

The highlighter is the single authority on fold levels: only the lexer knows
whether a brace sits inside a string or comment, so it computes a per-block
`foldingIndent` (stored in `BlockData`) which the fold controller and gutter
consume. Brace depth is bit-packed into the highlighter's block state so edits
cascade rehighlighting exactly as far as needed.

## Adding a language or theme

Drop a JSON file into the program folder and restart. Languages go in `styles`,
themes in `themes`. A dev build reads those folders next to `CRLF.exe`; an
installed copy reads them from the install folder, one level above `bin`
(`C:\Program Files\CRLF\styles` and `themes`), so an upgrade that replaces
`bin` leaves them in place. A file whose `name` matches a built-in language or
theme replaces it; any other name is added to the Language or Theme menu.

The same files can instead be compiled in: drop a language into
`resources/languages/` (or a theme into `resources/themes/`), register it in
`resources/resources.qrc`, and rebuild. See `cpp.json` for the language schema:
keywords, types, string/comment delimiters, regex patterns per token role, and
a folding mode (`delimiter` with pairs, `indent`, or `heading`). Set `"ignoreCase": true`
for languages whose keywords and types are case-insensitive (see `sql.json`), and
`"insideStrings": true` on a pattern to also color its matches inside quoted strings
(see the `@@variable` rule in `sqlrun.json`).
