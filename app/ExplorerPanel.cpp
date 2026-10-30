#include "app/ExplorerPanel.h"

#include "app/ExplorerModel.h"
#include "io/FileOperations.h"
#include "io/WslPath.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QAbstractItemDelegate>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QRegularExpression>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QString directoryName(const QString &path)
{
    const QString name = QDir(path).dirName();
    return name.isEmpty() ? QDir::toNativeSeparators(path) : name;
}

bool samePath(const QString &left, const QString &right)
{
    return QDir::cleanPath(left).compare(QDir::cleanPath(right), Qt::CaseInsensitive) == 0;
}

// The parent of a drive root or a distro share is the path itself, so the
// parent button can decide from the string without stating the share.
QString parentDirectory(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    const int slash = clean.lastIndexOf(QLatin1Char('/'));
    if (slash <= 0)
        return clean;
    QString parent = clean.left(slash);
    if (parent.size() == 2 && parent.at(1) == QLatin1Char(':'))
        parent += QLatin1Char('/');
    return parent;
}

bool canScopeUp(const QString &path)
{
    if (WslPath::isDistroRoot(path))
        return false;
    return parentDirectory(path).compare(QDir::cleanPath(path), Qt::CaseInsensitive) != 0;
}

} // namespace

// Draws a cut or in-progress drag row faded so the two states read at a glance.
class ExplorerDelegate : public QStyledItemDelegate
{
public:
    explicit ExplorerDelegate(ExplorerPanel *panel, QObject *parent)
        : QStyledItemDelegate(parent)
        , m_panel(panel)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        const QString path = index.data(ExplorerModel::FilePathRole).toString();
        const bool dragging = !m_panel->m_dragPath.isEmpty()
                              && path.compare(m_panel->m_dragPath, Qt::CaseInsensitive) == 0;
        const bool cut = m_panel->m_clipboardCut
                         && path.compare(m_panel->m_clipboardPath, Qt::CaseInsensitive) == 0;
        if (!dragging && !cut) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        painter->save();
        painter->setOpacity(dragging ? 0.35 : 0.45);
        QStyledItemDelegate::paint(painter, option, index);
        painter->restore();
    }

private:
    ExplorerPanel *m_panel;
};

// Internal drag of one row onto a folder. The base drop would move immediately;
// this asks first and expands a hovered folder while the drag is held over it.
class ExplorerTree : public QTreeView
{
public:
    explicit ExplorerTree(ExplorerPanel *panel)
        : QTreeView(panel)
        , m_panel(panel)
    {
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::DragDrop);
        setDefaultDropAction(Qt::MoveAction);
        // Hovering a collapsed folder with a dragged row opens it.
        setAutoExpandDelay(600);
        setViewportMargins(12, 0, 0, 0);
    }

    // The file-icon style sheet polishes the view and clears viewport margins.
    bool event(QEvent *event) override
    {
        const bool handled = QTreeView::event(event);
        if (event->type() == QEvent::Polish)
            setViewportMargins(12, 0, 0, 0);
        return handled;
    }

