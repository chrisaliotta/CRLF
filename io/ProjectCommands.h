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
    // Enter copies the prior line's leading whitespace onto the next line.
    // Tab still follows the inferred level, then this rule.
    bool keep = false;
};

struct IndentSettings
{
    IndentRule standard;
    QList<IndentRule> types;
};

// One glyph reused from an installed icon theme. `kind` is extension, file,
// or folder. `pattern` is an exact name, or a wildcard when it contains
// * or ?. `from` is the source theme and `icon` is a key in that theme.
// `iconOpen` is the expanded folder glyph; empty keeps `icon`.
struct IconRule
{
    QString kind;
    QString pattern;
    QString from;
    QString icon;
    QString iconOpen;
};

// `theme` names the installed file-icon theme for this project. Empty leaves
// the View > File Icons choice. `rules` replace that theme's associations.
struct ProjectIcons
{
    QString theme;
    QList<IconRule> rules;
};

// An external program the project runs without being chosen from a menu: the
// syntax checker (docs/diagnostics.md) or the autocomplete session
// (docs/autocomplete.md). `enabled` is false when the .crlf file has no key
// for it. `command` is expanded when the program starts. `workingDirectory`
// is absolute: the .crlf directory, or its `cwd`. `watch` holds absolute file
// paths, resolved against `workingDirectory`; a change to one restarts the
// services.
struct ServiceSettings
{
    bool enabled = false;
    QString command;
    QString workingDirectory;
    QStringList watch;
};

// Source control for the scoped folder. `depth` is how many folder levels
// below that folder are searched for repositories; 0 is only the folder
// itself and the repository that contains it.
struct GitSettings
{
    bool enabled = false;
    int depth = 0;
};

// One entry from a project's .crlf file. workingDirectory is absolute:
// the .crlf directory, or its `cwd` resolved against that directory.
// command may contain ${...} names from CommandVariables, filled in when
// the command starts. menu is a path from the menu bar. The first part is a
// top-level menu (an existing one, or a new one). Further parts nest under
// it. An empty list places the command on Run. "Tools/Format" and
// ["Tools", "Format"] are the same path. key is an optional function key
// (F1, F2, ...) that runs the command. outputToEditor sends the focused
// editor's text to the process and, on exit 0, replaces that editor with
// standard output. The file on disk is not written.
struct ProjectCommand
{
    QString name;
    QString command;
    QString workingDirectory;
    QStringList menu;
    QKeySequence key;
    bool outputToEditor = false;
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
    ProjectIcons icons;
    // Ctrl+click definition navigation. Missing from the file means enabled.
    bool navigation = true;
    // Package and stylesheet links resolved under node_modules on click.
    bool externalNavigation = true;
    // One short syntax check per editor.
    ServiceSettings syntax;
    // One long-lived completion process per project.
    ServiceSettings autocomplete;
    // Keeps every line of a process log. False keeps the last 2,000.
    bool unlimitedLog = false;
    GitSettings git;
    QString error;
};

// The rule for this file name. A name with no suffix is matched as itself.
IndentRule indentForFileName(const IndentSettings &settings, const QString &fileName);

// `start` is a file or directory. Parents are searched up to the drive root.
// When that search finds nothing, the application directory's .crlf is used.
Project projectFor(const QString &start);

// Rewrites `groups` in the project file, keeping commands. Creates the file
// when it does not exist. An existing file that is not JSON is left unchanged.
bool saveProjectGroups(const QString &filePath, const QList<FileGroup> &groups);

// Rewrites `icons` the same way. An empty theme and no rules removes the object.
bool saveProjectIcons(const QString &filePath, const ProjectIcons &icons);
