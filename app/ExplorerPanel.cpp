#include "app/ExplorerPanel.h"

#include "io/FileOperations.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

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

    m_model = new QFileSystemModel(this);
    m_model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    // Writable so inline edits rename files on disk.
    m_model->setReadOnly(false);
    connect(m_model, &QFileSystemModel::fileRenamed, this,
            [this](const QString &directory, const QString &oldName, const QString &newName) {
                const QDir dir(directory);
                emit pathMoved(dir.filePath(oldName), dir.filePath(newName));
            });
    connect(m_model, &QFileSystemModel::directoryLoaded, this, [this](const QString &path) {
        if (m_expandingAll)
            expandChildren(m_model->index(path));
    });

    m_tree = new QTreeView(this);
    m_tree->setModel(m_model);
    m_tree->setHeaderHidden(true);
    for (int column = 1; column < m_model->columnCount(); ++column)
        m_tree->hideColumn(column);
    // Single clicks already toggle folders; a double click would toggle twice more.
    m_tree->setExpandsOnDoubleClick(false);
    // Renaming only starts from the context menu.
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setFocusPolicy(Qt::NoFocus);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);

    // Clicks on the branch arrow expand natively and never emit clicked().
    connect(m_tree, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        if (m_model->isDir(index))
            m_tree->setExpanded(index, !m_tree->isExpanded(index));
        else
            emit fileActivated(m_model->filePath(index));
    });
    connect(m_tree, &QTreeView::collapsed, this, [this] { m_expandingAll = false; });
    connect(m_tree, &QTreeView::customContextMenuRequested, this, &ExplorerPanel::showContextMenu);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 6, 0);
    header->setSpacing(2);
    header->addWidget(m_titleLabel, 1);
    header->addWidget(expandButton);
    header->addWidget(collapseButton);
    header->addWidget(m_parentButton);
    header->addWidget(closeButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addWidget(m_tree, 1);
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

void ExplorerPanel::setRootPath(const QString &path)
{
    m_rootPath = path;
    m_expandingAll = false;
    m_titleLabel->setText(QDir(path).dirName());
    m_titleLabel->setToolTip(QDir::toNativeSeparators(path));
    // A drive root has no parent to scope up to.
    m_parentButton->setEnabled(!QDir(path).isRoot());
    m_tree->setRootIndex(m_model->setRootPath(path));
    emit rootPathChanged(path);
}

void ExplorerPanel::scopeToParent()
{
    QDir dir(m_rootPath);
    dir.cdUp();
    setRootPath(dir.absolutePath());
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
    for (int row = 0; row < m_model->rowCount(parent); ++row) {
        const QModelIndex child = m_model->index(row, 0, parent);
        if (!m_model->isDir(child))
            continue;
        m_tree->expand(child);
        // Folders not loaded yet continue from directoryLoaded instead.
        if (!m_model->canFetchMore(child))
            expandChildren(child);
    }
}

void ExplorerPanel::showContextMenu(const QPoint &pos)
{
    const QModelIndex index = m_tree->indexAt(pos);
    const bool onItem = index.isValid();
    const QString path = onItem ? m_model->filePath(index) : m_rootPath;
    const bool onFolder = onItem && m_model->isDir(index);
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
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void ExplorerPanel::setClipboard(const QString &path, bool cut)
{
    m_clipboardPath = path;
    m_clipboardCut = cut;
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
        return;
    }

    const QString destination = FileOperations::availableDestination(source, target);
    if (!m_clipboardCut) {
        if (!FileOperations::copy(source, destination))
            warn(QStringLiteral("Could not copy \"%1\".").arg(QDir::toNativeSeparators(source)));
        return;
    }
    if (!FileOperations::move(source, destination)) {
        warn(QStringLiteral("Could not move \"%1\".").arg(QDir::toNativeSeparators(source)));
        return;
    }
    // A cut item can only be pasted once; it no longer exists at the source.
    m_clipboardPath.clear();
    emit pathMoved(source, destination);
}

void ExplorerPanel::deletePath(const QString &path)
{
    const auto choice = QMessageBox::question(
        this, QCoreApplication::applicationName(),
        QStringLiteral("Move \"%1\" to the Recycle Bin?").arg(QFileInfo(path).fileName()));
    if (choice != QMessageBox::Yes)
        return;
    if (!FileOperations::moveToTrash(path))
        warn(QStringLiteral("Could not delete \"%1\".").arg(QDir::toNativeSeparators(path)));
}

void ExplorerPanel::warn(const QString &message)
{
    QMessageBox::warning(this, QCoreApplication::applicationName(), message);
}
