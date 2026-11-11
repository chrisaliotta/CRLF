#include "app/SourceControl.h"

#include "app/ProcessOrchestrator.h"
#include "git/GitDiscovery.h"
#include "git/GitParse.h"
#include "io/DocumentFile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QSharedPointer>

namespace {

// A folder shows the strongest tone among the changes inside it.
int toneRank(GitTone tone)
{
    switch (tone) {
    case GitTone::Conflicted:
        return 3;
    case GitTone::Modified:
        return 2;
    case GitTone::Deleted:
        return 1;
    case GitTone::Added:
        break;
    }
    return 0;
}

// Staged entries. When this list changes, the index changed.
QStringList stagedSignature(const GitStatus &status)
{
    QStringList signature;
    for (const GitChange &change : status.changes) {
        if (change.staged() || change.conflicted)
            signature.append(change.path + QLatin1Char('|') + change.index);
    }
    return signature;
}

QString decodedText(const QByteArray &bytes)
{
    DocumentFile file;
    return file.decode(bytes);
}

QString folderName(const QString &root)
{
    return QDir(root).dirName();
}

} // namespace

SourceControl::SourceControl(ProcessOrchestrator *orchestrator, GitClient *client, QObject *parent)
    : QObject(parent)
    , m_orchestrator(orchestrator)
    , m_client(client)
{
    connect(client, &GitClient::statusReady, this, &SourceControl::applyStatus, Qt::QueuedConnection);
    connect(client, &GitClient::operationStarted, this, &SourceControl::operationStarted, Qt::QueuedConnection);
    connect(client, &GitClient::operationFinished, this,
            [this](const QString &root, const QString &title, int exitCode, const QString &output) {
                emit operationFinished(root, title, exitCode, output);
                if (exitCode != 0)
                    emit message(QStringLiteral("%1 failed in %2").arg(title, folderName(root)));
                emit baseChanged(root);
            },
            Qt::QueuedConnection);
    connect(client, &GitClient::bytesReady, this,
            [this](int ticket, bool found, const QByteArray &bytes) {
                const auto reply = m_replies.take(ticket);
                if (reply)
                    reply(found, bytes);
            },
            Qt::QueuedConnection);
    connect(client, &GitClient::branchesReady, this, &SourceControl::branchesListed, Qt::QueuedConnection);
    connect(client, &GitClient::commitsReady, this,
            [this](const QString &root, const QList<GitCommit> &commits) {
                emit commitsListed(root, m_historyPaths.value(root), commits);
            },
            Qt::QueuedConnection);
    connect(client, &GitClient::failed, this,
            [this](const QString &root, const QString &text) {
                emit message(QStringLiteral("git in %1: %2").arg(folderName(root), text.section(QLatin1Char('\n'), 0, 0)));
            },
            Qt::QueuedConnection);
}

void SourceControl::setProject(const GitSettings &settings, const QString &scope, const QStringList &hide)
{
    const bool same = settings.enabled == m_settings.enabled && settings.depth == m_settings.depth
                      && scope == m_scope && hide == m_hide;
    m_settings = settings;
    m_scope = scope;
    m_hide = hide;
    if (same)
        return;
    if (!enabled()) {
        ++m_generation;
        applyRoots({});
        return;
    }
    discover();
}

void SourceControl::discover()
{
    const int generation = ++m_generation;
    const QString scope = m_scope;
    const int depth = m_settings.depth;
    const QStringList hide = m_hide;
    QPointer<SourceControl> alive(this);
    m_orchestrator->submit(QStringLiteral("git-discover"), QStringLiteral("Find repositories"),
                           QDir::toNativeSeparators(scope), 0, QStringLiteral("depth %1").arg(depth),
                           [alive, generation, scope, depth, hide] {
                               const QStringList roots = findRepositories(scope, depth, hide);
                               QMetaObject::invokeMethod(QCoreApplication::instance(), [alive, generation, roots] {
                                   if (!alive.isNull() && alive->m_generation == generation)
                                       alive->applyRoots(roots);
                               }, Qt::QueuedConnection);
                           });
}

void SourceControl::applyRoots(const QStringList &roots)
{
    m_roots = roots;
    for (auto it = m_status.begin(); it != m_status.end();) {
        if (roots.contains(it.key()))
            ++it;
        else
            it = m_status.erase(it);
    }
    emit repositoriesChanged();
    refresh();
}

const GitStatus *SourceControl::status(const QString &root) const
{
    const auto it = m_status.constFind(root);
    return it == m_status.cend() ? nullptr : &it.value();
}

void SourceControl::applyStatus(const QString &root, const GitStatus &status)
{
    if (!m_roots.contains(root))
        return;
    const auto previous = m_status.constFind(root);
    const bool moved = previous == m_status.cend() || previous->head != status.head
                       || stagedSignature(*previous) != stagedSignature(status);
    m_status.insert(root, status);
    emit statusChanged(root);
    if (moved)
        emit baseChanged(root);
}

