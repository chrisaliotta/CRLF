#pragma once

#include <QString>
#include <QStringList>

// Repository roots for the scoped folder, as absolute paths. The repository
// that contains `scope` comes first, even when its root is above `scope`.
// Below that, folders up to `depth` levels under `scope` are checked for a
// `.git` entry: a directory, or the file a submodule or worktree uses. A
// repository's own subfolders are searched too, so nested repositories are
// found. The walk skips dot folders, node_modules, and names matching `hide`.
// The rest are sorted by path.
QStringList findRepositories(const QString &scope, int depth, const QStringList &hide);

// The directory holding HEAD and MERGE_HEAD for a repository root. A `.git`
// file names it with a `gitdir:` line, relative to the root or absolute.
QString gitDirectory(const QString &root);
