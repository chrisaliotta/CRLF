#include "app/SourceControlPanel.h"

#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QShortcut>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace {

enum Role { RootRole = Qt::UserRole + 1, KindRole, GroupRole, ChangeRole };
enum Kind { RepositoryRow, GroupRow, FileRow };

QString repositoryTitle(const QString &root, const GitStatus *status)
{
    QString title = QDir(root).dirName();
    if (status == nullptr)
        return title + QStringLiteral("  …");
    title += QStringLiteral("  \u2387 ") + (status->branch.isEmpty() ? status->head.left(7) : status->branch);
    if (status->ahead > 0)
        title += QStringLiteral(" \u2191%1").arg(status->ahead);
    if (status->behind > 0)
        title += QStringLiteral(" \u2193%1").arg(status->behind);
    if (status->merging)
        title += QStringLiteral("  (merging)");
    return title;
}

QString groupKey(const QString &root, int group)
{
    return root + QLatin1Char('|') + QString::number(group);
}

} // namespace

SourceControlPanel::SourceControlPanel(SourceControl *control, QWidget *parent)
    : QWidget(parent)
    , m_control(control)
{
    setAttribute(Qt::WA_StyledBackground, true);

    auto *title = new QLabel(QStringLiteral("Source Control"), this);
    title->setObjectName(QStringLiteral("sourceControlTitle"));
    QToolButton *refresh = makeButton(QStringLiteral("\u27F3"), QStringLiteral("Refresh"));
    QToolButton *fetchAll = makeButton(QStringLiteral("\u21E3"), QStringLiteral("Fetch All Repositories"));
    QToolButton *close = makeButton(QStringLiteral("\u2715"), QStringLiteral("Close Source Control"));
    connect(refresh, &QToolButton::clicked, m_control, qOverload<>(&SourceControl::refresh));
    connect(fetchAll, &QToolButton::clicked, m_control, &SourceControl::fetchAll);
    connect(close, &QToolButton::clicked, this, &SourceControlPanel::closeRequested);
    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 6, 0);
    header->setSpacing(2);
    header->addWidget(title, 1);
    header->addWidget(refresh);
    header->addWidget(fetchAll);
    header->addWidget(close);

    m_repositories = new QComboBox(this);
    connect(m_repositories, &QComboBox::currentIndexChanged, this, [this] { refreshActions(); });

    m_message = new QPlainTextEdit(this);
    m_message->setPlaceholderText(QStringLiteral("Message (Ctrl+Enter to commit)"));
    m_message->setFixedHeight(m_message->fontMetrics().lineSpacing() * 4 + 12);
    auto *commitKey = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), m_message);
    commitKey->setContext(Qt::WidgetShortcut);
    connect(commitKey, &QShortcut::activated, this, &SourceControlPanel::commit);

    m_commit = makeButton(QStringLiteral("\u2713 Commit"), QStringLiteral("Commit the staged changes"));
    m_amend = makeButton(QStringLiteral("Amend"), QStringLiteral("Replace the last commit"));
    m_amend->setCheckable(true);
    QToolButton *pull = makeButton(QStringLiteral("\u21E3"), QStringLiteral("Pull (fast-forward only)"));
    QToolButton *push = makeButton(QStringLiteral("\u21E1"), QStringLiteral("Push"));
    QToolButton *fetch = makeButton(QStringLiteral("\u27F2"), QStringLiteral("Fetch"));
    m_branch = makeButton(QStringLiteral("\u2387 branch"), QStringLiteral("Branches"));
    connect(m_commit, &QToolButton::clicked, this, &SourceControlPanel::commit);
    connect(pull, &QToolButton::clicked, this, [this] { m_control->pull(currentRoot()); });
    connect(push, &QToolButton::clicked, this, [this] { m_control->push(currentRoot()); });
    connect(fetch, &QToolButton::clicked, this, [this] { m_control->fetch(currentRoot()); });
    connect(m_branch, &QToolButton::clicked, this, [this] {
        showBranchMenu(currentRoot(), m_branch->mapToGlobal(QPoint(0, m_branch->height())));
    });
    auto *actions = new QHBoxLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(2);
    actions->addWidget(m_commit);
    actions->addWidget(m_amend);
    actions->addStretch(1);
    actions->addWidget(m_branch);
    actions->addWidget(fetch);
    actions->addWidget(pull);
    actions->addWidget(push);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderHidden(true);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setIndentation(12);
    m_tree->setFocusPolicy(Qt::NoFocus);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setExpandsOnDoubleClick(false);
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) { activate(item); });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &SourceControlPanel::showContextMenu);
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *item) {
        m_collapsed.insert(item->data(0, KindRole).toInt() == RepositoryRow
                               ? item->data(0, RootRole).toString()
                               : groupKey(item->data(0, RootRole).toString(), item->data(0, GroupRole).toInt()));
    });
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) {
        m_collapsed.remove(item->data(0, KindRole).toInt() == RepositoryRow
                               ? item->data(0, RootRole).toString()
                               : groupKey(item->data(0, RootRole).toString(), item->data(0, GroupRole).toInt()));
    });

    auto *body = new QWidget(this);
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(8, 4, 8, 0);
    bodyLayout->setSpacing(4);
    bodyLayout->addWidget(m_repositories);
    bodyLayout->addWidget(m_message);
    bodyLayout->addLayout(actions);
    bodyLayout->addWidget(m_tree, 1);

    m_empty = new QLabel(this);
    m_empty->setObjectName(QStringLiteral("sourceControlEmpty"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_empty);
    m_stack->addWidget(body);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addWidget(m_stack, 1);

    connect(m_control, &SourceControl::repositoriesChanged, this, &SourceControlPanel::rebuild);
    connect(m_control, &SourceControl::statusChanged, this, &SourceControlPanel::rebuild);
    connect(m_control, &SourceControl::branchesListed, this,
            [this](const QString &root, const QList<GitBranch> &branches) {
                if (root == m_branchMenuRoot)
                    openBranchMenu(root, branches);
            });
    connect(m_control, &SourceControl::commitsListed, this,
            [this](const QString &root, const QString &path, const QList<GitCommit> &commits) {
                if (root == m_historyRoot && path == m_historyPath)
                    openHistory(root, path, commits);
            });
    rebuild();
}

