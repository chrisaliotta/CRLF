#pragma once

#include <QAbstractItemModel>
#include <QFileIconProvider>
#include <QList>

class DirectoryListing;

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

    // Replaces the tree. Children arrive later through directoryLoaded.
    void setRootPath(const QString &path);
    QModelIndex rootIndex() const;

    QString filePath(const QModelIndex &index) const;
    QString fileName(const QModelIndex &index) const;
    bool isDir(const QModelIndex &index) const;
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

    Node *m_root = nullptr;
    QFileIconProvider m_icons;
};
