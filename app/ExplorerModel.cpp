#include "app/ExplorerModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QMimeData>
#include <QRunnable>
#include <QThreadPool>
#include <QUrl>

namespace {

bool samePath(const QString &left, const QString &right)
{
    return QDir::cleanPath(left).compare(QDir::cleanPath(right), Qt::CaseInsensitive) == 0;
}

} // namespace

struct ExplorerModel::Node
{
    QString name;
    QString path;
    bool isDir = false;
    bool placeholder = false;
    bool listed = false;
    bool listing = false;
    int generation = 0;
    Node *parent = nullptr;
    QList<Node *> children;
};

class DirectoryListing : public QRunnable
{
public:
    DirectoryListing(ExplorerModel *model, const QString &path, int generation)
        : m_model(model)
        , m_path(path)
        , m_generation(generation)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        QDir dir(m_path);
        const bool ok = dir.exists();
        QList<ExplorerModel::Entry> entries;
        if (ok) {
            const QFileInfoList infos = dir.entryInfoList(
                QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                QDir::DirsFirst | QDir::IgnoreCase | QDir::Name);
            entries.reserve(infos.size());
            for (const QFileInfo &info : infos) {
                ExplorerModel::Entry entry;
                entry.name = info.fileName();
                entry.path = QDir::cleanPath(info.absoluteFilePath());
                entry.isDir = info.isDir();
                entries.append(entry);
            }
        }
        const QString path = m_path;
        const int generation = m_generation;
        const bool found = ok;
        QMetaObject::invokeMethod(m_model, [model = m_model, path, generation, found, entries] {
            model->completeListing(path, generation, found, entries);
        }, Qt::QueuedConnection);
    }

private:
    ExplorerModel *m_model;
    QString m_path;
    int m_generation;
};

ExplorerModel::ExplorerModel(QObject *parent)
    : QAbstractItemModel(parent)
{
}

ExplorerModel::~ExplorerModel()
{
    deleteNode(m_root);
}

void ExplorerModel::setRootPath(const QString &path)
{
    beginResetModel();
    deleteNode(m_root);
    m_root = new Node;
    m_root->path = QDir::cleanPath(path);
    m_root->name = QDir(m_root->path).dirName();
    if (m_root->name.isEmpty())
        m_root->name = QDir::toNativeSeparators(m_root->path);
    m_root->isDir = true;
    endResetModel();
    startListing(m_root);
}

QModelIndex ExplorerModel::rootIndex() const
{
    return indexOf(m_root);
}

QString ExplorerModel::filePath(const QModelIndex &index) const
{
    const Node *node = nodeAt(index);
    return node != nullptr && !node->placeholder ? node->path : QString();
}

QString ExplorerModel::fileName(const QModelIndex &index) const
{
    const Node *node = nodeAt(index);
    return node != nullptr ? node->name : QString();
}

bool ExplorerModel::isDir(const QModelIndex &index) const
{
    const Node *node = nodeAt(index);
    return node != nullptr && node->isDir;
}

bool ExplorerModel::isPlaceholder(const QModelIndex &index) const
{
    const Node *node = nodeAt(index);
    return node != nullptr && node->placeholder;
}

bool ExplorerModel::isListed(const QModelIndex &index) const
{
    const Node *node = nodeAt(index);
    return node != nullptr && node->listed;
}