QToolButton *SourceControlPanel::makeButton(const QString &text, const QString &toolTip)
{
    auto *button = new QToolButton(this);
    button->setText(text);
    button->setToolTip(toolTip);
    button->setFocusPolicy(Qt::NoFocus);
    if (text.size() == 1)
        button->setFixedSize(22, 22);
    else
        button->setFixedHeight(22);
    return button;
}

void SourceControlPanel::setToneColors(const QColor &added, const QColor &modified, const QColor &deleted,
                                       const QColor &conflicted)
{
    m_colors[int(GitTone::Added)] = added;
    m_colors[int(GitTone::Modified)] = modified;
    m_colors[int(GitTone::Deleted)] = deleted;
    m_colors[int(GitTone::Conflicted)] = conflicted;
    rebuild();
}

QString SourceControlPanel::currentRoot() const
{
    return m_repositories->currentData().toString();
}

void SourceControlPanel::setCurrentRoot(const QString &root)
{
    const int index = m_repositories->findData(root);
    if (index >= 0)
        m_repositories->setCurrentIndex(index);
}

void SourceControlPanel::refreshRepositoryPicker()
{
    const QString current = currentRoot();
    const QSignalBlocker blocker(m_repositories);
    m_repositories->clear();
    for (const QString &root : m_control->roots())
        m_repositories->addItem(repositoryTitle(root, m_control->status(root)), root);
    const int index = m_repositories->findData(current);
    m_repositories->setCurrentIndex(index >= 0 ? index : 0);
    m_repositories->setVisible(m_control->roots().size() > 1);
}

void SourceControlPanel::refreshActions()
{
    const GitStatus *status = m_control->status(currentRoot());
    m_branch->setText(status == nullptr ? QStringLiteral("\u2387")
                                        : QStringLiteral("\u2387 ")
                                              + (status->branch.isEmpty() ? status->head.left(7) : status->branch));
}

void SourceControlPanel::rebuild()
{
    if (!m_control->enabled()) {
        m_empty->setText(QStringLiteral("Source control is off for this folder.\nAdd \"git\": true to .crlf to turn it on."));
        m_stack->setCurrentWidget(m_empty);
        return;
    }
    if (m_control->roots().isEmpty()) {
        m_empty->setText(QStringLiteral("No repositories were found in this folder."));
        m_stack->setCurrentWidget(m_empty);
        return;
    }
    m_stack->setCurrentIndex(1);
    refreshRepositoryPicker();
    refreshActions();

    QString selected;
    if (QTreeWidgetItem *item = m_tree->currentItem())
        selected = item->data(0, RootRole).toString() + QLatin1Char('|') + item->text(0);
    const int scroll = m_tree->verticalScrollBar()->value();
    m_tree->clear();
    for (const QString &root : m_control->roots())
        addRepository(root);
    if (!selected.isEmpty()) {
        for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
            if ((*it)->data(0, RootRole).toString() + QLatin1Char('|') + (*it)->text(0) == selected) {
                m_tree->setCurrentItem(*it);
                break;
            }
        }
    }
    m_tree->verticalScrollBar()->setValue(scroll);
}

