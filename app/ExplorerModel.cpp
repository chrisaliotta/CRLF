#include "app/ExplorerModel.h"

#include "app/FileIconTheme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QMimeData>
#include <QRegularExpression>
#include <QRunnable>
#include <QThreadPool>
#include <QUrl>

namespace {

bool samePath(const QString &left, const QString &right)
{
    return QDir::cleanPath(left).compare(QDir::cleanPath(right), Qt::CaseInsensitive) == 0;
}

bool patternMatches(const QString &pattern, const QString &name)
{
    if (!pattern.contains(QLatin1Char('*')) && !pattern.contains(QLatin1Char('?')))
        return name.compare(pattern, Qt::CaseInsensitive) == 0;
    const QRegularExpression expression(
        QRegularExpression::wildcardToRegularExpression(pattern),
        QRegularExpression::CaseInsensitiveOption);
    return expression.match(name).hasMatch();
}

} // namespace

struct ExplorerModel::Node
{
    QString name;
    QString path;
    // Folder name the icon theme looks up. Virtual folders use the group's
    // icon, or its display name when the group leaves icon empty.
    QString iconName;
    bool isDir = false;
    bool placeholder = false;
    bool virtualFolder = false;
    bool listed = false;
    bool listing = false;
    bool expanded = false;
    int generation = 0;
    int groupIndex = -1;
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

void ExplorerModel::setIconTheme(const FileIconTheme *theme)
{
    m_iconTheme = theme;
    if (m_root != nullptr)
        notifyDecoration(m_root);
}

void ExplorerModel::setGroups(const QString &directory, const QList<FileGroup> &groups)
{
    m_projectDirectory = directory.isEmpty() ? QString() : QDir::cleanPath(directory);
    m_groups = groups;
    reapplyGroups(m_root);
}

void ExplorerModel::reapplyGroups(Node *node)
{
    if (node == nullptr || node->virtualFolder || !node->isDir)
        return;
    const QList<Node *> children = node->children;
    for (Node *child : children)
        reapplyGroups(child);
    if (node->listed && !node->listing)
        regroupDirectory(node);
}

void ExplorerModel::moveNode(Node *node, Node *destination)
{
    if (node->parent == destination)
        return;
    const int oldRow = node->parent->children.indexOf(node);
    const int newRow = fileRow(destination, node->name);
    if (!beginMoveRows(indexOf(node->parent), oldRow, oldRow, indexOf(destination), newRow))
        return;
    node->parent->children.removeAt(oldRow);
    node->parent = destination;
    destination->children.insert(newRow, node);
    endMoveRows();
}

bool ExplorerModel::shouldShowGroup(const FileGroup &group, bool hasFiles) const
{
    return hasFiles || group.showIfEmpty;
}

void ExplorerModel::regroupDirectory(Node *directory)
{
    QList<Node *> files;
    for (Node *child : directory->children) {
        if (child->placeholder)
            continue;
        if (child->virtualFolder) {
            for (Node *file : child->children)
                files.append(file);
        } else if (!child->isDir) {
            files.append(child);
        }
    }

    QList<int> active;
    for (int group = 0; group < m_groups.size(); ++group) {
        if (groupAppliesTo(m_groups.at(group), directory->path))
            active.append(group);
    }

    QList<QList<Node *>> buckets(m_groups.size());
    QList<Node *> loose;
    for (Node *file : files) {
        const int group = groupFor(directory->path, file->name);
        if (group >= 0)
            buckets[group].append(file);
        else
            loose.append(file);
    }

    QList<Node *> kept;
    for (int group : active) {
        const bool hasFiles = !buckets.at(group).isEmpty();
        if (!shouldShowGroup(m_groups.at(group), hasFiles))
            continue;
        Node *folder = nullptr;
        for (Node *child : directory->children) {
            if (child->virtualFolder && child->groupIndex == group) {
                folder = child;
                break;
            }
        }
        if (folder == nullptr) {
            folder = ensureGroup(directory, group);
        } else {
            const FileGroup &spec = m_groups.at(group);
            folder->name = spec.name;
            folder->iconName = spec.icon.isEmpty() ? spec.name : spec.icon;
            const QModelIndex index = indexOf(folder);
            emit dataChanged(index, index, {Qt::DisplayRole, Qt::DecorationRole});
        }
        kept.append(folder);
        for (Node *file : buckets.at(group))
            moveNode(file, folder);
    }
    for (Node *file : loose)
        moveNode(file, directory);

    QList<Node *> stale;
    for (Node *child : directory->children) {
        if (child->virtualFolder && !kept.contains(child))
            stale.append(child);
    }
    for (Node *folder : stale) {
        const QList<Node *> leftover = folder->children;
        for (Node *file : leftover)
            moveNode(file, directory);
        discardNode(folder);
    }
}

int ExplorerModel::virtualGroup(const QModelIndex &index) const
{
    const Node *node = nodeAt(index);
    return node != nullptr && node->virtualFolder ? node->groupIndex : -1;
}

QString ExplorerModel::virtualDirectory(const QModelIndex &index) const
{
    const Node *node = nodeAt(index);
    return node != nullptr && node->virtualFolder ? node->path : QString();
}

bool ExplorerModel::groupApplies(int groupIndex, const QString &directory) const
{
    if (groupIndex < 0 || groupIndex >= m_groups.size())
        return false;
    return groupAppliesTo(m_groups.at(groupIndex), directory);
}

bool ExplorerModel::groupAppliesTo(const FileGroup &group, const QString &directory) const
{
    if (m_projectDirectory.isEmpty())
        return false;
    const QString root = group.path.isEmpty()
                             ? m_projectDirectory
                             : QDir::cleanPath(QDir(m_projectDirectory).filePath(group.path));
    const QString clean = QDir::cleanPath(directory);
    if (samePath(clean, root))
        return true;
    if (!group.recursive)
        return false;
    return clean.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive);
}

