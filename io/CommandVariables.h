#pragma once

#include <QJsonValue>
#include <QString>

// Application values a command in a .crlf file can name with ${...}. Each
// one is filled in when the command starts, from the document that is
// focused then. A document that has never been saved has an empty path;
// line, column, and selection are empty in the hex view.
//
//   ${file}          absolute path
//   ${fileName}      name and extension (the tab title when unsaved)
//   ${fileBase}      name without the extension
//   ${fileExt}       extension, without the dot
//   ${fileDir}       directory that contains the file
//   ${relativeFile}  path relative to ${project}
//   ${line}          cursor line, starting at 1
//   ${column}        cursor column, starting at 1
//   ${selection}     selected text
//   ${buffer}        path of a UTF-8 file holding the focused editor's text,
//                    including unsaved edits. Empty in the hex view
//   ${terminal}      path of a UTF-8 file holding the current terminal's
//                    plain-text scrollback and screen. Empty when no session
//                    is open
//   ${language}      language of the focused document
//   ${project}       the project root: the folder that holds the .crlf
//                    files. Empty when only application.crlf applies
//   ${folder}        folder the explorer is scoped to
//   ${cwd}           this command's working directory
//   ${programDir}    the folder that holds styles, themes, and scripts
//                    beside the program
//   ${appDir}        the application's .crlf folder, which holds the scripts,
//                    keys, and rules shared by every workspace
//
// A name this list does not have is left unchanged. Quote a value in the
// command when it may contain spaces: "${file}".
struct CommandVariables
{
    QString file;
    QString fileName;
    QString fileBase;
    QString fileExt;
    QString fileDir;
    QString relativeFile;
    QString line;
    QString column;
    QString selection;
    // Paths of files written for this launch. Empty when the command does
    // not name them. The runner deletes the files when the process ends.
    QString buffer;
    QString terminal;
    QString language;
    QString project;
    QString folder;
    QString cwd;
    QString programDir;
    QString appDir;
};

// Replaces each known ${name} in `command`.
QString expandCommand(const QString &command, const CommandVariables &vars);

// The variables as a process started in `workingDirectory` reads them, with
// ${cwd} set. A directory inside a WSL distro runs its command in that
// distro, so every path variable becomes a Linux path there: a path inside
// the distro loses its \\wsl.localhost prefix, and a path on a Windows drive
// is the drive's mount, /mnt/<drive>/... That is how ${buffer}, ${terminal},
// ${appDir}, and ${programDir} reach a distro.
CommandVariables launchVariables(CommandVariables vars, const QString &workingDirectory);

// The command line of one .crlf entry. `command` is a string, or an object
// with `windows` and `wsl`, where a working directory inside a WSL distro
// takes `wsl` and any other takes `windows`.
QString commandFor(const QJsonValue &command, const QString &workingDirectory);