protected:
    void startDrag(Qt::DropActions supportedActions) override
    {
        const QModelIndexList indexes = selectedIndexes();
        if (indexes.isEmpty())
            return;
        QMimeData *mime = model()->mimeData(indexes);
        if (mime == nullptr)
            return;

        const QModelIndex index = indexes.first();
        const QRect rect = visualRect(index);
        QPixmap shot = viewport()->grab(rect);
        QPixmap ghost(shot.size());
        ghost.setDevicePixelRatio(shot.devicePixelRatio());
        ghost.fill(Qt::transparent);
        QPainter painter(&ghost);
        painter.setOpacity(0.6);
        painter.drawPixmap(0, 0, shot);
        painter.end();

        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->setPixmap(ghost);
        const QPoint hotspot = viewport()->mapFromGlobal(QCursor::pos()) - rect.topLeft();
        drag->setHotSpot(QPoint(qBound(0, hotspot.x(), rect.width()),
                                qBound(0, hotspot.y(), rect.height())));

        m_panel->m_dragPath = index.data(ExplorerModel::FilePathRole).toString();
        viewport()->update();
        drag->exec(supportedActions, Qt::MoveAction);
        m_panel->m_dragPath.clear();
        viewport()->update();
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->source() == this)
            QTreeView::dragEnterEvent(event);
        else
            event->ignore();
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (event->source() != this) {
            event->ignore();
            return;
        }
        QTreeView::dragMoveEvent(event);
    }

    void dropEvent(QDropEvent *event) override
    {
        if (event->source() != this || !event->mimeData()->hasUrls()) {
            event->ignore();
            return;
        }
        const QString dropped = QDir::cleanPath(event->mimeData()->urls().first().toLocalFile());
        const QModelIndex index = indexAt(event->position().toPoint());
        if (index.isValid()) {
            const QModelIndex target = sourceIndex(index);
            if (files()->isVirtual(target)) {
                if (QFileInfo(dropped).isDir()) {
                    event->ignore();
                    return;
                }
                const int group = files()->virtualGroup(target);
                const QString home = files()->virtualDirectory(target);
                event->acceptProposedAction();
                QTimer::singleShot(0, m_panel, [panel = m_panel, dropped, group, home] {
                    if (!samePath(QFileInfo(dropped).absolutePath(), home)) {
                        panel->warn(QStringLiteral("Drop a file from this folder."));
                        return;
                    }
                    panel->addToVirtualFolder(dropped, group);
                });
                return;
            }
        }
        QString directory = m_panel->m_rootPath;
        if (index.isValid()) {
            const QModelIndex target = sourceIndex(index);
            const QString path = files()->filePath(target);
            directory = files()->isDir(target) ? path : QFileInfo(path).absolutePath();
        }
        event->acceptProposedAction();
        // The drag pixmap stays up until this returns, so confirm afterwards.
        QTimer::singleShot(0, m_panel, [panel = m_panel, dropped, directory] {
            panel->confirmAndMove(dropped, directory);
        });
    }

private:
    ExplorerModel *files() const
    {
        auto *proxy = qobject_cast<QAbstractProxyModel *>(model());
        return static_cast<ExplorerModel *>(proxy != nullptr ? proxy->sourceModel() : model());
    }

    QModelIndex sourceIndex(const QModelIndex &index) const
    {
        auto *proxy = qobject_cast<QAbstractProxyModel *>(model());
        return proxy != nullptr ? proxy->mapToSource(index) : index;
    }

    ExplorerPanel *m_panel;
};

// Drops directories whose names start with '.' while leaving dot files,
// such as .crlf, in the tree. The scoped folder itself is the model's only
// top-level row and stays visible even when its name starts with '.'.
bool patternMatches(const QString &pattern, const QString &name)
{
    if (!pattern.contains(QLatin1Char('*')) && !pattern.contains(QLatin1Char('?')))
        return name.compare(pattern, Qt::CaseInsensitive) == 0;
    const QRegularExpression expression(
        QRegularExpression::wildcardToRegularExpression(pattern),
        QRegularExpression::CaseInsensitiveOption);
    return expression.match(name).hasMatch();
}

class DirectoryFilter : public QSortFilterProxyModel
{
public:
    explicit DirectoryFilter(QObject *parent)
        : QSortFilterProxyModel(parent)
    {
    }

    bool hideDotFolders = true;
    QStringList hidden;

    void setHideDotFolders(bool hide)
    {
        hideDotFolders = hide;
        beginFilterChange();
        endFilterChange();
    }