void ExplorerModel::notifyDecoration(Node *node)
{
    const QModelIndex index = indexOf(node);
    if (index.isValid())
        emit dataChanged(index, index, {Qt::DecorationRole});
    for (Node *child : node->children)
        notifyDecoration(child);
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
    if (node == nullptr || node->placeholder || node->virtualFolder)
        return {};
    return node->path;
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

bool ExplorerModel::isVirtual(const QModelIndex &index) const
{
    const Node *node = nodeAt(index);
    return node != nullptr && node->virtualFolder;
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
        if (child->virtualFolder) {
            for (const Node *nested : child->children) {
                if (nested->name.compare(name, Qt::CaseInsensitive) == 0)
                    return true;
            }
            continue;
        }
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
    Node *home = parentNode;
    const int group = groupFor(parentNode->path, name);
    if (group >= 0)
        home = ensureGroup(parentNode, group);
    const int row = fileRow(home, name);

    beginInsertRows(indexOf(home), row, row);
    auto *child = new Node;
    child->name = name;
    child->path = QDir::cleanPath(path);
    child->parent = home;
    home->children.insert(row, child);
    endInsertRows();
    return indexOf(child);
}

bool ExplorerModel::remove(const QModelIndex &index)
{
    Node *node = nodeAt(index);
    if (node == nullptr || node == m_root || node->placeholder)
        return false;
    if (node->virtualFolder)
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
    if (node == nullptr || node == m_root || node->virtualFolder)
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

void ExplorerModel::setExpanded(const QModelIndex &index, bool expanded)
{
    Node *node = nodeAt(index);
    if (node == nullptr || !node->isDir || node->expanded == expanded)
        return;
    node->expanded = expanded;
    const QModelIndex row = indexOf(node);
    emit dataChanged(row, row, {Qt::DecorationRole});
}

void ExplorerModel::markStale(const QModelIndex &index)
{
    Node *node = nodeAt(index);
    if (node == nullptr || node == m_root || !node->isDir || node->virtualFolder)
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
        return node->placeholder || node->virtualFolder ? QString() : node->path;
    case Qt::DecorationRole:
        if (node->placeholder)
            return {};
        if (m_iconTheme != nullptr) {
            const QString key = node->virtualFolder && !node->iconName.isEmpty() ? node->iconName : node->name;
            return m_iconTheme->iconFor(key, node->isDir, node->expanded);
        }
        return node->isDir ? m_icons.icon(QFileIconProvider::Folder) : m_icons.icon(QFileIconProvider::File);
    case Qt::ToolTipRole:
        if (node->placeholder || node->virtualFolder)
            return {};
        return QDir::toNativeSeparators(node->path);
    default:
        return {};
    }
}

bool ExplorerModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole)
        return false;
    Node *node = nodeAt(index);
    if (node == nullptr || node == m_root || node->placeholder || node->virtualFolder || node->parent == nullptr)
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
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole, Qt::DecorationRole, FilePathRole, Qt::ToolTipRole});
    emit fileRenamed(directory, oldName, name);
    if (!node->isDir) {
        const QString moved = node->path;
        QMetaObject::invokeMethod(this, [this, moved] { regroupFile(moved); }, Qt::QueuedConnection);
    }
    return true;
}

