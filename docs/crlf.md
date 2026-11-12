# The .crlf file

`.crlf` is the project file. One file is in use at a time. It is JSON. The parser in `io/ProjectCommands.cpp` is the list of keys that do anything. A key that is not in that list is kept when the file is rewritten and is otherwise ignored.

Menus, variables, and how a command runs are in [commands.md](commands.md). Checks are in [diagnostics.md](diagnostics.md). Completion is in [autocomplete.md](autocomplete.md). Definition links are in [navigation.md](navigation.md). Source control is in [git.md](git.md). The Agent panel is in [agent.md](agent.md).

## Which file is in use

The search starts at the folder the explorer is scoped to. When no folder is scoped, it starts at the directory of the focused document. It walks parent folders and stops at the drive root, or at the distro root of a `\\wsl.localhost` path. The first `.crlf` found is the project file.

When that walk finds nothing, CRLF uses the `.crlf` next to the program, beside `styles` and `themes`. View > Edit Defaults opens that copy. A workspace file replaces it for as long as the walk finds the workspace file.

The file is read again when the explorer folder changes, when the current tab changes, when a document is saved, when the file on disk changes, and when a virtual folder or an icon mapping is saved from the UI. The read runs off the UI thread. A newer read replaces an older one that has not been applied yet.

A file that is not JSON, or that is neither an array nor an object, keeps its path so it can be watched, shows "Couldn't read .crlf" in the status bar, and applies no commands. The other settings fall back to their defaults, because the broken file did not supply them.

## Two shapes

A bare array is a list of commands and nothing else.

```json
[
  { "name": "Build", "command": "cmake --build build", "cwd": "build" }
]
```

An object is the full file. `commands` is the same list. Every other key below is read only from the object. Putting `indent`, `hide`, `syntax`, `autocomplete`, `agents`, or `git` next to a bare array does not parse: a bare array has no place to put them.

```json
{
  "commands": [],
  "groups": [],
  "hide": [],
  "icons": {},
  "indent": {},
  "navigation": true,
  "externalNavigation": true,
  "syntax": {},
  "autocomplete": {},
  "agents": [],
  "unlimitedLog": false,
  "git": {}
}
```

