# Project commands

A project's `.crlf` files define its commands. CRLF finds those files, adds each command to the menu bar, and runs it when you choose it. The command string can include values from the document that is focused at the moment the command starts. Every key a file can carry, how several files merge, and the cases where a key is ignored, are in [crlf.md](crlf.md).

## Finding the files

CRLF looks for files with the `.crlf` extension, such as `workspace.crlf`. The search starts at the folder the explorer is scoped to. When no folder is scoped, it starts at the directory of the focused document. It walks parent folders and stops at the drive root, or at the distro root of a `\\wsl.localhost` path.

The first folder that holds a `<name>.crlf` file is the project root. Every such file in it is read, in name order, and merged; the earlier file wins a shared command name. When the walk finds no project root, CRLF uses `application.crlf` in the application folder, `%APPDATA%\CRLF\CRLF`. A workspace replaces those settings unless one of its files sets `inherit`. Saving a file in use, or switching to a document that resolves to a different project, reloads the menus. A command that is no longer defined is stopped. The rules are in [crlf.md](crlf.md#which-files-are-in-use).

View > Edit Defaults > Application Settings lists the application files and checks the active one; Edit Active File, on that menu, opens it as text. Themes and Syntax, under Edit Defaults, open the theme and syntax files beside the program. Saving the active application file reloads commands only while that file is in use. See [crlf.md](crlf.md#choosing-the-active-file).

Each file is a JSON array of commands, or an object with a `commands` array and the other keys. An entry needs `name` and `command`. A second entry with the same `name` is ignored, because the name identifies the running process and its output log. A file that is neither an array nor an object is skipped, and the status bar says "Couldn't read" and its name; the other files still apply.

```json
[
  {
    "name": "Format",
    "command": "clang-format -i \"${file}\"",
    "menu": "File"
  },
  {
    "name": "Debug",
    "command": "cmake --build build",
    "cwd": "build",
    "menu": "Build/Debug",
    "key": "F5"
  }
]
```

| Field | Required | Meaning |
|---|---|---|
| `name` | yes | Menu label, and the name of the process and its output log |
| `command` | yes | The shell command, as a string or as an object with `windows` and `wsl` (see [Running a command](#running-a-command)). Variables are filled in when it starts |
| `cwd` | no | Working directory, relative to the project root. The default is that folder |
| `menu` | no | Where the command appears on the menu bar. The default is Run |
| `key` | no | A function key, F1 through F35, with no modifiers |
| `output` | no | `editor` sends the focused editor's text to the process and replaces that editor with standard output. Any other value, or leaving it out, keeps output in the panel |

A `key` that is not a bare function key is ignored. When two commands ask for the same key, the first one in the merged list keeps it.

Run is shown only when at least one command in the files in use is placed in it.

## Virtual folders

`groups` is read only from the object form of a file. Each group is a virtual folder in the explorer. Nothing is created on disk. A file is claimed by the first group whose pattern matches its name, and directories are left where they are.

With no `path`, the group applies in the project root. `path` limits it to a folder under that directory. `recursive` also applies it in subfolders of that location. A group with no matching files is not shown, including in the directory named by `path`. `showIfEmpty` set to `true` shows the folder anyway.

```json
{
  "commands": [],
  "groups": [
    {
      "name": "Configuration",
      "icon": "config",
      "path": "src",
      "recursive": true,
      "showIfEmpty": false,
      "files": [".gitattributes", ".gitignore", "package.json", "*.config.js"]
    }
  ]
}
```

| Field | Required | Meaning |
|---|---|---|
| `name` | yes | The folder's label in the explorer |
| `files` | no | File names to fold into the folder. A pattern that contains `*` or `?` is a wildcard; any other pattern is an exact name |
| `icon` | no | Folder name passed to the file-icon theme. The default is `name`. View > File Icons > Templates lists every icon key |
| `path` | no | Folder under the project root. The default is the root itself |
| `recursive` | no | Also apply the group in subfolders of `path` |
| `showIfEmpty` | no | When `false` (the default), hide the folder wherever it has no matching files |

Right-click a folder and choose New Virtual Folder. Apply in subfolders is the `recursive` field, and it can be toggled on the virtual folder's menu. Drag a file from that same folder onto the virtual folder to add its name to `files`. The file stays where it is on disk. Saving a `.crlf` file in use, or either of those actions, reloads the groups. The actions write to a workspace file; which one, and what happens to a group inherited from `application.crlf`, is in [crlf.md](crlf.md#saving-from-the-editor).

## Icon theme

`icons` chooses the file-icon theme for this project and adds mappings on top of it. `theme` is the name of an installed theme, such as `Seti`. Leaving `theme` out keeps the choice in View > File Icons. Each rule maps an expression to a glyph from an installed theme. `kind` is `extension`, `file`, or `folder`. `pattern` is an exact name, or a wildcard when it contains `*` or `?`. `from` is the source theme and `icon` is a key in that theme. A folder may also set `iconOpen` for the expanded glyph; leaving it out keeps `icon` when the folder is open. View > File Icons > Templates lists every icon key and edits these mappings.

```json
"icons": {
  "theme": "Seti",
  "rules": [
    { "kind": "extension", "pattern": "*.test.js", "from": "Seti", "icon": "javascript" },
    { "kind": "file", "pattern": "readme.md", "from": "Seti", "icon": "readme.md" },
    { "kind": "folder", "pattern": "src*", "from": "Seti", "icon": "config", "iconOpen": "folderExpanded" }
  ]
}
```

## Hidden names

`hide` is a list of file and folder names to leave out of the explorer. A pattern that contains `*` or `?` is a wildcard. Any other pattern is an exact name. Matching ignores case. The files stay on disk. No name is built in: the `.crlf` folder is a dot folder and follows Hide Dot Folders on the explorer's menu.

```json
"hide": ["node_modules", "dist", "*.log"]
```

## Definition navigation

`navigation` turns Ctrl+hover and Ctrl+click definition links on or off. Leaving it out, or setting it to `true`, keeps the links. `false` stops the scan of open files and of the scoped folder.

```json
"navigation": false
```

`externalNavigation` controls links to code outside the project. With it on (the default), a name imported from a package, such as `useState` from `react`, and a stylesheet class or variable that the project does not define are underlined in the theme's function color. Ctrl+click reads that package's `package.json` and entry file under `node_modules`, or the stylesheets of packages the document imports, on a background thread. A member of a default or `* as` package import, such as `React.FC`, is searched in that package. A PascalCase name with no definition and no import, such as `HTMLDivElement` or `JSX.Element`, is searched in the `@types` packages and TypeScript's `lib` declaration files. A Tailwind utility class also searches the `tailwindcss` package for its theme variable, then a Tailwind 3 `tailwind.config` for the theme key. pnpm's linked packages, and those kept only under `node_modules/.pnpm/node_modules`, are found the same way. `false` leaves those names as plain text and never reads `node_modules`. Project definitions, including project stylesheets, still link. `node_modules` is never part of the folder scan.

```json
"externalNavigation": false
```

## Indent

`indent` sets what Tab inserts when the lines above the cursor do not show an indentation. `style` is `spaces` or `tab`. `size` is how many spaces a spaces-style Tab inserts, and how many columns a tab character is wide. With no `indent` key, that fallback is one tab character four columns wide.

When a line at or above the cursor is indented, Tab follows that line instead of the file rule. The current line counts when it is already indented; otherwise the prior line does. The walk stops at the first line that has text and no indent. A tab in that leading whitespace inserts a tab. Spaces insert the shallowest space run in the walk, so a line at four spaces and a line at eight insert four spaces. Shift+Tab removes one such level. A selection uses the same level for every highlighted line.

`keep` copies indentation onto the new line when Enter is pressed. The copy is the current line's own leading whitespace. A blank line uses the prior non-blank line. A line that is not indented stays at column zero; Enter does not invent a level from the file rule. Leaving `keep` out leaves Enter at column zero.

`types` overrides that for file extensions. A file with no extension is matched on its file name, so `Makefile` uses an extension entry of `makefile`. The first matching type wins. A selection indents each highlighted line; Shift+Tab removes one indent from those lines. A type that omits `keep` uses the value from the standard rule.

```json
"indent": {
  "style": "spaces",
  "size": 4,
  "keep": true,
  "types": [
    { "extensions": ["py"], "style": "spaces", "size": 4 },
    { "extensions": ["go", "makefile"], "style": "tab", "size": 4 }
  ]
}
```

## Menus

`menu` is a path that starts at the menu bar. The first part names a top-level menu. Further parts nest under it. A name that matches an existing menu, such as File or Run, is reused, ignoring case and the `&` shortcut marker. Any other name is added as its own menu after the built-in ones.

These two paths are the same menu: Run, then Build, then Debug, then the command.

```json
"menu": "Build/Debug"
"menu": ["Build", "Debug"]
```

A slash or a backslash separates the string form. Each entry in the array is one title, so a title written that way can contain a slash. Empty parts are dropped. Leaving `menu` out places the command on Run.

Commands appear in the order they are written. A menu is created the first time a command needs it. Choosing the same menu again adds the next command to that menu. Reloading the file removes the commands and any menus that were created for them. File, Edit, View, and Language stay. Run stays too, and is hidden while it has no commands.

A command that is running shows a check mark. The check follows the command into whatever menu it occupies.

## Variables

Write a variable as `${name}` inside `command`. The value is taken when the command starts, including when you start it again while it is already running. Quote a value that may contain spaces: `"${file}"`. A `${name}` that is not in the list below is left as written, so the shell can expand its own variables. View > Templates lists the same names.

| Variable | Value |
|---|---|
| `${file}` | Full path of the focused document |
| `${fileName}` | File name and extension. For a document that has never been saved, this is the tab title |
| `${fileBase}` | File name without the extension |
| `${fileExt}` | Extension, without the leading dot |
| `${fileDir}` | Directory that contains the file |
| `${relativeFile}` | Path of the file relative to `${project}` |
| `${line}` | Cursor line, starting at 1 |
| `${column}` | Cursor column, starting at 1 |
| `${selection}` | The selected text |
| `${buffer}` | Path of a UTF-8 file written for this launch. The file is the focused editor's text, including unsaved edits. Empty in the hex view, and when the command does not name it |
| `${terminal}` | Path of a UTF-8 file written for this launch. The file is the current terminal session: scrollback, then the screen, as plain text. Empty when no session is open |
| `${language}` | Language of the focused document. The hex view reports `Hex` |
| `${project}` | The project root: the folder that holds the `.crlf` files. Empty when only `application.crlf` applies |
| `${folder}` | Folder the explorer is scoped to |
| `${cwd}` | This command's working directory, after `cwd` is resolved |
| `${programDir}` | The folder beside the program that holds `styles`, `themes`, and `scripts`: the build folder for a dev build, the install folder otherwise. The shipped agent is `"${programDir}/scripts/agent/crlf_agent.py"` |
| `${appDir}` | The application's `.crlf` folder, `%APPDATA%\CRLF\CRLF\.crlf`, which holds the `scripts`, `keys`, and `rules` shared by every workspace. A key file there is `"${appDir}/keys/openai"` |

`${file}`, `${fileDir}`, and `${relativeFile}` are empty when the focused document has never been saved. `${line}`, `${column}`, and `${selection}` are empty in the hex view.

`${buffer}` and `${terminal}` are paths, not the text itself. A buffer or a transcript can contain newlines and quotes, which would split the shell command. The file is written when the command starts and removed when that process exits. Quote the path: `"${buffer}"`. A command that runs in a WSL distro receives `/mnt/<drive>/...` for these two files, because they are created on the Windows temp directory. `${file}` and the other project paths are still Linux paths when they already point into the distro.

```json
{
  "name": "Review",
  "command": "python review.py \"${buffer}\" \"${terminal}\""
}
```

`cwd` in the JSON file is a path, not a command string, so variables are not substituted there. `${cwd}` inside `command` is the directory the process actually starts in.

When the working directory is inside a WSL distro, the command runs in that distro and the path variables are Linux paths: `${file}`, `${fileDir}`, `${project}`, `${folder}`, `${buffer}`, `${terminal}`, `${programDir}`, `${appDir}`, and `${cwd}`. A path inside the distro loses its `\\wsl.localhost\<Distro>` prefix. A path on a Windows drive becomes `/mnt/<drive>/...`, which is how the two temp files, the install folder, and the application folder are reached. That depends on WSL mounting the Windows drives, which it does by default. `${relativeFile}`, the file name, and the extension stay as they are.

## Running a command

On Windows, a command whose working directory is a normal path runs through `cmd.exe /d /s /c`. A working directory under `\\wsl.localhost` or `\\wsl$` runs through `wsl.exe` in that distro: `bash -lc` starts in the Linux directory, and the panel shows the command's own output. Elsewhere the command runs through `/bin/sh -c`.

`command` may be an object with one line for each environment, for an entry that is used in both, such as one in `application.crlf`:

```json
{
  "name": "Lint",
  "command": { "windows": "python tools\\lint.py \"${file}\"", "wsl": "python3 tools/lint.py \"${file}\"" }
}
```

A working directory inside a distro takes `wsl`; any other takes `windows`. A string serves both. A side that is left out is an empty command, so the entry is skipped in that environment. `syntax`, `autocomplete`, and `agents` take the same form. [crlf.md](crlf.md#windows-and-wsl) lists what has to be installed in each environment.

Standard output and standard error are merged into one log. The panel opens at the bottom of the window when the command starts. A command with `"output": "editor"` is the exception: its streams are described below.

- The menu lists commands that have produced output. Each keeps its own log.
- The status reads Running, Stopped, or Exit code N.
- Stop is available while that command's process is still running. It ends the process tree. In a distro it also ends the Linux process group.
- Clear wipes the log that is showing.
- Closing the panel hides it and leaves the process running. Starting the command again shows the panel.

One process runs per command name. Choosing a command that is already running stops it and starts it again, and the variables are read again for the new start. Quitting the application stops every running command.

## Editor output

`"output": "editor"` runs the command against the focused text editor and puts standard output back into that editor. The file on disk is not written. The tab is marked modified, and the replacement is one undo step. The cursor stays where it was.

The editor's text is the process's standard input, as UTF-8 with LF line endings. Standard output is read as UTF-8 and becomes the new buffer. Standard error goes to the output panel, which opens when the process writes an error or exits with a failure code. Exit code 0 with output replaces the editor. Any other exit leaves the editor unchanged and shows standard output in the panel. Stopping the command leaves the editor unchanged. The hex view has no text buffer, so the same command keeps both streams in the panel.

Give the tool the buffer on standard input, not a path to read. A path makes it read the saved file and ignore the editor, including unsaved edits. `--assume-filename` only tells the tool which language the buffer is; it does not open that path.

```json
{
  "name": "Format",
  "command": "clang-format --assume-filename=\"${fileName}\"",
  "output": "editor",
  "menu": "File"
}
```

An empty editor may be replaced with empty output. A command that exits 0 and writes nothing while the editor has text leaves the buffer as it is, and the panel says "The command wrote nothing."

## Syntax

`syntax` runs an external program against the focused editor's buffer and draws what it reports. Leaving the key out leaves checking off. `"syntax": false` does the same. Implementation notes for a later change are in [diagnostics.md](diagnostics.md).

```json
"syntax": {
  "enabled": true,
  "command": "scripts/syntax.sh \"${file}\"",
  "cwd": "."
}
```

| Field | Required | Meaning |
|---|---|---|
| `enabled` | no | `false` stops scheduling. The command stays in the file. Missing `syntax` means off |
| `command` | when enabled | The shell command. Variables are filled in when a check starts, the same names as a menu command |
| `cwd` | no | Working directory, relative to the project root. The default is that folder |

The editor's text is standard input, UTF-8 with LF line endings. `${file}` tells the tool which language the buffer is and where its includes live. A tool that opens `${file}` checks the saved copy and misses unsaved edits.

Standard output is a JSON array. `line` and `column` are required and start at 1. `endLine` and `endColumn` are optional; without them the mark runs from `column` through the end of the line. When they are present, `endColumn` is the column where the mark stops, and that column is not included. `severity` is `error` or `warning`.

```json
[
  {
    "line": 12,
    "column": 5,
    "endLine": 12,
    "endColumn": 14,
    "severity": "error",
    "message": "expected ';'"
  }
]
```

Exit code 0 means that array is the result. `[]` means the file is clean. Any other exit leaves the current marks and puts standard error on the status bar. A script that treats "found problems" as a failure, the way clang and eslint do, exits 0 after it has written the array.

An edit clears the marks in that editor and runs the command again once typing has been still for a moment. Each open editor has its own run. The hex view is not checked.

`scripts/syntax.sh` is a Linux example. It reads stdin and picks the tool from the extension: `clang` or `clang++ -fsyntax-only` for C and C++, `csc` or the .NET SDK compiler for `.cs` when one of those is installed, and `node --check` for `.js`, `.mjs`, and `.cjs` when `node` is installed. A missing C# or Node toolchain writes `[]`. Other extensions write `[]`. A command whose working directory is inside a WSL distro runs in that distro, so the script path has to be one that distro can execute. `scripts/syntax.sh` from the project directory is the form above.

The Problems page at the bottom lists every span from the editors that have been checked. Rows are grouped by directory, then by file name. A file in the project root is under `.`. A buffer that has never been saved is under `(unsaved)`. Activating a row moves the cursor to that span. The page opens when a check finds spans and the strip is hidden. A check that finishes while Output or Terminal is showing updates the list and leaves that page where it is. Closing the strip does not stop checking.

## Completion

`unlimitedLog` set to `true` keeps every line of a process log. Left out, or in an array-shaped file, the log keeps its last 2,000 lines. It is read again when the project reloads.

`autocomplete` keeps one external process for the project and asks it for completions. Leaving the key out leaves completion off. `"autocomplete": false` does the same. The session is described in [autocomplete.md](autocomplete.md). Adding `--verbose` to the command, or setting `CRLF_AUTOCOMPLETE_VERBOSE=1`, keeps the language server's information messages.

```json
"autocomplete": {
  "enabled": true,
  "command": "scripts/autocomplete.sh"
}
```

| Field | Required | Meaning |
|---|---|---|
| `enabled` | no | `false` stops the session. The command stays in the file. Missing `autocomplete` means off |
| `command` | when enabled | The shell command. It stays running and reads one JSON object per line |
| `cwd` | no | Working directory, relative to the project root. The default is that folder |

Each request is one line. `kind` is `completion`, `signature` for the parameter hint inside a call, `open` to load a file before anything is asked about it, or `resolve` for the documentation of one row. `line` and `column` start at 1. `text` is the editor buffer. `revision` is the document revision at the moment the request was sent. A completion that a typed character asked for carries that character as `trigger`.

```json
{"kind":"completion","id":7,"revision":4,"path":"/src/main.cpp","line":12,"column":8,"text":"","trigger":"."}
```

The script writes two kinds of line. A state says where one file stands. It is written when a file is first named, each time that changes, and again for every `open`:

```json
{"kind":"state","path":"/src/main.cpp","state":"ready","server":"clangd","message":"ready","triggers":[".",">",":"]}
```

| Field | Meaning |
|---|---|
| `state` | `starting`, `loading`, or `ready` on the way to a working server. `unsupported`, `missing`, or `failed` when the file has none |
| `server` | The name of the server that owns the file |
| `message` | The sentence the status bar shows after `Autocomplete:` |
| `triggers` | The characters a completion is asked after, in this file |

The editor shows the state of the tab in front and nothing else. A script that never writes a state leaves the field at `Autocomplete: starting`. A file whose state is `unsupported` or `missing` is not asked about again.

A reply answers one request and repeats its `id` and `revision`. An `open` has no reply. `replaceEndColumn` is the column where the replacement stops, and that column is not included. `incomplete` asks the editor to send the request again each time the word being typed changes.

```json
{"id":7,"revision":4,"incomplete":false,"items":[{"label":"push_back","detail":"void push_back(const T&)","insert":"push_back(","kind":"method","data":"7:0","replaceLine":12,"replaceColumn":5,"replaceEndColumn":8}]}
```

| Item field | Required | Meaning |
|---|---|---|
| `label` | yes | The row's text |
| `replaceLine`, `replaceColumn`, `replaceEndColumn` | yes | The text the row replaces |
| `insert` | no | What is inserted. The default is the label |
| `detail` | no | Shown after the label |
| `documentation` | no | Markdown shown under the list for the selected row |
| `kind` | no | What the row is, shown at the right: `class`, `enum`, `enumMember`, `method`, `keyword`, ... |
| `filterText` | no | What the typed word is compared with. The default is the label |
| `preselect` | no | `true` on the row the list opens on, such as the enum a value is being assigned to |
| `additionalEdits` | no | Edits that belong with the row, such as an import. Carried, not applied yet |
| `data` | no | The script's name for the row. Selecting a row with no documentation sends `resolve` with it |

`resolve` carries `data` and no `text`. Its reply is `{id, revision, kind, data, detail, documentation}` for that row. A signature reply is `signatures`, one overload each, with `label`, `parameters`, and `active`, and `activeSignature`. The editor shows one at a time, with `2 of 5` in front, and Ctrl+Up and Ctrl+Down move through them. A signature that the server rejects comes back as an empty `signatures` list. A reply may be skipped when a newer request of the same kind has arrived, because the editor drops an older reply anyway.

Ctrl+Space asks immediately. A trigger character and the first letters of a word ask after a short pause. Letters typed while the list is open narrow it in the editor and are not sent, unless the list was `incomplete`. Enter or Tab inserts the selected row. Escape closes the list.

`scripts/autocomplete.sh` runs `scripts/autocomplete.py`. That program speaks to `clangd` for C and C++, and to OmniSharp or the .NET SDK language server for C#, when those tools are installed. The editor does not speak their protocol, and it does not look for a project file. The script walks the completion working directory for `.csproj` files and keeps one C# server per project, plus one server for `.cs` files that belong to none of them. A command whose working directory is inside a WSL distro runs in that distro.