void SourceControlPanel::addRepository(const QString &root)
{
    const GitStatus *status = m_control->status(root);
    auto *repository = new QTreeWidgetItem(m_tree);
    repository->setText(0, repositoryTitle(root, status));
    repository->setToolTip(0, QDir::toNativeSeparators(root));
    repository->setData(0, RootRole, root);
    repository->setData(0, KindRole, RepositoryRow);
    QFont bold = repository->font(0);
    bold.setBold(true);
    repository->setFont(0, bold);
    repository->setExpanded(!m_collapsed.contains(root));
    if (status == nullptr)
        return;

    const auto addGroup = [&](Group group, const QString &label) {
        QList<int> members;
        for (int i = 0; i < status->changes.size(); ++i) {
            const GitChange &change = status->changes.at(i);
            const bool belongs = group == Conflicts ? change.conflicted
                                 : group == Staged  ? change.staged()
                                                    : change.unstaged() || change.untracked();
            if (belongs)
                members.append(i);
        }
        if (members.isEmpty())
            return;
        auto *item = new QTreeWidgetItem(repository);
        item->setText(0, label);
        item->setText(1, QString::number(members.size()));
        item->setData(0, RootRole, root);
        item->setData(0, KindRole, GroupRow);
        item->setData(0, GroupRole, group);
        for (int index : std::as_const(members)) {
            const GitChange &change = status->changes.at(index);
            const GitBadge badge = badgeFor(change);
            const QFileInfo info(change.path);
            auto *file = new QTreeWidgetItem(item);
            const QString folder = info.path() == QLatin1String(".") ? QString() : info.path();
            file->setText(0, folder.isEmpty() ? info.fileName() : info.fileName() + QStringLiteral("  ") + folder);
            file->setText(1, badge.letter);
            file->setToolTip(0, change.originalPath.isEmpty() ? change.path
                                                              : change.originalPath + QStringLiteral(" \u2192 ") + change.path);
            file->setData(0, RootRole, root);
            file->setData(0, KindRole, FileRow);
            file->setData(0, GroupRole, group);
            file->setData(0, ChangeRole, index);
            const QColor color = m_colors[int(badge.tone)];
            if (color.isValid()) {
                file->setForeground(0, color);
                file->setForeground(1, color);
            }
        }
        item->setExpanded(!m_collapsed.contains(groupKey(root, group)));
    };
    addGroup(Conflicts, QStringLiteral("Merge Conflicts"));
    addGroup(Staged, QStringLiteral("Staged Changes"));
    addGroup(Changes, QStringLiteral("Changes"));
}

QList<GitChange> SourceControlPanel::changesAt(QTreeWidgetItem *item) const
{
    const GitStatus *status = m_control->status(item->data(0, RootRole).toString());
    QList<GitChange> changes;
    if (item->data(0, KindRole).toInt() == FileRow) {
        changes.append(status->changes.at(item->data(0, ChangeRole).toInt()));
        return changes;
    }
    for (int i = 0; i < item->childCount(); ++i)
        changes.append(status->changes.at(item->child(i)->data(0, ChangeRole).toInt()));
    return changes;
}

GitDiffRequest SourceControlPanel::diffFor(const QString &root, Group group, const GitChange &change) const
{
    GitDiffRequest request;
    request.root = root;
    request.path = change.path;
    if (group == Staged) {
        const QString before = change.originalPath.isEmpty() ? change.path : change.originalPath;
        if (change.index != QLatin1Char('A'))
            request.leftObject = QStringLiteral("HEAD:") + before;
        if (change.index != QLatin1Char('D'))
            request.rightObject = QLatin1Char(':') + change.path;
        request.leftLabel = QStringLiteral("HEAD");
        request.rightLabel = QStringLiteral("Index");
    } else {
        if (!change.untracked())
            request.leftObject = QLatin1Char(':') + change.path;
        request.rightWorking = true;
        request.leftLabel = change.untracked() ? QStringLiteral("(untracked)") : QStringLiteral("Index");
        request.rightLabel = QStringLiteral("Working Tree");
    }
    return request;
}