bool ExplorerModel::containsName(const QModelIndex &directory, const QString &name) const
{
    const Node *node = nodeAt(directory);
    if (node == nullptr)
        return false;
    for (const Node *child : node->children) {
        if (!child->placeholder && child->name.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QModelIndex ExplorerModel::index(const QString &path)
{
    if (Node *existing = find(path))
        return indexOf(existing);

    const QFileInfo info(path);
    Node *parentNode = find(info.path());
    if (parentNode == nullptr || !parentNode->listed)
        return {};

    const QString name = info.fileName();
    int row = parentNode->children.size();
    for (int i = 0; i < parentNode->children.size(); ++i) {
        const Node *child = parentNode->children.at(i);
        if (child->placeholder || child->isDir)
            continue;
        if (child->name.compare(name, Qt::CaseInsensitive) > 0) {
            row = i;
            break;
        }
    }

    beginInsertRows(indexOf(parentNode), row, row);
    auto *child = new Node;
    child->name = name;
    child->path = QDir::cleanPath(path);
    child->parent = parentNode;
    parentNode->children.insert(row, child);
    endInsertRows();
    return indexOf(child);
}

bool ExplorerModel::remove(const QModelIndex &index)
{
    Node *node = nodeAt(index);
    if (node == nullptr || node == m_root || node->placeholder)
        return false;
    const bool removed = node->isDir ? QDir(node->path).removeRecursively() : QFile::remove(node->path);
    if (!removed)
        return false;
    discardNode(node);
    return true;
}

void ExplorerModel::discard(const QString &path)
{
    Node *node = find(path);
    if (node == nullptr || node == m_root)
        return;
    discardNode(node);
}

void ExplorerModel::refresh(const QString &directory)
{
    Node *node = find(directory);
    if (node == nullptr || !node->isDir)
        return;
    startListing(node);
}

void ExplorerModel::markStale(const QModelIndex &index)
{
    Node *node = nodeAt(index);
    if (node == nullptr || node == m_root || !node->isDir)
        return;
    node->generation += 1;
    node->listing = false;
    node->listed = false;
    clearChildren(node);
}

QModelIndex ExplorerModel::index(int row, int column, const QModelIndex &parent) const
{
    if (column != 0 || row < 0)
        return {};
    if (!parent.isValid())
        return row == 0 ? indexOf(m_root) : QModelIndex();
    const Node *node = nodeAt(parent);
    if (node == nullptr || row >= node->children.size())
        return {};
    return indexOf(node->children.at(row));
}

QModelIndex ExplorerModel::parent(const QModelIndex &child) const
{
    const Node *node = nodeAt(child);
    if (node == nullptr || node->parent == nullptr)
        return {};
    return indexOf(node->parent);
}

int ExplorerModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0)
        return 0;
    if (!parent.isValid())
        return m_root != nullptr ? 1 : 0;
    const Node *node = nodeAt(parent);
    return node != nullptr ? node->children.size() : 0;
}

int ExplorerModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 1;
}

QVariant ExplorerModel::data(const QModelIndex &index, int role) const
{
    const Node *node = nodeAt(index);
    if (node == nullptr)
        return {};
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return node->name;
    case FilePathRole:
        return node->placeholder ? QString() : node->path;
    case Qt::DecorationRole:
        if (node->placeholder)
            return {};
        return node->isDir ? m_icons.icon(QFileIconProvider::Folder) : m_icons.icon(QFileIconProvider::File);
    case Qt::ToolTipRole:
        return node->placeholder ? QString() : QDir::toNativeSeparators(node->path);
    default:
        return {};
    }
}

bool ExplorerModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole)
        return false;
    Node *node = nodeAt(index);
    if (node == nullptr || node == m_root || node->placeholder || node->parent == nullptr)
        return false;

    const QString name = value.toString().trimmed();
    if (name.isEmpty() || name == node->name)
        return !name.isEmpty();
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')))
        return false;

    const QString updated = QDir(node->parent->path).filePath(name);
    if (!QFile::rename(node->path, updated))
        return false;

    const QString oldName = node->name;
    const QString directory = node->parent->path;
    node->name = name;
    const QString newPath = QDir::cleanPath(updated);
    const auto rewrite = [](auto &&self, Node *current, const QString &path) -> void {
        current->path = path;
        for (Node *child : current->children) {
            if (!child->placeholder)
                self(self, child, QDir(path).filePath(child->name));
        }
    };
    rewrite(rewrite, node, newPath);
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole, FilePathRole, Qt::ToolTipRole});
    emit fileRenamed(directory, oldName, name);
    return true;
}

Qt::ItemFlags ExplorerModel::flags(const QModelIndex &index) const
{
    const Node *node = nodeAt(index);
    if (node == nullptr || node->placeholder)
        return Qt::ItemIsEnabled;
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsEditable;
    if (node->isDir)
        flags |= Qt::ItemIsDropEnabled;
    else
        flags |= Qt::ItemNeverHasChildren;
    return flags;
}

bool ExplorerModel::hasChildren(const QModelIndex &parent) const
{
    const Node *node = nodeAt(parent);
    if (node == nullptr)
        return m_root != nullptr;
    if (!node->isDir)
        return false;
    // An unread directory keeps its arrow. A finished one only does when it
    // actually has children, so a failed or empty folder does not spin.
    return !node->listed || !node->children.isEmpty();
}