    void setHidden(const QStringList &patterns)
    {
        hidden = patterns;
        beginFilterChange();
        endFilterChange();
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override
    {
        auto *files = static_cast<ExplorerModel *>(sourceModel());
        const QModelIndex index = files->index(sourceRow, 0, sourceParent);
        if (files->isPlaceholder(index))
            return true;
        const QString name = files->fileName(index);
        for (const QString &pattern : hidden) {
            if (patternMatches(pattern, name))
                return false;
        }
        if (!hideDotFolders || !sourceParent.isValid())
            return true;
        return !files->isDir(index) || !name.startsWith(QLatin1Char('.'));
    }
};

ExplorerPanel::ExplorerPanel(QWidget *parent)
    : QWidget(parent)
{
    // Plain QWidget subclasses ignore stylesheet backgrounds without this.
    setAttribute(Qt::WA_StyledBackground, true);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName(QStringLiteral("explorerTitle"));
    QToolButton *expandButton = makeHeaderButton(QStringLiteral("\u229E"), QStringLiteral("Expand All"));
    QToolButton *collapseButton =
        makeHeaderButton(QStringLiteral("\u229F"), QStringLiteral("Collapse All"));
    m_parentButton = makeHeaderButton(QStringLiteral("\u2191"), QStringLiteral("Parent Folder"));
    QToolButton *closeButton = makeHeaderButton(QStringLiteral("\u2715"),
                                                QStringLiteral("Close Explorer"));
    connect(expandButton, &QToolButton::clicked, this, &ExplorerPanel::expandAll);
    connect(collapseButton, &QToolButton::clicked, this, &ExplorerPanel::collapseAll);
    connect(m_parentButton, &QToolButton::clicked, this, &ExplorerPanel::scopeToParent);
    connect(closeButton, &QToolButton::clicked, this, &ExplorerPanel::closeRequested);

    m_model = new ExplorerModel(this);
    connect(m_model, &ExplorerModel::fileRenamed, this,
            [this](const QString &directory, const QString &oldName, const QString &newName) {
                const QDir dir(directory);
                const QString from = dir.filePath(oldName);
                const QString to = dir.filePath(newName);
                if (!m_newFilePath.isEmpty() && samePath(from, m_newFilePath))
                    m_newFilePath = to;
                emit pathMoved(from, to);
            });
    connect(m_model, &ExplorerModel::directoryLoaded, this, [this](const QString &path) {
        if (samePath(path, m_rootPath))
            showRootReady();
        if (m_expandingAll)
            expandChildren(m_filter->mapFromSource(m_model->index(path)));
        if (!m_pendingDirectory.isEmpty() && samePath(path, m_pendingDirectory)) {
            const QString directory = m_pendingDirectory;
            m_pendingDirectory.clear();
            insertNewFile(directory);
        }
    });
    connect(m_model, &ExplorerModel::listingFailed, this, [this](const QString &path) {
        if (samePath(path, m_rootPath))
            showRootFailed();
    });

    m_filter = new DirectoryFilter(this);
    m_filter->setSourceModel(m_model);
    m_filter->hideDotFolders = QSettings().value(QStringLiteral("hideDotFolders"), true).toBool();

    m_tree = new ExplorerTree(this);
    m_tree->setModel(m_filter);
    auto *delegate = new ExplorerDelegate(this, m_tree);
    m_tree->setItemDelegate(delegate);
    connect(delegate, &QAbstractItemDelegate::commitData, this, [this](QWidget *editor) {
        if (m_newFilePath.isEmpty())
            return;
        if (auto *line = qobject_cast<QLineEdit *>(editor))
            m_typedName = line->text().trimmed();
    });
    connect(delegate, &QAbstractItemDelegate::closeEditor, this,
            [this](QWidget *, QAbstractItemDelegate::EndEditHint hint) { finishNewFile(int(hint)); });
    m_tree->setHeaderHidden(true);
    // The platform default (20) steps each nested folder quite far to the right.
    m_tree->setIndentation(12);
    // Single clicks already toggle folders; a double click would toggle twice more.
    m_tree->setExpandsOnDoubleClick(false);
    // Renaming only starts from the context menu.
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setFocusPolicy(Qt::NoFocus);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);

