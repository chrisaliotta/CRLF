#pragma once

#include <QWidget>

class QFileSystemModel;
class QLabel;
class QModelIndex;
class QToolButton;
class QTreeView;

// File tree scoped to one user-chosen folder, shown left of the editor.
// A single click opens a file or toggles a folder, like VS Code's explorer.
// The context menu rescopes the tree and offers basic file management.
class ExplorerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ExplorerPanel(QWidget *parent = nullptr);

    void setRootPath(const QString &path);
    // Empty until the user has chosen a folder.
    QString rootPath() const { return m_rootPath; }

signals:
    void fileActivated(const QString &path);
    void rootPathChanged(const QString &path);
    // A file or folder was renamed or moved, so open tabs can follow it.
    void pathMoved(const QString &from, const QString &to);
    void closeRequested();

private:
    QToolButton *makeHeaderButton(const QString &glyph, const QString &toolTip);
    void scopeToParent();
    void expandAll();
    void collapseAll();
    void expandChildren(const QModelIndex &parent);
    void showContextMenu(const QPoint &pos);
    void setClipboard(const QString &path, bool cut);
    void paste(const QString &directory);
    void deletePath(const QString &path);
    void warn(const QString &message);

    QString m_rootPath;
    QLabel *m_titleLabel = nullptr;
    QToolButton *m_parentButton = nullptr;
    QFileSystemModel *m_model = nullptr;
    QTreeView *m_tree = nullptr;
    // Expand All keeps descending as QFileSystemModel loads folders
    // asynchronously; any collapse or rescope ends it.
    bool m_expandingAll = false;
    QString m_clipboardPath;
    bool m_clipboardCut = false;
};
