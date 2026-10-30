#pragma once

#include "io/ProjectCommands.h"

#include <QWidget>

class DirectoryFilter;
class ExplorerDelegate;
class ExplorerModel;
class ExplorerTree;
class FileIconTheme;
class QLabel;
class QModelIndex;
class QStackedWidget;
class QToolButton;
class QTreeView;

// File tree scoped to one user-chosen folder, shown left of the editor.
// A single click opens a file or toggles a folder, like VS Code's explorer.
// The context menu rescopes the tree and offers basic file management.
// Items can be dragged onto a folder; the drop is confirmed before it moves.
class ExplorerPanel : public QWidget
{
    Q_OBJECT
    friend class ExplorerDelegate;
    friend class ExplorerTree;

public:
    explicit ExplorerPanel(QWidget *parent = nullptr);

    // warnIfMissing is false for the folder restored at startup: a missing
    // saved folder closes the explorer instead of interrupting launch.
    void setRootPath(const QString &path, bool warnIfMissing = true);
    void setIconTheme(const FileIconTheme *theme);
    void setGroups(const QString &directory, const QList<FileGroup> &groups);
    // Name patterns from .crlf `hide`. A pattern with * or ? is a wildcard.
    void setHidden(const QStringList &patterns);
    // Empty until the user has chosen a folder.
    QString rootPath() const { return m_rootPath; }

signals:
    void fileActivated(const QString &path);
    void rootPathChanged(const QString &path);
    // A file or folder was renamed or moved, so open tabs can follow it.
    void pathMoved(const QString &from, const QString &to);
    void closeRequested();
    // The edited group list. MainWindow writes it into .crlf.
    void virtualFoldersEdited(const QList<FileGroup> &groups);

private:
    QToolButton *makeHeaderButton(const QString &glyph, const QString &toolTip);
    void scopeToParent();
    void expandAll();
    void collapseAll();
    void expandChildren(const QModelIndex &parent);
    void showContextMenu(const QPoint &pos);
    void setClipboard(const QString &path, bool cut);
    void paste(const QString &directory);
    void confirmAndMove(const QString &source, const QString &directory);
    void createVirtualFolder(const QString &directory);
    void addToVirtualFolder(const QString &filePath, int groupIndex);
    void setGroupRecursive(int groupIndex, bool recursive);
    void commitGroups(const QList<FileGroup> &groups);
    void deletePath(const QString &path);
    void createFile(const QString &directory);
    void insertNewFile(const QString &directory);
    void finishNewFile(int hint);
    void showRootReady();
    void showRootFailed();
    void warn(const QString &message);

    QString m_rootPath;
    // Last folder whose listing succeeded. A failed open returns to this one.
    QString m_announcedRoot;
    QString m_restoreRoot;
    bool m_warnIfMissing = true;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_loading = nullptr;
    QToolButton *m_parentButton = nullptr;
    QStackedWidget *m_stack = nullptr;
    ExplorerModel *m_model = nullptr;
    // Hides directories whose names start with '.' when that option is on.
    // The tree shows this model; m_model stays the file-system source.
    DirectoryFilter *m_filter = nullptr;
    QTreeView *m_tree = nullptr;
    // Expand All keeps descending as folders finish loading; any collapse
    // or rescope ends it.
    bool m_expandingAll = false;
    QString m_clipboardPath;
    bool m_clipboardCut = false;
    // Set for the duration of a drag so the source row can be drawn faded.
    QString m_dragPath;
    // Set while a New File row is waiting for its name. The original path is
    // the placeholder on disk; the current path follows a successful rename.
    QString m_pendingDirectory;
    QString m_newFileOriginal;
    QString m_newFilePath;
    QString m_typedName;
};
