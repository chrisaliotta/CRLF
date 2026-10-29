# CRLF

A lightweight tabbed text editor for Windows built with Qt Widgets, intended as a
minimalist replacement for Notepad and Notepad++. It provides syntax highlighting driven by
bundled JSON language definitions and collapsible (foldable) text regions.

## Features

- Tabbed editing: closable, reorderable tabs with dirty markers and save prompts
- Explorer panel (File > Open Folder, toggle with View > Explorer or Ctrl+B):
  a file tree scoped to the chosen folder; click a file to open it. Expand/collapse
  all, rescope to a subfolder or the parent, reveal in File Explorer, and cut, copy,
  paste, rename, or delete (to the Recycle Bin) from the context menu
- Syntax highlighting for C++, C#, Python, JSON, XML/HTML, SQL, SQL Run (`.sqlrun`), Markdown, and plain text
- Code folding: delimiter-based (braces/brackets) or indentation-based per language,
  toggled from the gutter, plus Fold All / Unfold All
- Line-number gutter with clickable fold markers
- Find and replace (top of the window) with wrap-around, match highlighting,
  and Find All results that jump to each match; Go to Line
- Dark, Cool Dark, and light themes (View > Theme). Cool Dark uses the
  blue-black of the application icon
- Word wrap toggle, zoom in/out/reset
- Status bar with cursor position, language, encoding, and line endings
- Session persistence (on by default): unsaved edits, open tabs, and cursor
  positions survive restarts and crashes, snapshotted every 30 seconds and on
  exit; toggle via File > Remember Unsaved Edits
- Encoding (UTF-8, UTF-16 with BOM detection) and CRLF/LF preservation on save
- Opens files passed on the command line

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
| `app/` | `MainWindow`, `TabWorkspace` (tab strip + documents), `Actions`, `FindBar` |
| `editor/` | `CodeEditor` (QPlainTextEdit subclass), `Gutter`, `FoldController`, `FoldingLayout`, `BlockData` |
| `syntax/` | JSON-driven `LanguageDefinition`/`LanguageRegistry`, `Theme`, `Highlighter` |
| `io/` | `DocumentFile` (load/save, encoding, line endings), `SessionStore` (session persistence) |
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
a folding mode (`delimiter` with pairs, or `indent`). Set `"ignoreCase": true`
for languages whose keywords and types are case-insensitive (see `sql.json`), and
`"insideStrings": true` on a pattern to also color its matches inside quoted strings
(see the `@@variable` rule in `sqlrun.json`).