QString SourceControl::repositoryFor(const QString &path) const
{
    const QString clean = QDir::cleanPath(path);
    QString best;
    for (const QString &root : m_roots) {
        const bool inside = clean.compare(root, Qt::CaseInsensitive) == 0
                            || clean.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive);
        if (inside && root.size() > best.size())
            best = root;
    }
    return best;
}

QString SourceControl::relativePath(const QString &root, const QString &path) const
{
    return QDir(root).relativeFilePath(path);
}

QString SourceControl::absolutePath(const QString &root, const QString &relative) const
{
    return QDir::cleanPath(QDir(root).filePath(relative));
}

QHash<QString, GitBadge> SourceControl::badges() const
{
    QHash<QString, GitBadge> marks;
    for (auto it = m_status.cbegin(); it != m_status.cend(); ++it) {
        const QString root = it.key();
        for (const GitChange &change : it->changes) {
            const GitBadge badge = badgeFor(change);
            QString path = absolutePath(root, change.path);
            marks.insert(path.toLower(), badge);
            while (true) {
                path = QFileInfo(path).path();
                if (path.size() <= root.size())
                    break;
                const QString key = path.toLower();
                const auto existing = marks.constFind(key);
                if (existing == marks.cend() || toneRank(badge.tone) > toneRank(existing->tone))
                    marks.insert(key, {QChar(0x2022), badge.tone});
            }
        }
    }
    return marks;
}

void SourceControl::enqueue(const GitJob &job)
{
    QMetaObject::invokeMethod(m_client, [client = m_client, job] { client->enqueue(job); }, Qt::QueuedConnection);
}

void SourceControl::refresh()
{
    for (const QString &root : std::as_const(m_roots))
        refresh(root);
}

void SourceControl::refresh(const QString &root)
{
    enqueue(statusJob(root));
}

void SourceControl::run(const QString &root, const QString &title, const QStringList &arguments,
                        const QByteArray &input)
{
    GitJob job;
    job.root = root;
    job.title = title;
    job.arguments = arguments;
    job.input = input;
    enqueue(job);
}

void SourceControl::read(const QString &root, const QString &title, const QStringList &arguments, GitRead read,
                         int ticket)
{
    GitJob job;
    job.root = root;
    job.title = title;
    job.arguments = arguments;
    job.read = read;
    job.ticket = ticket;
    enqueue(job);
}

void SourceControl::stage(const QString &root, const QStringList &paths)
{
    run(root, QStringLiteral("git add"), QStringList{QStringLiteral("add"), QStringLiteral("--")} + paths);
}

void SourceControl::unstage(const QString &root, const QStringList &paths)
{
    // reset, unlike restore --staged, also works before the first commit.
    run(root, QStringLiteral("git reset"),
        QStringList{QStringLiteral("reset"), QStringLiteral("-q"), QStringLiteral("--")} + paths);
}

void SourceControl::discard(const QString &root, const QList<GitChange> &changes)
{
    QStringList tracked;
    QStringList untracked;
    for (const GitChange &change : changes)
        (change.untracked() ? untracked : tracked).append(change.path);
    if (!tracked.isEmpty())
        run(root, QStringLiteral("git restore"),
            QStringList{QStringLiteral("restore"), QStringLiteral("--worktree"), QStringLiteral("--")} + tracked);
    if (!untracked.isEmpty())
        run(root, QStringLiteral("git clean"),
            QStringList{QStringLiteral("clean"), QStringLiteral("-f"), QStringLiteral("--")} + untracked);
}

void SourceControl::commit(const QString &root, const QString &text, bool amend)
{
    QStringList arguments{QStringLiteral("commit"), QStringLiteral("-F"), QStringLiteral("-")};
    if (amend)
        arguments.append(QStringLiteral("--amend"));
    run(root, amend ? QStringLiteral("git commit --amend") : QStringLiteral("git commit"), arguments, text.toUtf8());
}

void SourceControl::fetch(const QString &root)
{
    run(root, QStringLiteral("git fetch"), {QStringLiteral("fetch"), QStringLiteral("--all"), QStringLiteral("--prune")});
}

void SourceControl::fetchAll()
{
    for (const QString &root : std::as_const(m_roots))
        fetch(root);
}

void SourceControl::pull(const QString &root)
{
    run(root, QStringLiteral("git pull"), {QStringLiteral("pull"), QStringLiteral("--ff-only")});
}

void SourceControl::push(const QString &root)
{
    const GitStatus *current = status(root);
    if (current != nullptr && current->upstream.isEmpty() && !current->branch.isEmpty()) {
        run(root, QStringLiteral("git push"),
            {QStringLiteral("push"), QStringLiteral("--set-upstream"), QStringLiteral("origin"), current->branch});
        return;
    }
    run(root, QStringLiteral("git push"), {QStringLiteral("push")});
}

