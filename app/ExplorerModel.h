#pragma once

#include "io/ProjectCommands.h"

#include <QAbstractItemModel>
#include <QFileIconProvider>
#include <QList>

class DirectoryListing;
class FileIconTheme;

// The scoped folder is the model's only top-level node, and its children are
// the rows the tree shows. Listings are read on a worker thread: QFileSystemModel
// resolves a UNC path through "My Computer" and, when \\wsl.localhost itself is
// not a directory, that resolution lands on C:.
class ExplorerModel : public QAbstractItemModel
{
    Q_OBJECT
    friend class DirectoryListing;

public:
    enum Role { FilePathRole = Qt::UserRole + 1 };

    explicit ExplorerModel(QObject *parent = nullptr);
    ~ExplorerModel() override;

    void setIconTheme(const FileIconTheme *theme);
    // Virtual folders for one directory, usually the folder that holds .crlf.
    // An empty directory clears them. A directory that is already listed is
    // read again so the groups show up.
    // `directory` is the folder that contains .crlf. Groups may name a path
    // under it, and a recursive group also applies in that path's subfolders.
    void setGroups(const QString &directory, const QList<FileGroup> &groups);
    QList<FileGroup> groups() const { return m_groups; }
    QString projectDirectory() const { return m_projectDirectory; }
    // Index into groups() for a virtual folder row, or -1.
    int virtualGroup(const QModelIndex &index) const;
    // The real directory a virtual folder is shown in.
    QString virtualDirectory(const QModelIndex &index) const;
    bool groupApplies(int groupIndex, const QString &directory) const;

    // Replaces the tree. Children arrive later through directoryLoaded.
    void setRootPath(const QString &path);
    QModelIndex rootIndex() const;

    QString filePath(const QModelIndex &index) const;
    QString fileName(const QModelIndex &index) const;
    bool isDir(const QModelIndex &index) const;
    bool isVirtual(const QModelIndex &index) const;
    bool isPlaceholder(const QModelIndex &index) const;
    bool isListed(const QModelIndex &index) const;
    bool containsName(const QModelIndex &directory, const QString &name) const;

    // Inserts a file row when its parent directory is already listed.
    QModelIndex index(const QString &path);
    // Deletes the file on disk and drops the row.
    bool remove(const QModelIndex &index);
    // Drops a row whose file is already gone.
    void discard(const QString &path);
    // Reads the directory again. A collapsed folder is dropped so the next
    // expand reads it again.
    void refresh(const QString &directory);
    void markStale(const QModelIndex &index);
    // The tree reports this. An open directory uses the theme's folderExpanded icon.
    void setExpanded(const QModelIndex &index, bool expanded);

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool hasChildren(const QModelIndex &parent = {}) const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    QStringList mimeTypes() const override;
    Qt::DropActions supportedDropActions() const override;

signals:
    void directoryLoaded(const QString &path);
    void listingFailed(const QString &path);
    void fileRenamed(const QString &directory, const QString &oldName, const QString &newName);

private:
    struct Node;
    struct Entry {
        QString name;
        QString path;
        bool isDir = false;
    };

    void startListing(Node *node);
    void completeListing(const QString &path, int generation, bool ok, const QList<Entry> &entries);
    void clearChildren(Node *node);
    void deleteNode(Node *node);
    Node *nodeAt(const QModelIndex &index) const;
    QModelIndex indexOf(const Node *node) const;
    Node *find(const QString &path) const;
    Node *findFrom(Node *node, const QString &clean) const;
    void discardNode(Node *node);
    void notifyDecoration(Node *node);
    void reapplyGroups(Node *node);
    void regroupDirectory(Node *directory);
    void moveNode(Node *node, Node *destination);
    bool groupAppliesTo(const FileGroup &group, const QString &directory) const;
    bool shouldShowGroup(const FileGroup &group, bool hasFiles) const;
    int groupFor(const QString &directory, const QString &fileName) const;
    Node *ensureGroup(Node *directory, int groupIndex);
    void regroupFile(const QString &path);
    int fileRow(Node *parent, const QString &name) const;

    Node *m_root = nullptr;
    const FileIconTheme *m_iconTheme = nullptr;
    QString m_projectDirectory;
    QList<FileGroup> m_groups;
    QFileIconProvider m_icons;
};
