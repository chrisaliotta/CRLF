# Project commands

A `.crlf` file defines commands for one project. CRLF finds that file, adds each command to the menu bar, and runs it when you choose it. The command string can include values from the document that is focused at the moment the command starts.

## Finding the file

CRLF looks for a file named `.crlf`. The search starts at the folder the explorer is scoped to. When no folder is scoped, it starts at the directory of the focused document. It walks parent folders and stops at the drive root, or at the distro root of a `\\wsl.localhost` path.

The first `.crlf` found is the project file. Saving that file, or switching to a document that resolves to a different one, reloads the menus. A command that disappeared from the file is stopped.

The file is a JSON array. An entry needs `name` and `command`. A second entry with the same `name` is ignored, because the name identifies the running process and its output log.

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
| `command` | yes | The shell command. Variables are filled in when it starts |
| `cwd` | no | Working directory, relative to the `.crlf` directory. The default is that directory |
| `menu` | no | Where the command appears on the menu bar. The default is Run |
| `key` | no | A function key, F1 through F35, with no modifiers |

A `key` that is not a bare function key is ignored. When two commands ask for the same key, the first one in the file keeps it.

If the file is missing, Run shows "No .crlf in this folder". If the file is not a JSON array, Run shows "Couldn't read .crlf".

## Menus

`menu` is a path that starts at the menu bar. The first part names a top-level menu. Further parts nest under it. A name that matches an existing menu, such as File or Run, is reused, ignoring case and the `&` shortcut marker. Any other name is added as its own menu after the built-in ones.

These two paths are the same menu: Run, then Build, then Debug, then the command.

```json
"menu": "Build/Debug"
"menu": ["Build", "Debug"]
```

A slash or a backslash separates the string form. Each entry in the array is one title, so a title written that way can contain a slash. Empty parts are dropped. Leaving `menu` out places the command on Run.

Commands appear in the order they are written. A menu is created the first time a command needs it. Choosing the same menu again adds the next command to that menu. Reloading the file removes the commands and any menus that were created for them. File, Edit, View, Language, and Run stay.

A command that is running shows a check mark. The check follows the command into whatever menu it occupies.

## Variables

Write a variable as `${name}` inside `command`. The value is taken when the command starts, including when you start it again while it is already running. Quote a value that may contain spaces: `"${file}"`. A `${name}` that is not in the list below is left as written, so the shell can expand its own variables.

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
| `${language}` | Language of the focused document. The hex view reports `Hex` |
| `${project}` | Directory that contains the `.crlf` file |
| `${folder}` | Folder the explorer is scoped to |
| `${cwd}` | This command's working directory, after `cwd` is resolved |

`${file}`, `${fileDir}`, and `${relativeFile}` are empty when the focused document has never been saved. `${line}`, `${column}`, and `${selection}` are empty in the hex view.

`cwd` in the JSON file is a path, not a command string, so variables are not substituted there. `${cwd}` inside `command` is the directory the process actually starts in.

When the working directory is inside a WSL distro, the command runs in that distro and the path variables (`${file}`, `${fileDir}`, `${project}`, `${folder}`, and `${cwd}`) are Linux paths when they point into a distro. `${relativeFile}`, the file name, and the extension stay as they are.

## Running a command

On Windows, a command whose working directory is a normal path runs through `cmd.exe /d /s /c`. A working directory under `\\wsl.localhost` or `\\wsl$` runs through `wsl.exe` in that distro: `bash -lc` starts in the Linux directory, and the panel shows the command's own output. Elsewhere the command runs through `/bin/sh -c`.

Standard output and standard error are merged into one log. The panel opens at the bottom of the window when the command starts.

- The menu lists commands that have produced output. Each keeps its own log.
- The status reads Running, Stopped, or Exit code N.
- Stop is available while that command's process is still running. It ends the process tree. In a distro it also ends the Linux process group.
- Clear wipes the log that is showing.
- Closing the panel hides it and leaves the process running. Starting the command again shows the panel.

One process runs per command name. Choosing a command that is already running stops it and starts it again, and the variables are read again for the new start. Quitting the application stops every running command.
