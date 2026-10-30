#pragma once

#include <QKeySequence>
#include <QList>
#include <QString>
#include <QStringList>

// A virtual folder. `files` are exact names, or wildcard patterns when they
// contain * or ?. `icon` is the folder name passed to the icon theme; an
// empty icon uses `name`. `path` is relative to the .crlf directory; empty
// means that directory itself. `recursive` also applies the group in
// subfolders of that path. `showIfEmpty` shows the folder where no file
// matches. When it is false, the folder is omitted wherever it would be empty.
struct FileGroup
{
    QString name;
    QString icon;
    QString path;
    QStringList files;
    bool recursive = false;
    bool showIfEmpty = false;
};

// How Tab inserts text. `spaces` inserts `size` spaces; otherwise Tab inserts
// one tab character shown `size` columns wide. `extensions` is empty on the
// default rule. A file uses the first rule whose extension matches its suffix,
// or its file name when it has no suffix.
struct IndentRule
{
    QStringList extensions;
    bool spaces = false;
    int size = 4;
};

struct IndentSettings
{
    IndentRule standard;
    QList<IndentRule> types;
};

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
    QList<FileGroup> groups;
    QStringList hide;
    IndentSettings indent;
    QString error;
};

// The rule for this file name. A name with no suffix is matched as itself.
IndentRule indentForFileName(const IndentSettings &settings, const QString &fileName);

// `start` is a file or directory. Parents are searched up to the drive root.
Project projectFor(const QString &start);

// Rewrites `groups` in the project file, keeping commands. Creates the file
// when it does not exist. An existing file that is not JSON is left unchanged.
bool saveProjectGroups(const QString &filePath, const QList<FileGroup> &groups);