void SourceControl::switchBranch(const QString &root, const GitBranch &branch)
{
    if (branch.remote)
        run(root, QStringLiteral("git switch"), {QStringLiteral("switch"), QStringLiteral("--track"), branch.name});
    else
        run(root, QStringLiteral("git switch"), {QStringLiteral("switch"), branch.name});
}

void SourceControl::createBranch(const QString &root, const QString &name)
{
    run(root, QStringLiteral("git switch -c"), {QStringLiteral("switch"), QStringLiteral("-c"), name});
}

void SourceControl::deleteBranch(const QString &root, const QString &name)
{
    run(root, QStringLiteral("git branch -d"), {QStringLiteral("branch"), QStringLiteral("-d"), name});
}

void SourceControl::merge(const QString &root, const QString &branch)
{
    run(root, QStringLiteral("git merge"), {QStringLiteral("merge"), QStringLiteral("--no-edit"), branch});
}

void SourceControl::abortMerge(const QString &root)
{
    run(root, QStringLiteral("git merge --abort"), {QStringLiteral("merge"), QStringLiteral("--abort")});
}

void SourceControl::stash(const QString &root)
{
    run(root, QStringLiteral("git stash"),
        {QStringLiteral("stash"), QStringLiteral("push"), QStringLiteral("--include-untracked")});
}

void SourceControl::popStash(const QString &root)
{
    run(root, QStringLiteral("git stash pop"), {QStringLiteral("stash"), QStringLiteral("pop")});
}

void SourceControl::cancel(const QString &root)
{
    QMetaObject::invokeMethod(m_client, [client = m_client, root] { client->cancelRepository(root); },
                              Qt::QueuedConnection);
}

void SourceControl::readObject(const QString &root, const QString &object,
                               std::function<void(bool, const QByteArray &)> reply)
{
    const int ticket = ++m_ticket;
    m_replies.insert(ticket, std::move(reply));
    read(root, QStringLiteral("git cat-file"), {QStringLiteral("cat-file"), QStringLiteral("blob"), object},
         GitRead::Bytes, ticket);
}

void SourceControl::listBranches(const QString &root)
{
    read(root, QStringLiteral("git for-each-ref"), branchArguments(), GitRead::Branches);
}

void SourceControl::listCommits(const QString &root, const QString &path)
{
    m_historyPaths.insert(root, path);
    read(root, QStringLiteral("git log"), logArguments(200, path), GitRead::Commits);
}

void SourceControl::loadDiff(
    const GitDiffRequest &request,
    std::function<void(const QString &, const QString &, const QList<LineChange> &)> done)
{
    struct Pending
    {
        QString left;
        QString right;
        int remaining = 2;
    };
    auto pending = QSharedPointer<Pending>::create();
    QPointer<SourceControl> alive(this);
    const QString owner = QDir::toNativeSeparators(absolutePath(request.root, request.path));
    const auto finish = [this, alive, pending, done, owner] {
        if (--pending->remaining > 0)
            return;
        m_orchestrator->submit(QStringLiteral("git-diff"), QStringLiteral("Diff"), owner, 0, owner,
                               [alive, pending, done] {
                                   const QList<LineChange> changes =
                                       diffLines(splitLines(pending->left), splitLines(pending->right));
                                   QMetaObject::invokeMethod(QCoreApplication::instance(), [alive, pending, done, changes] {
                                       if (!alive.isNull())
                                           done(pending->left, pending->right, changes);
                                   }, Qt::QueuedConnection);
                               });
    };

    if (request.leftObject.isEmpty()) {
        finish();
    } else {
        readObject(request.root, request.leftObject, [pending, finish](bool found, const QByteArray &bytes) {
            if (found)
                pending->left = decodedText(bytes);
            finish();
        });
    }

    if (!request.rightWorking) {
        readObject(request.root, request.rightObject, [pending, finish](bool found, const QByteArray &bytes) {
            if (found)
                pending->right = decodedText(bytes);
            finish();
        });
    } else if (request.hasWorkingText) {
        pending->right = request.workingText;
        finish();
    } else {
        const QString path = absolutePath(request.root, request.path);
        m_orchestrator->submit(QStringLiteral("open:%1").arg(path), QStringLiteral("Open file"), owner, 0, path,
                               [alive, pending, finish, path] {
                                   QString text;
                                   if (QFileInfo::exists(path))
                                       text = DocumentFile(path).load();
                                   QMetaObject::invokeMethod(QCoreApplication::instance(), [alive, pending, finish, text] {
                                       if (alive.isNull())
                                           return;
                                       pending->right = text;
                                       finish();
                                   }, Qt::QueuedConnection);
                               });
    }
}
