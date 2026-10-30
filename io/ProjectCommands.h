#pragma once

#include <QKeySequence>
#include <QList>
#include <QString>

// One entry from a project's .crlf file. workingDirectory is absolute:
// the .crlf directory, or its `cwd` resolved against that directory.
// command may contain ${...} names from CommandVariables, filled in when
// the command starts. menu is a path from the menu bar. The first part is a
// top-level menu (an existing one, or a new one). Further parts nest under
// it. An empty list places the command on Run. "Tools/Format" and
// ["Tools", "Format"] are the same path. key is an optional function key
// (F1, F2, ...) that runs the command.
struct ProjectCommand
{
    QString name;
    QString command;
    QString workingDirectory;
    QStringList menu;
    QKeySequence key;
};

// The .crlf discovered by walking parent folders, or an empty filePath when
// no project file exists. A file that exists but cannot be parsed keeps its
// path (so it can be watched) and explains itself in `error`.
struct Project
{
    QString filePath;
    QList<ProjectCommand> commands;
    QString error;
};

// `start` is a file or directory. Parents are searched up to the drive root.
Project projectFor(const QString &start);
