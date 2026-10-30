#pragma once

#include <QString>

// Application values a .crlf command can name with ${...}. Each one is
// filled in when the command starts, from the document that is focused then.
// A document that has never been saved has an empty path; line, column, and
// selection are empty in the hex view.
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
//   ${language}      language of the focused document
//   ${project}       directory that contains the .crlf
//   ${folder}        folder the explorer is scoped to
//   ${cwd}           this command's working directory
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
    QString language;
    QString project;
    QString folder;
    QString cwd;
};

// Replaces each known ${name} in `command`.
QString expandCommand(const QString &command, const CommandVariables &vars);