void SourceControlPanel::activate(QTreeWidgetItem *item)
{
    const QString root = item->data(0, RootRole).toString();
    setCurrentRoot(root);
    if (item->data(0, KindRole).toInt() != FileRow) {
        item->setExpanded(!item->isExpanded());
        return;
    }
    const GitChange change = changesAt(item).first();
    const auto group = Group(item->data(0, GroupRole).toInt());
    if (group == Conflicts) {
        emit fileActivated(m_control->absolutePath(root, change.path));
        return;
    }
    emit diffRequested(diffFor(root, group, change));
}

void SourceControlPanel::showContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    if (item == nullptr)
        return;
    const QString root = item->data(0, RootRole).toString();
    setCurrentRoot(root);
    const int kind = item->data(0, KindRole).toInt();
    QMenu menu(this);
    if (kind == RepositoryRow) {
        const GitStatus *status = m_control->status(root);
        menu.addAction(QStringLiteral("Fetch"), this, [this, root] { m_control->fetch(root); });
        menu.addAction(QStringLiteral("Pull"), this, [this, root] { m_control->pull(root); });
        menu.addAction(QStringLiteral("Push"), this, [this, root] { m_control->push(root); });
        menu.addSeparator();
        const QPoint at = m_tree->viewport()->mapToGlobal(pos);
        menu.addAction(QStringLiteral("Branches…"), this, [this, root, at] { showBranchMenu(root, at); });
        menu.addAction(QStringLiteral("Stash Changes"), this, [this, root] { m_control->stash(root); });
        menu.addAction(QStringLiteral("Pop Stash"), this, [this, root] { m_control->popStash(root); });
        if (status != nullptr && status->merging)
            menu.addAction(QStringLiteral("Abort Merge"), this, [this, root] { m_control->abortMerge(root); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("Refresh"), this, [this, root] { m_control->refresh(root); });
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
        return;
    }

    const auto group = Group(item->data(0, GroupRole).toInt());
    const QList<GitChange> changes = changesAt(item);
    QStringList paths;
    for (const GitChange &change : changes)
        paths.append(change.path);
    if (kind == FileRow) {
        const GitChange change = changes.first();
        if (group != Conflicts)
            menu.addAction(QStringLiteral("Open Changes"), this,
                           [this, root, group, change] { emit diffRequested(diffFor(root, group, change)); });
        menu.addAction(QStringLiteral("Open File"), this,
                       [this, root, change] { emit fileActivated(m_control->absolutePath(root, change.path)); });
        menu.addSeparator();
    }
    const QString all = kind == GroupRow ? QStringLiteral(" All") : QString();
    if (group == Staged) {
        menu.addAction(QStringLiteral("Unstage") + all, this, [this, root, paths] { m_control->unstage(root, paths); });
    } else {
        menu.addAction(QStringLiteral("Stage") + all, this, [this, root, paths] { m_control->stage(root, paths); });
    }
    if (group == Changes) {
        menu.addAction(QStringLiteral("Discard") + all + QStringLiteral(" Changes"), this, [this, root, changes] {
            const QString what = changes.size() == 1 ? changes.first().path
                                                     : QStringLiteral("%1 files").arg(changes.size());
            const auto choice = QMessageBox::warning(
                this, QStringLiteral("Discard Changes"),
                QStringLiteral("Discard the changes to %1? Untracked files are deleted. This cannot be undone.").arg(what),
                QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
            if (choice == QMessageBox::Discard)
                m_control->discard(root, changes);
        });
    }
    if (kind == FileRow) {
        menu.addSeparator();
        const QString path = changes.first().path;
        menu.addAction(QStringLiteral("File History"), this, [this, root, path] { showHistory(root, path); });
    }
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void SourceControlPanel::commit()
{
    const QString root = currentRoot();
    const GitStatus *status = m_control->status(root);
    const QString text = m_message->toPlainText().trimmed();
    if (status == nullptr || text.isEmpty())
        return;
    const bool amend = m_amend->isChecked();
    bool staged = false;
    for (const GitChange &change : status->changes)
        staged = staged || change.staged();
    if (!staged && !amend) {
        if (status->changes.isEmpty())
            return;
        const auto choice = QMessageBox::question(
            this, QStringLiteral("Commit"),
            QStringLiteral("There are no staged changes. Stage every change in %1 and commit?").arg(QDir(root).dirName()),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
        if (choice != QMessageBox::Yes)
            return;
        m_control->run(root, QStringLiteral("git add -A"), {QStringLiteral("add"), QStringLiteral("-A")});
    }
    m_control->commit(root, text, amend);
    m_message->clear();
    m_amend->setChecked(false);
}

void SourceControlPanel::focusMessage()
{
    m_message->setFocus();
}

void SourceControlPanel::showBranchMenu(const QString &root, const QPoint &globalPos)
{
    if (root.isEmpty())
        return;
    m_branchMenuRoot = root;
    m_branchMenuPosition = globalPos;
    m_control->listBranches(root);
}

void SourceControlPanel::openBranchMenu(const QString &root, const QList<GitBranch> &branches)
{
    m_branchMenuRoot.clear();
    QMenu menu(this);
    QMenu *remote = nullptr;
    QMenu *mergeMenu = new QMenu(QStringLiteral("Merge Into Current"), &menu);
    QMenu *deleteMenu = new QMenu(QStringLiteral("Delete Branch"), &menu);
    for (const GitBranch &branch : branches) {
        if (branch.remote) {
            if (remote == nullptr) {
                menu.addSeparator();
                remote = menu.addMenu(QStringLiteral("Remote Branches"));
            }
            remote->addAction(branch.name, this, [this, root, branch] { m_control->switchBranch(root, branch); });
        } else {
            QAction *action = menu.addAction(branch.name, this, [this, root, branch] {
                if (!branch.current)
                    m_control->switchBranch(root, branch);
            });
            action->setCheckable(true);
            action->setChecked(branch.current);
            if (!branch.current) {
                deleteMenu->addAction(branch.name, this, [this, root, branch] {
                    const auto choice = QMessageBox::question(
                        this, QStringLiteral("Delete Branch"), QStringLiteral("Delete the branch %1?").arg(branch.name),
                        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
                    if (choice == QMessageBox::Yes)
                        m_control->deleteBranch(root, branch.name);
                });
            }
        }
        if (!branch.current)
            mergeMenu->addAction(branch.name, this, [this, root, branch] { m_control->merge(root, branch.name); });
    }
    menu.addSeparator();
    menu.addAction(QStringLiteral("New Branch…"), this, [this, root] {
        bool accepted = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("New Branch"), QStringLiteral("Branch name:"),
                                                   QLineEdit::Normal, QString(), &accepted)
                                 .trimmed();
        if (accepted && !name.isEmpty())
            m_control->createBranch(root, name);
    });
    mergeMenu->setEnabled(!mergeMenu->isEmpty());
    deleteMenu->setEnabled(!deleteMenu->isEmpty());
    menu.addMenu(mergeMenu);
    menu.addMenu(deleteMenu);
    menu.exec(m_branchMenuPosition);
}

void SourceControlPanel::showHistory(const QString &root, const QString &path)
{
    m_historyRoot = root;
    m_historyPath = path;
    m_control->listCommits(root, path);
}

void SourceControlPanel::openHistory(const QString &root, const QString &path, const QList<GitCommit> &commits)
{
    m_historyRoot.clear();
    m_historyPath.clear();
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("History of %1").arg(QFileInfo(path).fileName()));
    dialog->resize(640, 420);
    auto *list = new QListWidget(dialog);
    for (const GitCommit &commit : commits) {
        auto *row = new QListWidgetItem(QStringLiteral("%1  %2  %3 \u2014 %4")
                                            .arg(commit.date, commit.id.left(7), commit.subject, commit.author),
                                        list);
        row->setData(Qt::UserRole, commit.id);
    }
    auto *hint = new QLabel(QStringLiteral("Double-click a commit to see what it changed in this file."), dialog);
    auto *layout = new QVBoxLayout(dialog);
    layout->addWidget(hint);
    layout->addWidget(list, 1);
    connect(list, &QListWidget::itemActivated, this, [this, root, path](QListWidgetItem *row) {
        const QString id = row->data(Qt::UserRole).toString();
        GitDiffRequest request;
        request.root = root;
        request.path = path;
        request.leftObject = id + QStringLiteral("^:") + path;
        request.rightObject = id + QLatin1Char(':') + path;
        request.leftLabel = id.left(7) + QLatin1Char('^');
        request.rightLabel = id.left(7);
        emit diffRequested(request);
    });
    dialog->show();
}
