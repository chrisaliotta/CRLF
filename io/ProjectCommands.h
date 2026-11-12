#pragma once

#include "agent/AgentPolicy.h"

#include <QKeySequence>
#include <QList>
#include <QString>
#include <QStringList>

// A virtual folder. `files` are exact names, or wildcard patterns when they
// contain * or ?. `icon` is the folder name passed to the icon theme; an
// empty icon uses `name`. `path` is relative to the project root; empty
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
// (docs/autocomplete.md). `enabled` is false when no .crlf file has a key
// for it. `command` is expanded when the program starts. `workingDirectory`
// is absolute: the project root, or its `cwd`. `watch` holds absolute file
// paths, resolved against `workingDirectory`; a change to one restarts the
// services.
struct ServiceSettings
{
    bool enabled = false;
    QString command;
    QString workingDirectory;
    QStringList watch;
};

// One entry of `agents`: an Agent Client Protocol program the Agent panel can
// start (docs/agent.md). `command`, `workingDirectory`, and `watch` are read
// like a service's; `enabled` is not used.
struct AgentSettings
{
    QString name;
    ServiceSettings process;
    AgentPermissions permissions;
};

// Source control for the scoped folder. `depth` is how many folder levels
// below that folder are searched for repositories; 0 is only the folder
// itself and the repository that contains it.
struct GitSettings
{
    bool enabled = false;
    int depth = 0;
};

// One entry of `commands`. workingDirectory is absolute: the project root,
// or its `cwd` resolved against that directory. command is the line for the
// environment that directory is in (Windows or a WSL distro). It may contain
// ${...} names from CommandVariables, filled in when the command starts.
// menu is a path from the menu bar. The first part is a
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

// Every <name>.crlf file in a project root is a settings file. The folder
// named .crlf beside them holds what the project keeps for the editor and
// its agents: data, rules, keys, scripts.
constexpr char kProjectFolder[] = ".crlf";
// The file the editor creates when a project has none.
constexpr char kWorkspaceFile[] = "workspace.crlf";
// The user's own settings, in the application folder.
constexpr char kApplicationFile[] = "application.crlf";

// The settings of one project: every <name>.crlf file in the project root,
// merged in file-name order with the first definition winning, and
// application.crlf after them when a workspace file sets `inherit`. With no
// workspace file, application.crlf applies alone. See io/ProjectMerge.h.
struct Project
{
    // The project root: the nearest folder, walking up, that holds a
    // <name>.crlf file. Empty when only application.crlf applies.
    QString directory;
    // The settings files in use, in merge order, so they can be watched. A
    // file that is not JSON is listed too, applies nothing, and is named in
    // `error`.
    QStringList files;
    // Set by the load that renamed a legacy file named exactly `.crlf`.
    QString renamedFrom;
    QString renamedTo;
    QList<ProjectCommand> commands;
    QList<FileGroup> groups;
    // Name patterns left out of the explorer, the symbol scan, and the
    // repository search.
    QStringList hide;
    IndentSettings indent;
    ProjectIcons icons;
    // Ctrl+click definition navigation. Missing from the files means enabled.
    bool navigation = true;
    // Package and stylesheet links resolved under node_modules on click.
    bool externalNavigation = true;
    // One short syntax check per editor.
    ServiceSettings syntax;
    // One long-lived completion process per project.
    ServiceSettings autocomplete;
    // The Agent panel's choices, in merge order. One runs at a time.
    QList<AgentSettings> agents;
    // Keeps every line of a process log. False keeps the last 2,000.
    bool unlimitedLog = false;
    GitSettings git;
    QString error;
};

// The rule for this file name. A name with no suffix is matched as itself.
IndentRule indentForFileName(const IndentSettings &settings, const QString &fileName);

// The folder that holds application.crlf and the application's .crlf
// folder. It is where the session is kept: %APPDATA%\CRLF\CRLF.
QString applicationFolder();

// The <name>.crlf files in `directory`, as absolute paths in name order,
// ignoring case.
QStringList settingsFiles(const QString &directory);

// First run: copies application.crlf into `applicationFolder` and creates
// its .crlf/scripts, .crlf/keys, and .crlf/rules. The copy comes from the
// file named .crlf beside the program, which was the application's settings
// before, or from the application.crlf shipped there when that is absent.
// Does nothing once application.crlf exists.
void seedApplicationFolder(const QString &applicationFolder, const QString &programFolder);

// `start` is a file or directory. Parents are searched up to the drive root
// for the project root. A legacy file named `.crlf` there is renamed to
// workspace.crlf, or to legacy.crlf when that name is taken; when the rename
// fails, the file is read under its old name.
//
// Relative `cwd`, `watch`, and group paths resolve against the project root,
// in application.crlf entries too. With no project root they resolve against
// `start` when it is a folder, and `applicationFolder` otherwise.
Project projectFor(const QString &start, const QString &applicationFolder);

// Writes `groups` for the project rooted at `directory`. The first workspace
// file that has the key takes the list, and the key is removed from the
// later ones. With no such file, workspace.crlf in `directory` takes it; a
// folder with no settings file gets a workspace.crlf that also sets
// `inherit`, so the application's settings keep applying there.
// An entry inherited from application.crlf and left as it was is not written,
// and application.crlf is never changed. Returns false when the file to
// write is not JSON or cannot be written.
bool saveProjectGroups(const QString &directory, const QList<FileGroup> &groups, const QString &applicationFolder);

// Writes `icons` the same way. An empty theme and no rules removes the key.
bool saveProjectIcons(const QString &directory, const ProjectIcons &icons, const QString &applicationFolder);
