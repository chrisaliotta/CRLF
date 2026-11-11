#pragma once

#include "app/GitClient.h"
#include "git/GitTypes.h"
#include "git/LineDiff.h"
#include "io/ProjectCommands.h"

#include <QHash>
#include <QObject>
#include <QStringList>

#include <functional>

class ProcessOrchestrator;

// Two versions of one file to compare. An object is a git revision path such
// as "HEAD:src/main.cpp" or ":src/main.cpp" (the index). An empty left object
// leaves that side empty, as for a new file. With `rightWorking`, the right
// side is the working copy: `workingText` when the file is open in an editor,
// otherwise the file on disk.
struct GitDiffRequest
{
    QString root;
    // Relative to `root`, with forward slashes.
    QString path;
    QString leftObject;
    QString rightObject;
    bool rightWorking = false;
    bool hasWorkingText = false;
    QString workingText;
    QString leftLabel;
    QString rightLabel;
};

// The source-control state of the scoped folder, on the UI thread. It finds
// the repositories, keeps each one's last status, and turns user actions into
// GitClient jobs. Widgets read from it and call it; none of them start git.
class SourceControl : public QObject
{
    Q_OBJECT

public:
    SourceControl(ProcessOrchestrator *orchestrator, GitClient *client, QObject *parent = nullptr);

    // The .crlf `git` settings, the scoped folder, and the .crlf `hide` list.
    // Repositories are searched again only when one of these changed.
    void setProject(const GitSettings &settings, const QString &scope, const QStringList &hide);
    bool enabled() const { return m_settings.enabled && !m_scope.isEmpty(); }
    const QStringList &roots() const { return m_roots; }
    // Null until the repository's first status read has finished.
    const GitStatus *status(const QString &root) const;
    // The deepest tracked repository that holds `path`, or empty.
    QString repositoryFor(const QString &path) const;
    QString relativePath(const QString &root, const QString &path) const;
    QString absolutePath(const QString &root, const QString &relative) const;
    // Every changed path, and each folder above it up to its repository,
    // keyed by the lower-case absolute path.
    QHash<QString, GitBadge> badges() const;

    void refresh();
    void refresh(const QString &root);

    // Each of these is one git process, followed by a status read.
    void run(const QString &root, const QString &title, const QStringList &arguments,
             const QByteArray &input = {});
    void stage(const QString &root, const QStringList &paths);
    void unstage(const QString &root, const QStringList &paths);
    // Tracked files go back to their staged text. Untracked files are deleted.
    void discard(const QString &root, const QList<GitChange> &changes);
    void commit(const QString &root, const QString &message, bool amend);
    void fetch(const QString &root);
    void fetchAll();
    void pull(const QString &root);
    // A branch with no upstream is pushed to origin and starts tracking it.
    void push(const QString &root);
    // A remote branch is checked out as a local branch that tracks it.
    void switchBranch(const QString &root, const GitBranch &branch);
    void createBranch(const QString &root, const QString &name);
    void deleteBranch(const QString &root, const QString &name);
    void merge(const QString &root, const QString &branch);
    void abortMerge(const QString &root);
    void stash(const QString &root);
    void popStash(const QString &root);
    // Stops the running job of `root`, from the Output panel's Stop.
    void cancel(const QString &root);

    // `reply` runs on the UI thread. `found` is false when git has no such object.
    void readObject(const QString &root, const QString &object,
                    std::function<void(bool found, const QByteArray &bytes)> reply);
    void listBranches(const QString &root);
    void listCommits(const QString &root, const QString &path);
    // Reads both sides and diffs them on the pool. `done` runs on the UI thread.
    void loadDiff(const GitDiffRequest &request,
                  std::function<void(const QString &left, const QString &right, const QList<LineChange> &changes)> done);

signals:
    void repositoriesChanged();
    void statusChanged(const QString &root);
    // The index or HEAD of `root` may have moved: after an operation, a
    // commit made elsewhere, or a file staged from a terminal. Editors
    // compare against the index, so they read it again.
    void baseChanged(const QString &root);
    void branchesListed(const QString &root, const QList<GitBranch> &branches);
    void commitsListed(const QString &root, const QString &path, const QList<GitCommit> &commits);
    void operationStarted(const QString &root, const QString &title);
    void operationFinished(const QString &root, const QString &title, int exitCode, const QString &output);
    void message(const QString &text);

private:
    void discover();
    void applyRoots(const QStringList &roots);
    void applyStatus(const QString &root, const GitStatus &status);
    void enqueue(const GitJob &job);
    void read(const QString &root, const QString &title, const QStringList &arguments, GitRead read,
              int ticket = 0);

    ProcessOrchestrator *m_orchestrator;
    GitClient *m_client;
    GitSettings m_settings;
    QString m_scope;
    QStringList m_hide;
    int m_generation = 0;
    QStringList m_roots;
    QHash<QString, GitStatus> m_status;
    QHash<QString, QString> m_historyPaths;
    QHash<int, std::function<void(bool, const QByteArray &)>> m_replies;
    int m_ticket = 0;
};