Qt::ItemFlags ExplorerModel::flags(const QModelIndex &index) const
{
    const Node *node = nodeAt(index);
    if (node == nullptr || node->placeholder)
        return Qt::ItemIsEnabled;
    if (node->virtualFolder)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled;
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

    QList<Node *> directories;
    QList<Node *> files;
    directories.reserve(entries.size());
    for (const Entry &entry : entries) {
        auto *child = new Node;
        child->name = entry.name;
        child->path = entry.path;
        child->isDir = entry.isDir;
        if (entry.isDir)
            directories.append(child);
        else
            files.append(child);
    }

    QList<int> active;
    for (int group = 0; group < m_groups.size(); ++group) {
        if (groupAppliesTo(m_groups.at(group), node->path))
            active.append(group);
    }
    QList<QList<Node *>> buckets(m_groups.size());
    QList<Node *> loose;
    for (Node *file : files) {
        const int group = active.isEmpty() ? -1 : groupFor(node->path, file->name);
        if (group >= 0)
            buckets[group].append(file);
        else
            loose.append(file);
    }

    QList<Node *> rows = directories;
    for (int group : active) {
        if (!shouldShowGroup(m_groups.at(group), !buckets.at(group).isEmpty()))
            continue;
        auto *folder = new Node;
        folder->name = m_groups.at(group).name;
        folder->iconName = m_groups.at(group).icon.isEmpty() ? folder->name : m_groups.at(group).icon;
        folder->path = node->path;
        folder->isDir = true;
        folder->listed = true;
        folder->virtualFolder = true;
        folder->groupIndex = group;
        for (Node *file : buckets.at(group)) {
            file->parent = folder;
            folder->children.append(file);
        }
        rows.append(folder);
    }
    rows.append(loose);

    if (!rows.isEmpty()) {
        beginInsertRows(indexOf(node), 0, rows.size() - 1);
        for (Node *child : rows) {
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
    // A virtual folder stores its real parent's path so renames land on disk.
    // Matching it here would hide that parent.
    if (!node->placeholder && !node->virtualFolder && samePath(node->path, clean))
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

int ExplorerModel::groupFor(const QString &directory, const QString &fileName) const
{
    for (int i = 0; i < m_groups.size(); ++i) {
        if (!groupAppliesTo(m_groups.at(i), directory))
            continue;
        for (const QString &pattern : m_groups.at(i).files) {
            if (patternMatches(pattern, fileName))
                return i;
        }
    }
    return -1;
}

int ExplorerModel::fileRow(Node *parent, const QString &name) const
{
    for (int i = 0; i < parent->children.size(); ++i) {
        const Node *child = parent->children.at(i);
        if (child->placeholder || child->isDir)
            continue;
        if (child->name.compare(name, Qt::CaseInsensitive) > 0)
            return i;
    }
    return parent->children.size();
}

ExplorerModel::Node *ExplorerModel::ensureGroup(Node *directory, int groupIndex)
{
    for (Node *child : directory->children) {
        if (child->virtualFolder && child->groupIndex == groupIndex)
            return child;
    }

    int row = directory->children.size();
    for (int i = 0; i < directory->children.size(); ++i) {
        const Node *child = directory->children.at(i);
        const bool laterGroup = child->virtualFolder && child->groupIndex > groupIndex;
        if (laterGroup || !child->isDir) {
            row = i;
            break;
        }
    }

    beginInsertRows(indexOf(directory), row, row);
    auto *folder = new Node;
    folder->name = m_groups.at(groupIndex).name;
    folder->iconName = m_groups.at(groupIndex).icon.isEmpty() ? folder->name : m_groups.at(groupIndex).icon;
    folder->path = directory->path;
    folder->isDir = true;
    folder->listed = true;
    folder->virtualFolder = true;
    folder->groupIndex = groupIndex;
    folder->parent = directory;
    directory->children.insert(row, folder);
    endInsertRows();
    return folder;
}

void ExplorerModel::regroupFile(const QString &path)
{
    Node *node = find(path);
    if (node == nullptr || node->isDir || node->virtualFolder || node->parent == nullptr)
        return;
    Node *directory = node->parent->virtualFolder ? node->parent->parent : node->parent;
    if (directory == nullptr)
        return;

    const int group = groupFor(directory->path, node->name);
    Node *destination = group >= 0 ? ensureGroup(directory, group) : directory;
    if (destination == node->parent)
        return;

    const int oldRow = node->parent->children.indexOf(node);
    const int newRow = fileRow(destination, node->name);
    if (!beginMoveRows(indexOf(node->parent), oldRow, oldRow, indexOf(destination), newRow))
        return;
    node->parent->children.removeAt(oldRow);
    node->parent = destination;
    destination->children.insert(newRow, node);
    endMoveRows();
}