bool ExplorerModel::canFetchMore(const QModelIndex &parent) const
{
    const Node *node = nodeAt(parent);
    return node != nullptr && node->isDir && !node->listed && !node->listing;
}

void ExplorerModel::fetchMore(const QModelIndex &parent)
{
    Node *node = nodeAt(parent);
    if (node != nullptr && node->isDir && !node->listed && !node->listing)
        startListing(node);
}

QMimeData *ExplorerModel::mimeData(const QModelIndexList &indexes) const
{
    auto *mime = new QMimeData;
    QList<QUrl> urls;
    for (const QModelIndex &index : indexes) {
        const QString path = filePath(index);
        if (!path.isEmpty())
            urls.append(QUrl::fromLocalFile(path));
    }
    mime->setUrls(urls);
    return mime;
}

QStringList ExplorerModel::mimeTypes() const
{
    return {QStringLiteral("text/uri-list")};
}

Qt::DropActions ExplorerModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

void ExplorerModel::startListing(Node *node)
{
    node->generation += 1;
    node->listing = true;
    const bool showPlaceholder = node != m_root && !node->listed && node->children.isEmpty();
    if (showPlaceholder) {
        beginInsertRows(indexOf(node), 0, 0);
        auto *placeholder = new Node;
        placeholder->name = QStringLiteral("Loading…");
        placeholder->placeholder = true;
        placeholder->parent = node;
        node->children.append(placeholder);
        endInsertRows();
    }
    QThreadPool::globalInstance()->start(new DirectoryListing(this, node->path, node->generation));
}

void ExplorerModel::completeListing(const QString &path, int generation, bool ok,
                                    const QList<Entry> &entries)
{
    Node *node = find(path);
    if (node == nullptr || node->generation != generation)
        return;
    node->listing = false;
    if (!ok) {
        clearChildren(node);
        if (node == m_root) {
            node->listed = false;
            emit listingFailed(node->path);
        } else {
            node->listed = true;
        }
        return;
    }

    clearChildren(node);
    if (!entries.isEmpty()) {
        beginInsertRows(indexOf(node), 0, entries.size() - 1);
        for (const Entry &entry : entries) {
            auto *child = new Node;
            child->name = entry.name;
            child->path = entry.path;
            child->isDir = entry.isDir;
            child->parent = node;
            node->children.append(child);
        }
        endInsertRows();
    }
    node->listed = true;
    emit directoryLoaded(node->path);
}

void ExplorerModel::clearChildren(Node *node)
{
    if (node->children.isEmpty())
        return;
    beginRemoveRows(indexOf(node), 0, node->children.size() - 1);
    for (Node *child : node->children)
        deleteNode(child);
    node->children.clear();
    endRemoveRows();
}

void ExplorerModel::deleteNode(Node *node)
{
    if (node == nullptr)
        return;
    for (Node *child : node->children)
        deleteNode(child);
    delete node;
}

ExplorerModel::Node *ExplorerModel::nodeAt(const QModelIndex &index) const
{
    return index.isValid() ? static_cast<Node *>(index.internalPointer()) : nullptr;
}

QModelIndex ExplorerModel::indexOf(const Node *node) const
{
    if (node == nullptr)
        return {};
    if (node->parent == nullptr)
        return createIndex(0, 0, const_cast<Node *>(node));
    const int row = node->parent->children.indexOf(const_cast<Node *>(node));
    return createIndex(row, 0, const_cast<Node *>(node));
}

ExplorerModel::Node *ExplorerModel::find(const QString &path) const
{
    return m_root != nullptr ? findFrom(m_root, QDir::cleanPath(path)) : nullptr;
}

ExplorerModel::Node *ExplorerModel::findFrom(Node *node, const QString &clean) const
{
    if (!node->placeholder && samePath(node->path, clean))
        return node;
    for (Node *child : node->children) {
        if (Node *found = findFrom(child, clean))
            return found;
    }
    return nullptr;
}

void ExplorerModel::discardNode(Node *node)
{
    const int row = node->parent->children.indexOf(node);
    beginRemoveRows(indexOf(node->parent), row, row);
    node->parent->children.removeAt(row);
    node->parent = nullptr;
    deleteNode(node);
    endRemoveRows();
}