    // Clicks on the branch arrow expand natively and never emit clicked().
    connect(m_tree, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        const QModelIndex source = m_filter->mapToSource(index);
        if (m_model->isPlaceholder(source))
            return;
        if (m_model->isDir(source))
            m_tree->setExpanded(index, !m_tree->isExpanded(index));
        else
            emit fileActivated(m_model->filePath(source));
    });
    connect(m_tree, &QTreeView::expanded, this, [this](const QModelIndex &index) {
        m_model->setExpanded(m_filter->mapToSource(index), true);
    });
    connect(m_tree, &QTreeView::collapsed, this, [this](const QModelIndex &index) {
        m_expandingAll = false;
        m_model->setExpanded(m_filter->mapToSource(index), false);
        // Dropping rows inside the collapse handler reenters the view.
        const QPersistentModelIndex source = m_filter->mapToSource(index);
        QTimer::singleShot(0, this, [this, source] {
            if (source.isValid() && !m_model->isVirtual(source))
                m_model->markStale(source);
        });
    });
    connect(m_tree, &QTreeView::customContextMenuRequested, this, &ExplorerPanel::showContextMenu);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 6, 0);
    header->setSpacing(2);
    header->addWidget(m_titleLabel, 1);
    header->addWidget(expandButton);
    header->addWidget(collapseButton);
    header->addWidget(m_parentButton);
    header->addWidget(closeButton);

    m_loading = new QLabel(QStringLiteral("Loading…"), this);
    m_loading->setObjectName(QStringLiteral("explorerLoading"));
    m_loading->setAlignment(Qt::AlignCenter);

    m_stack = new QStackedWidget(this);
    m_stack->setFrameShape(QFrame::NoFrame);
    m_stack->addWidget(m_loading);
    m_stack->addWidget(m_tree);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addWidget(m_stack, 1);
}

QToolButton *ExplorerPanel::makeHeaderButton(const QString &glyph, const QString &toolTip)
{
    auto *button = new QToolButton(this);
    button->setText(glyph);
    button->setToolTip(toolTip);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(22, 22);
    return button;
}

void ExplorerPanel::setIconTheme(const FileIconTheme *theme)
{
    m_model->setIconTheme(theme);
}

void ExplorerPanel::setGroups(const QString &directory, const QList<FileGroup> &groups)
{
    m_model->setGroups(directory, groups);
}

void ExplorerPanel::setHidden(const QStringList &patterns)
{
    m_filter->setHidden(patterns);
}

void ExplorerPanel::setRootPath(const QString &path, bool warnIfMissing)
{
    if (path.isEmpty())
        return;
    m_restoreRoot = m_announcedRoot;
    m_warnIfMissing = warnIfMissing;
    m_rootPath = path;
    m_expandingAll = false;
    m_titleLabel->setText(directoryName(path));
    const WslPath::LinuxPath linux = WslPath::toLinux(path);
    m_titleLabel->setToolTip(linux.valid() ? linux.path : QDir::toNativeSeparators(path));
    m_parentButton->setEnabled(canScopeUp(path));
    m_stack->setCurrentWidget(m_loading);
    m_model->setRootPath(path);
    m_tree->setRootIndex(m_filter->mapFromSource(m_model->rootIndex()));
}

void ExplorerPanel::showRootReady()
{
    m_stack->setCurrentWidget(m_tree);
    if (samePath(m_announcedRoot, m_rootPath))
        return;
    m_announcedRoot = m_rootPath;
    emit rootPathChanged(m_rootPath);
}

void ExplorerPanel::showRootFailed()
{
    m_stack->setCurrentWidget(m_tree);
    if (samePath(m_announcedRoot, m_rootPath))
        return;
    const QString restore = m_restoreRoot;
    const bool warn = m_warnIfMissing;
    m_restoreRoot.clear();
    if (warn)
        this->warn(QStringLiteral("That folder was not found."));
    if (!restore.isEmpty() && !samePath(restore, m_rootPath))
        setRootPath(restore, false);
    else if (!warn)
        emit closeRequested();
}

