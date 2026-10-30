#pragma once

#include <QString>

// File-system mutations behind the explorer's context menu. Each returns
// false when the OS refuses the operation (permissions, locks, missing files).
namespace FileOperations {

// Path inside `directory` named after `source`, Windows-style suffixed
// ("name - Copy.ext", "name - Copy (2).ext", ...) when the name is taken.
QString availableDestination(const QString &source, const QString &directory);

// Folders are copied recursively.
bool copy(const QString &source, const QString &destination);
// Renames in place; folders crossing volumes are copied then removed.
bool move(const QString &source, const QString &destination);
bool moveToTrash(const QString &path);

// Opens Windows File Explorer with `path` selected in its parent folder.
void revealInFileExplorer(const QString &path);

} // namespace FileOperations