| Key | Type | When it is left out | Section |
|---|---|---|---|
| `commands` | array of objects | No menu commands | [Commands](#commands) |
| `groups` | array of objects | No virtual folders | [Virtual folders](#virtual-folders) |
| `hide` | array of strings | Nothing is hidden | [Hidden names](#hidden-names) |
| `icons` | object: `theme` string, `rules` array of objects | View > File Icons decides | [Icons](#icons) |
| `indent` | object: `style`, `size`, `keep`, `types` array of objects | One tab character, four columns wide | [Indent](#indent) |
| `navigation` | boolean | `true` | [Navigation](#navigation) |
| `externalNavigation` | boolean | `true` | [Navigation](#navigation) |
| `syntax` | object: `enabled`, `command`, `cwd`. Or a boolean | Off | [Syntax and autocomplete](#syntax-and-autocomplete) |
| `autocomplete` | object: `enabled`, `command`, `cwd`. Or a boolean | Off | [Syntax and autocomplete](#syntax-and-autocomplete) |
| `agents` | array of objects: `name`, `command`, `cwd`, `watch`, `permissions` | The Agent panel has nothing to start | [Agents](#agents) |
| `unlimitedLog` | boolean | `false` | [Process logs](#process-logs) |
| `git` | object: `enabled`, `depth`. Or a boolean | Off | [Source control](#source-control) |

A value of the wrong type is read as if the key were left out. JSON has no comments, so this document is where the file is explained.

The `.crlf` at the root of this repository uses every key and is the file CRLF reads when this folder is open. Its `syntax` and `autocomplete` entries carry the right commands and are switched off, because the scripts need `bash` and a C++ toolchain (`clang`, `clangd`) that a Windows checkout does not have.

New Virtual Folder and View > File Icons > Templates write the object form. If the file was a bare array, that write turns it into an object whose `commands` array is the old list. Other keys already in an object are left as they were. A file that is not JSON is left unchanged, and the status bar says "Couldn't write .crlf".

The shipped `resources/default.crlf` is `{ "commands": [] }`. It is copied beside the program only when that copy is not already there.

## Commands

Each entry needs `name` and `command`. An entry missing either is skipped. A second entry with the same `name` is skipped. The name is the menu label, the process name, and the output log.

| Field | Meaning |
|---|---|
| `name` | Required. Identifies the command |
| `command` | Required. Shell command. `${name}` is filled in when the process starts |
| `cwd` | Working directory, relative to the `.crlf` directory. The default is that directory. Variables are not filled in here |
| `menu` | Menu path. A string splits on `/` and `\`. An array does not split, so one entry can contain a slash. Empty parts are dropped. The default is Run |
| `key` | A function key from F1 through F35, with no modifiers. Anything else is ignored. The first command to claim a key keeps it |
| `output` | `editor` sends the focused editor's text to the process and, on exit 0, replaces that editor. Any other value keeps both streams in the output panel |

```json
"commands": [
  {
    "name": "Format",
    "command": "clang-format --assume-filename=\"${fileName}\"",
    "output": "editor",
    "menu": "File"
  },
  {
    "name": "Build",
    "command": "cmake --build build",
    "cwd": "build",
    "menu": ["Tools", "Build"],
    "key": "F5"
  }
]
```

Format replaces the focused editor and does not write the file. Build runs in the `build` directory under the `.crlf` folder and sits on Tools, then Build. F5 starts it. A second `"key": "F5"` later in the file is ignored.

`${name}` matches letters only, as in `${file}`. `${file-name}` is not a variable. A name this program does not know is left in the string for the shell. The names are listed in [commands.md](commands.md). `${buffer}` and `${terminal}` are paths to files written for that launch: the focused editor's text, including unsaved edits, and the current terminal's plain text. The same substitution is used for `syntax` and `autocomplete`.

On Windows, a working directory under `\\wsl.localhost` or `\\wsl$` runs the command through `wsl.exe` in that distro. Path variables that point into a distro are Linux paths. Any other Windows directory runs through `cmd.exe /d /s /c`.

## Virtual folders

`groups` adds folders in the explorer that do not exist on disk. A file is claimed by the first group, in file order, whose pattern matches its name in that directory. Directories are never claimed.

| Field | Meaning |
|---|---|
| `name` | Required. The label. A group with an empty name is skipped |
| `files` | Names to fold in. A pattern containing `*` or `?` is a wildcard. Any other pattern is an exact name. Matching ignores case |
| `icon` | Folder name passed to the file-icon theme. The default is `name` |
| `path` | Folder under the `.crlf` directory. Empty, `.`, and `./` mean that directory |
| `recursive` | Also apply the group in subfolders of `path`. The default is false, so the group exists only in `path` itself |
| `showIfEmpty` | When false (the default), the folder is omitted wherever it has no matching files |

```json
"groups": [
  {
    "name": "Configuration",
    "icon": "config",
    "path": "src",
    "recursive": true,
    "files": [".gitignore", "package.json", "*.config.js"]
  },
  {
    "name": "Notes",
    "showIfEmpty": true,
    "files": ["TODO.md", "NOTES.md"]
  }
]
```

Configuration appears in `src` and in its subfolders, and only where one of those names is present. `app.config.js` matches the wildcard. Notes is at the `.crlf` directory, and it stays visible even when both files are missing. A file that matches both groups lands in Configuration, because that group is written first.

## Hidden names

`hide` is a list of file and folder names. A pattern containing `*` or `?` is a wildcard. Any other pattern is an exact name. Matching ignores case and uses the name, not the path.

The explorer drops a matching row. The definition scan of the scoped folder also skips a matching name, so a hidden directory is not walked for Ctrl+click. That scan already skips directories whose names start with `.` and a directory named `node_modules`, whether or not they are in `hide`.

```json
"hide": ["node_modules", "dist", "*.log", "bin", "obj"]
```

Hide Dot Folders is a view setting. It hides directories whose names start with `.` and leaves dot files, including `.crlf`, visible. It is not a key in this file.

## Icons

`icons` selects the file-icon theme for this project and adds mappings. `theme` is the name of an installed theme, such as `Seti`. Leaving it out keeps View > File Icons. An empty theme and no rules removes the object the next time the mappings are saved.

Each rule needs `kind`, `pattern`, `from`, and `icon`. A rule missing any of those is skipped.

| Field | Meaning |
|---|---|
| `kind` | `extension`, `file`, or `folder`. `directory` is read as `folder` |
| `pattern` | Exact name, or a wildcard when it contains `*` or `?`. `key` is used when `pattern` is empty |
| `from` | Installed theme the glyph is copied from |
| `icon` | Key in that theme |
| `iconOpen` | Expanded folder glyph. Empty keeps `icon` |

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

The same folder rule can be written with `"kind": "directory"` and `"key": "src*"` instead of `pattern`. A rule that leaves out `from` or `icon` is dropped.

## Indent

`indent` is the fallback for Tab when the lines around the cursor do not show an indentation. With no `indent` key, Tab inserts one tab character four columns wide.

| Field | Meaning |
|---|---|
| `style` | `spaces` or `tab`. Any other string leaves the style already in effect |
| `size` | Spaces inserted by a spaces-style Tab, and the column width of a tab character. A value below 1 is ignored |
| `keep` | Enter copies the current line's leading whitespace. A blank line copies the prior non-blank line. A line with no indent stays at column zero. The default is false |
| `types` | Per-extension overrides. Each entry has `extensions` plus any of the fields above |

A type inherits `style`, `size`, and `keep` from the standard rule, then applies the fields it sets. The first matching type wins. The match is the file's suffix without the dot, compared without case. A file with no suffix is matched on its whole name, so `Makefile` needs `makefile`. An entry of `.py` does not match `py`.

Tab itself prefers the whitespace already on the current line, or on the prior line when the current line is not indented. It stops at the first line that has text and no indent. A tab in that whitespace inserts a tab. Spaces insert the shallowest space run in that walk. The rule in this file is used only when that walk finds no indent. Shift+Tab removes one such level.

```json
"indent": {
  "style": "spaces",
  "size": 4,
  "keep": true,
  "types": [
    { "extensions": ["py"], "style": "spaces", "size": 4 },
    { "extensions": ["go", "makefile"], "style": "tab", "size": 4, "keep": true },
    { "extensions": ["json"], "keep": false }
  ]
}
```

A `.py` file uses four spaces. A `Makefile` uses a tab four columns wide, matched on the file name because it has no suffix. A `.json` file inherits four spaces and does not copy indent on Enter. A `.cs` file is not listed, so it uses the standard rule: four spaces, and Enter keeps the prior line's indent.

## Navigation

`navigation` turns Ctrl+hover and Ctrl+click definition links on or off. Missing, or `true`, keeps them. `false` stops the scan of open files and of the scoped folder.

`externalNavigation` turns links into `node_modules` on or off, including package imports, stylesheet names the project does not define, and Tailwind theme lookups. Missing, or `true`, keeps them. `false` leaves those names as plain text. Project definitions still link.

Both keys are read only from the object form. A bare array leaves both on.

```json
"navigation": true,
"externalNavigation": false
```

Ctrl+click still opens a definition in the project. A name imported from a package stays plain text and is not read from `node_modules`.

## Syntax and autocomplete

`syntax` and `autocomplete` use the same object. Leaving the key out leaves that feature off. `false` does the same.

| Field | Meaning |
|---|---|
| `enabled` | Must be `true` for the feature to run. An object that omits it is off |
| `command` | Shell command. Required in practice: an enabled entry with an empty command does not start |
| `cwd` | Working directory, relative to the `.crlf` directory. The default is that directory |
| `watch` | Array of file paths, relative to `cwd`. Saving one of them restarts the services |

`"syntax": true` sets `enabled` and leaves `command` empty, so no check runs. The same is true for `autocomplete`. The command has to be in the object, with `enabled` set to `true`.

```json
"syntax": {
  "enabled": true,
  "command": "./scripts/syntax.sh \"${file}\"",
  "cwd": "."
},
"autocomplete": {
  "enabled": true,
  "command": "./scripts/autocomplete.sh",
  "watch": ["scripts/autocomplete.sh", "scripts/autocomplete.py"]
}
```

Saving the `.crlf` file, or any file listed under `watch` by either service, stops the autocomplete session and starts it again, and runs every open editor's check again. List each script the command runs, including scripts it calls in turn, such as `autocomplete.py` above.

`syntax` reads the editor buffer on standard input and writes a JSON array of spans. `autocomplete` stays running and speaks one JSON object per line. A working directory inside a WSL distro runs the command in that distro, so `./scripts/syntax.sh` has to be a path that distro can execute. The contracts are in [diagnostics.md](diagnostics.md) and [autocomplete.md](autocomplete.md).

The Processes page shows a check as its own task and the completion session as `autocomplete`.

These two do not start a process:

```json
"syntax": true,
"autocomplete": { "command": "./scripts/autocomplete.sh" }
```

The first sets `enabled` and leaves `command` empty. The second has a command and leaves `enabled` off.

## Agents

`agents` lists the programs the Agent panel (View > Agent) can start. Each one speaks the Agent Client Protocol on standard input and output: the reference agent beside the program, Claude Code behind its ACP adapter, or anything else that does. [agent.md](agent.md) describes the panel and the protocol.

| Field | Meaning |
|---|---|
| `name` | Required. The label in the panel's picker. A second entry with the same name is skipped |
| `command` | Required. Shell command, with `${name}` filled in when the agent starts |
| `cwd` | The session's working directory, relative to the `.crlf` directory. The default is that directory. The agent may read and write inside it without leaving the permissions below |
| `watch` | Array of file paths, relative to `cwd`. Saving one starts a new session |
| `permissions` | Object with `read`, `write`, and `terminal`, each `allow`, `ask`, `review`, or `deny` |

| Permission | Default | Covers |
|---|---|---|
| `read` | `allow` | Reading files, the open files, problems, git status, and the terminal's text |
| `write` | `review` | Writing a file. `review` shows the change and waits for Accept |
| `terminal` | `ask` | Running a command, and typing into your terminal |

A path outside `cwd` always asks, even when the setting is `allow`.

```json
"agents": [
  {
    "name": "Claude",
    "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider anthropic --model claude-sonnet-4-5 --api-key-file .keys/anthropic"
  },
  {
    "name": "Local",
    "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider ollama --model qwen3-coder",
    "permissions": { "write": "allow", "terminal": "ask" }
  }
]
```

Claude reads its key from `.keys/anthropic` beside `.crlf`; keep that folder out of git, and put a new key in the file to rotate it. Without `--api-key-file`, the reference agent reads `ANTHROPIC_API_KEY` from the environment CRLF was started from. Local talks to Ollama on this machine, writes files without review, and asks before each command. The model names are examples; use the ones your provider offers.

For the reference agent, `--model` is only where a session starts, along with `--max-tokens`, `--reasoning-effort` (OpenAI and xAI), and `--thinking` (Anthropic: `off`, `4k`, `16k`, `32k`, or `adaptive`). The Agent panel lists the provider's models and these settings, and remembers what you pick there for each agent `name`. [agent.md](agent.md#settings) describes them.

## Process logs

`unlimitedLog` set to `true` keeps every line a process writes to its log on the Processes page. Missing, or `false`, keeps the last 2,000 lines of each log. It is read only from the object form.

```json
"unlimitedLog": true
```

Set it before reproducing a problem in a noisy process such as the autocomplete session, so the start of the log is still there afterwards.

## Source control

`git` tracks the git repositories in the scoped folder: the Source Control panel, explorer badges, gutter change bars, the Diff tab, and the Git menu. Leaving the key out leaves it off. [git.md](git.md) describes all of it.

| Field | Meaning |
|---|---|
| `enabled` | `false` turns it off. An object that omits it is on, unlike `syntax` and `autocomplete` |
| `depth` | How many folder levels below the scoped folder are searched for repositories. The default is 0: the scoped folder and the repository that contains it. A value below 0 is read as 0 |

`"git": true` is on with `depth` 0. The walk skips folders whose names start with `.`, `node_modules`, and names in `hide`, and it keeps searching inside a repository, so nested repositories are found.

```json
"git": { "depth": 2 }
```

With the explorer on `C:/work`, this tracks the repository holding `C:/work` if there is one, and repositories such as `C:/work/api` and `C:/work/clients/web`. A repository at `C:/work/clients/web/vendor` is three levels down and is not tracked.

## A whole file

One object that uses every key. Paths are relative to the directory that contains `.crlf`.

```json
{
  "commands": [
    {
      "name": "Format",
      "command": "clang-format --assume-filename=\"${fileName}\"",
      "output": "editor",
      "menu": "File"
    },
    {
      "name": "Build",
      "command": "cmake --build build",
      "cwd": "build",
      "menu": "Tools/Build",
      "key": "F5"
    }
  ],
  "groups": [
    {
      "name": "Configuration",
      "icon": "config",
      "recursive": true,
      "files": [".gitignore", "package.json", "*.config.js"]
    }
  ],
  "hide": ["node_modules", "dist", "bin", "obj", "*.log"],
  "icons": {
    "theme": "Seti",
    "rules": [
      { "kind": "extension", "pattern": "*.test.js", "from": "Seti", "icon": "javascript" },
      { "kind": "folder", "pattern": "src*", "from": "Seti", "icon": "config", "iconOpen": "folderExpanded" }
    ]
  },
  "indent": {
    "style": "spaces",
    "size": 4,
    "keep": true,
    "types": [
      { "extensions": ["go", "makefile"], "style": "tab", "size": 4 }
    ]
  },
  "navigation": true,
  "externalNavigation": true,
  "syntax": {
    "enabled": true,
    "command": "./scripts/syntax.sh \"${file}\""
  },
  "autocomplete": {
    "enabled": true,
    "command": "./scripts/autocomplete.sh"
  },
  "agents": [
    {
      "name": "GPT",
      "command": "python \"${programDir}/scripts/agent/crlf_agent.py\" --provider openai --model gpt-5",
      "permissions": { "terminal": "ask" }
    }
  ],
  "unlimitedLog": false,
  "git": { "enabled": true, "depth": 1 }
}
```