void ExplorerPanel::scopeToParent()
{
    setRootPath(parentDirectory(m_rootPath));
}

void ExplorerPanel::expandAll()
{
    m_expandingAll = true;
    expandChildren(m_tree->rootIndex());
}

void ExplorerPanel::collapseAll()
{
    m_tree->collapseAll();
    m_expandingAll = false;
}

void ExplorerPanel::expandChildren(const QModelIndex &parent)
{
    for (int row = 0; row < m_filter->rowCount(parent); ++row) {
        const QModelIndex child = m_filter->index(row, 0, parent);
        const QModelIndex source = m_filter->mapToSource(child);
        if (m_model->isPlaceholder(source) || !m_model->isDir(source))
            continue;
        m_tree->expand(child);
        // Folders not loaded yet continue from directoryLoaded instead.
        if (!m_model->canFetchMore(source))
            expandChildren(child);
    }
}

void ExplorerPanel::showContextMenu(const QPoint &pos)
{
    const QModelIndex index = m_tree->indexAt(pos);
    const QModelIndex source = m_filter->mapToSource(index);
    const bool onItem = index.isValid() && !m_model->isPlaceholder(source);
    const bool virtualFolder = onItem && m_model->isVirtual(source);
    const QString path = onItem && !virtualFolder ? m_model->filePath(source) : m_rootPath;
    const bool onFolder = onItem && m_model->isDir(source) && !virtualFolder;
    // Pasting onto a file drops into the folder that holds it.
    const QString pasteTarget = !onItem || onFolder ? path : QFileInfo(path).absolutePath();

    QMenu menu(this);
    if (onFolder)
        menu.addAction(QStringLiteral("Scope to This Folder"), this, [this, path] { setRootPath(path); });
    menu.addAction(QStringLiteral("Scope to Parent Folder"), this, &ExplorerPanel::scopeToParent)
        ->setEnabled(m_parentButton->isEnabled());
    menu.addSeparator();
    menu.addAction(QStringLiteral("Expand All"), this, &ExplorerPanel::expandAll);
    menu.addAction(QStringLiteral("Collapse All"), this, &ExplorerPanel::collapseAll);
    QAction *hideDotFolders = menu.addAction(QStringLiteral("Hide Dot Folders"));
    hideDotFolders->setCheckable(true);
    hideDotFolders->setChecked(m_filter->hideDotFolders);
    connect(hideDotFolders, &QAction::toggled, this, [this](bool hide) {
        m_filter->setHideDotFolders(hide);
        QSettings().setValue(QStringLiteral("hideDotFolders"), hide);
    });
    if (virtualFolder) {
        const int group = m_model->virtualGroup(source);
        QAction *recursive = menu.addAction(QStringLiteral("Apply in Subfolders"));
        recursive->setCheckable(true);
        if (group >= 0 && group < m_model->groups().size())
            recursive->setChecked(m_model->groups().at(group).recursive);
        connect(recursive, &QAction::toggled, this, [this, group](bool checked) {
            setGroupRecursive(group, checked);
        });
    }
    if (!virtualFolder) {
        menu.addSeparator();
        const QString newFileDirectory = !onItem ? m_rootPath
                                           : onFolder ? path
                                                      : QFileInfo(path).absolutePath();
        menu.addAction(QStringLiteral("New File"), this, [this, newFileDirectory] {
            createFile(newFileDirectory);
        })->setEnabled(!newFileDirectory.isEmpty());
        menu.addAction(QStringLiteral("New Virtual Folder"), this, [this, newFileDirectory] {
            createVirtualFolder(newFileDirectory);
        })->setEnabled(!newFileDirectory.isEmpty());
        menu.addSeparator();
        menu.addAction(QStringLiteral("Reveal in File Explorer"), this, [this, path, onItem] {
            if (onItem)
                FileOperations::revealInFileExplorer(path);
            else
                QDesktopServices::openUrl(QUrl::fromLocalFile(m_rootPath));
        });
        menu.addSeparator();
        if (onItem) {
            menu.addAction(QStringLiteral("Cut"), this, [this, path] { setClipboard(path, true); });
            menu.addAction(QStringLiteral("Copy"), this, [this, path] { setClipboard(path, false); });
        }
        menu.addAction(QStringLiteral("Paste"), this, [this, pasteTarget] { paste(pasteTarget); })
            ->setEnabled(!m_clipboardPath.isEmpty());
        if (onItem) {
            menu.addSeparator();
            menu.addAction(QStringLiteral("Rename"), this, [this, index] { m_tree->edit(index); });
            menu.addAction(QStringLiteral("Delete"), this, [this, path] { deletePath(path); });
        }
    }
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void ExplorerPanel::setClipboard(const QString &path, bool cut)
{
    m_clipboardPath = path;
    m_clipboardCut = cut;
    m_tree->viewport()->update();
}

void ExplorerPanel::paste(const QString &directory)
{
    const QString source = m_clipboardPath;
    const QString target = QDir::cleanPath(directory);
    if (target.compare(source, Qt::CaseInsensitive) == 0
        || target.startsWith(source + QLatin1Char('/'), Qt::CaseInsensitive)) {
        warn(QStringLiteral("A folder cannot be pasted into itself."));
        return;
    }
    // Moving an item into the folder it already lives in changes nothing.
    if (m_clipboardCut
        && QFileInfo(source).absolutePath().compare(target, Qt::CaseInsensitive) == 0) {
        m_clipboardPath.clear();
        m_tree->viewport()->update();
        return;
    }

    const QString destination = FileOperations::availableDestination(source, target);
    if (!m_clipboardCut) {
        if (!FileOperations::copy(source, destination))
            warn(QStringLiteral("Could not copy \"%1\".").arg(QDir::toNativeSeparators(source)));
        else
            m_model->refresh(target);
        return;
    }
    if (!FileOperations::move(source, destination)) {
        warn(QStringLiteral("Could not move \"%1\".").arg(QDir::toNativeSeparators(source)));
        return;
    }
    m_model->discard(source);
    m_model->refresh(target);
    // A cut item can only be pasted once; it no longer exists at the source.
    m_clipboardPath.clear();
    m_tree->viewport()->update();
    emit pathMoved(source, destination);
}

void ExplorerPanel::commitGroups(const QList<FileGroup> &groups)
{
    const QString directory = m_model->projectDirectory().isEmpty() ? m_rootPath : m_model->projectDirectory();
    m_model->setGroups(directory, groups);
    emit virtualFoldersEdited(groups);
}

void ExplorerPanel::createVirtualFolder(const QString &directory)
{
    const QString base = m_model->projectDirectory().isEmpty() ? m_rootPath : m_model->projectDirectory();
    const QString cleanBase = QDir::cleanPath(base);
    const QString cleanDirectory = QDir::cleanPath(directory);
    const bool inside = samePath(cleanDirectory, cleanBase)
                        || cleanDirectory.startsWith(cleanBase + QLatin1Char('/'), Qt::CaseInsensitive);
    if (!inside) {
        warn(QStringLiteral("Virtual folders belong to the folder that contains .crlf."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("New Virtual Folder"));
    auto *nameEdit = new QLineEdit(&dialog);
    auto *recursive = new QCheckBox(QStringLiteral("Apply in subfolders"), &dialog);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *form = new QFormLayout(&dialog);
    form->addRow(QStringLiteral("Name"), nameEdit);
    form->addRow(recursive);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString name = nameEdit->text().trimmed();
    if (name.isEmpty() || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        warn(QStringLiteral("Enter a folder name."));
        return;
    }

    QString relative = QDir(cleanBase).relativeFilePath(cleanDirectory);
    if (relative == QLatin1String("."))
        relative.clear();

    QList<FileGroup> groups = m_model->groups();
    for (const FileGroup &existing : groups) {
        if (existing.name.compare(name, Qt::CaseInsensitive) == 0
            && existing.path.compare(relative, Qt::CaseInsensitive) == 0) {
            warn(QStringLiteral("A virtual folder with that name already exists here."));
            return;
        }
    }

    FileGroup group;
    group.name = name;
    group.path = relative;
    group.recursive = recursive->isChecked();
    groups.append(group);
    commitGroups(groups);
}

void ExplorerPanel::addToVirtualFolder(const QString &filePath, int groupIndex)
{
    QList<FileGroup> groups = m_model->groups();
    if (groupIndex < 0 || groupIndex >= groups.size())
        return;
    const QString name = QFileInfo(filePath).fileName();
    FileGroup &group = groups[groupIndex];
    for (const QString &pattern : group.files) {
        const bool exact = !pattern.contains(QLatin1Char('*')) && !pattern.contains(QLatin1Char('?'));
        if (exact && pattern.compare(name, Qt::CaseInsensitive) == 0)
            return;
    }
    for (int i = 0; i < groups.size(); ++i) {
        if (i == groupIndex)
            continue;
        QStringList &patterns = groups[i].files;
        for (int n = patterns.size() - 1; n >= 0; --n) {
            const QString &pattern = patterns.at(n);
            const bool exact = !pattern.contains(QLatin1Char('*')) && !pattern.contains(QLatin1Char('?'));
            if (exact && pattern.compare(name, Qt::CaseInsensitive) == 0)
                patterns.removeAt(n);
        }
    }
    group.files.append(name);
    commitGroups(groups);
}

void ExplorerPanel::setGroupRecursive(int groupIndex, bool recursive)
{
    QList<FileGroup> groups = m_model->groups();
    if (groupIndex < 0 || groupIndex >= groups.size() || groups.at(groupIndex).recursive == recursive)
        return;
    groups[groupIndex].recursive = recursive;
    commitGroups(groups);
}

void ExplorerPanel::confirmAndMove(const QString &source, const QString &directory)
{
    const QString target = QDir::cleanPath(directory);
    if (target.compare(source, Qt::CaseInsensitive) == 0
        || target.startsWith(source + QLatin1Char('/'), Qt::CaseInsensitive)) {
        warn(QStringLiteral("A folder cannot be moved into itself."));
        return;
    }
    // Dropping an item onto the folder it already lives in changes nothing.
    if (QFileInfo(source).absolutePath().compare(target, Qt::CaseInsensitive) == 0)
        return;

    const QFileInfo info(source);
    const QString kind = info.isDir() ? QStringLiteral("folder") : QStringLiteral("file");
    const auto choice = QMessageBox::question(
        this, QCoreApplication::applicationName(),
        QStringLiteral("Do you wish to move %1 \"%2\" to \"%3\"?")
            .arg(kind, info.fileName(), directoryName(target)));
    if (choice != QMessageBox::Yes)
        return;

    const QString destination = QDir(target).filePath(info.fileName());
    if (QFileInfo::exists(destination)) {
        warn(QStringLiteral("\"%1\" already exists.").arg(info.fileName()));
        return;
    }
    if (!FileOperations::move(source, destination)) {
        warn(QStringLiteral("Could not move \"%1\".").arg(QDir::toNativeSeparators(source)));
        return;
    }
    m_model->discard(source);
    m_model->refresh(target);
    if (m_clipboardCut && m_clipboardPath.compare(source, Qt::CaseInsensitive) == 0)
        m_clipboardPath.clear();
    m_tree->viewport()->update();
    emit pathMoved(source, destination);
}

void ExplorerPanel::createFile(const QString &directory)
{
    const QModelIndex sourceParent = m_model->index(directory);
    const QModelIndex parent = m_filter->mapFromSource(sourceParent);
    if (!sourceParent.isValid())
        return;
    // The root's children are already the visible rows.
    if (parent.isValid() && parent != m_tree->rootIndex())
        m_tree->expand(parent);
    // A folder that has not been listed yet reports the new row only after
    // its first load. index() inserts a file once that listing is in place.
    if (!m_model->isListed(sourceParent)) {
        m_pendingDirectory = directory;
        if (m_model->canFetchMore(sourceParent))
            m_model->fetchMore(sourceParent);
        return;
    }
    insertNewFile(directory);
}

void ExplorerPanel::insertNewFile(const QString &directory)
{
    const QModelIndex directoryIndex = m_model->index(directory);
    QString name = QStringLiteral("untitled");
    for (int n = 2; m_model->containsName(directoryIndex, name); ++n)
        name = QStringLiteral("untitled %1").arg(n);
    const QString path = QDir(directory).filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        warn(QStringLiteral("Could not create \"%1\".").arg(name));
        return;
    }
    file.close();

    // Asking the model for the path inserts the row. A file created beside
    // the model is otherwise invisible until the folder is scoped again.
    const QModelIndex index = m_filter->mapFromSource(m_model->index(path));
    if (index.isValid() && index.parent().isValid() && index.parent() != m_tree->rootIndex())
        m_tree->expand(index.parent());
    if (!index.isValid()) {
        QFile::remove(path);
        warn(QStringLiteral("Could not create \"%1\".").arg(name));
        return;
    }
    m_newFileOriginal = path;
    m_newFilePath = path;
    m_typedName.clear();
    m_tree->scrollTo(index);
    m_tree->edit(index);
    auto *line = qobject_cast<QLineEdit *>(m_tree->indexWidget(index));
    if (line == nullptr) {
        m_newFileOriginal.clear();
        m_newFilePath.clear();
        m_model->remove(m_filter->mapToSource(index));
        warn(QStringLiteral("Could not create \"%1\".").arg(name));
        return;
    }
    line->clear();
    line->setPlaceholderText(QStringLiteral("File name"));
    line->setFocus();
}

void ExplorerPanel::finishNewFile(int hint)
{
    const QString original = m_newFileOriginal;
    const QString current = m_newFilePath;
    const QString typed = m_typedName;
    m_newFileOriginal.clear();
    m_newFilePath.clear();
    m_typedName.clear();
    if (original.isEmpty())
        return;

    const bool cancelled = hint == int(QAbstractItemDelegate::RevertModelCache) || typed.isEmpty();
    const bool renamed = current.compare(original, Qt::CaseInsensitive) != 0;
    if (cancelled || !renamed) {
        const QModelIndex index = m_model->index(original);
        if (index.isValid())
            m_model->remove(index);
        else
            QFile::remove(original);
        if (!cancelled && !typed.isEmpty())
            warn(QStringLiteral("Could not create \"%1\".").arg(typed));
        return;
    }
    emit fileActivated(current);
}

void ExplorerPanel::deletePath(const QString &path)
{
    const QString name = QFileInfo(path).fileName();
    if (WslPath::toLinux(path).valid()) {
        const auto choice = QMessageBox::question(
            this, QCoreApplication::applicationName(), QStringLiteral("Delete \"%1\"?").arg(name));
        if (choice != QMessageBox::Yes)
            return;
        const bool removed = QFileInfo(path).isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
        if (!removed)
            warn(QStringLiteral("Could not delete \"%1\".").arg(name));
        else
            m_model->discard(path);
        return;
    }

    const auto choice = QMessageBox::question(
        this, QCoreApplication::applicationName(),
        QStringLiteral("Move \"%1\" to the Recycle Bin?").arg(name));
    if (choice != QMessageBox::Yes)
        return;
    if (!FileOperations::moveToTrash(path))
        warn(QStringLiteral("Could not delete \"%1\".").arg(QDir::toNativeSeparators(path)));
    else
        m_model->discard(path);
}

void ExplorerPanel::warn(const QString &message)
{
    QMessageBox::warning(this, QCoreApplication::applicationName(), message);
}
