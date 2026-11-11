#pragma once

#include "git/GitTypes.h"

#include <QHash>
#include <QObject>
#include <QStringList>

class ProcessOrchestrator;

// What a job's standard output is read as, and which signal reports it.
enum class GitRead { Status, Operation, Bytes, Branches, Commits };

// One git invocation in one repository.
struct GitJob
{
    QString root;
    // The Processes row and the Output log line, such as "git fetch".
    QString title;
    QStringList arguments;
    // Standard input. A commit message goes here, so it is never quoted.
    QByteArray input;
    GitRead read = GitRead::Operation;
    // Echoed on bytesReady so the caller can match the reply to its request.
    int ticket = 0;
};

// The status read of one repository.
GitJob statusJob(const QString &root);

// Runs git for every tracked repository. It lives on the orchestrator thread,
// like SyntaxRunner; the UI thread queues calls onto it. Jobs for one
// repository run one at a time, because two git processes writing the same
// index fail on index.lock. Different repositories run side by side. Each job
// is a Processes record keyed git:<n> until it exits. See docs/git.md.
class GitClient : public QObject
{
    Q_OBJECT

public:
    explicit GitClient(QObject *parent = nullptr);
    ~GitClient() override;

    void setOrchestrator(ProcessOrchestrator *orchestrator) { m_orchestrator = orchestrator; }

    // Waits behind any job already queued for the same repository. A status
    // read that is already waiting is not queued twice. An operation is
    // followed by a status read of its repository.
    void enqueue(const GitJob &job);
    // Stops the job holding this Processes key.
    void cancel(const QString &key);
    // Stops the running job of this repository. Waiting jobs still run.
    void cancelRepository(const QString &root);
    // Drops waiting jobs and stops running ones, for every repository.
    void cancelAll();

signals:
    void statusReady(const QString &root, const GitStatus &status);
    // `output` is standard output and standard error together.
    void operationStarted(const QString &root, const QString &title);
    void operationFinished(const QString &root, const QString &title, int exitCode, const QString &output);
    // `found` is false when git has no such object, such as a file that was
    // never committed.
    void bytesReady(int ticket, bool found, const QByteArray &bytes);
    void branchesReady(const QString &root, const QList<GitBranch> &branches);
    void commitsReady(const QString &root, const QList<GitCommit> &commits);
    // A status, branch, or log read that git refused. `message` is its
    // standard error.
    void failed(const QString &root, const QString &message);

private:
    struct Repository;

    Repository *repository(const QString &root);
    void startNext(Repository *repo);
    void launch(Repository *repo, const QString &key);
    void complete(Repository *repo, int exitCode, const QString &error);

    QHash<QString, Repository *> m_repositories;
    ProcessOrchestrator *m_orchestrator = nullptr;
    int m_serial = 0;
};
