#pragma once

#include <QList>
#include <QString>

// One entry from a project's .crlf file. workingDirectory is absolute:
// the .crlf directory, or its `cwd` resolved against that directory.
struct ProjectCommand
{
    QString name;
    QString command;
    QString workingDirectory;
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
