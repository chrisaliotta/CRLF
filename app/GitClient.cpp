#include "app/GitClient.h"

#include "app/ProcessLaunch.h"
#include "app/ProcessOrchestrator.h"
#include "git/GitDiscovery.h"
#include "git/GitParse.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

namespace {

// git writes progress with carriage returns. The log keeps the last state of
// each line.
QString operationText(const QByteArray &bytes)
{
    QStringList lines = QString::fromUtf8(bytes).split(QLatin1Char('\n'));
    for (QString &line : lines)
        line = line.section(QLatin1Char('\r'), -1);
    return lines.join(QLatin1Char('\n'));
}

// A prompt would wait forever on a pipe, so git fails instead and says why.
// Optional locks stay off so a status read never blocks the user's own git.
// Literal pathspecs keep a file named with * or [ from matching others.
// WSLENV carries all three into a distro.
QProcessEnvironment gitEnvironment()
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    environment.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    environment.insert(QStringLiteral("GIT_LITERAL_PATHSPECS"), QStringLiteral("1"));
    const QString shared = QStringLiteral("GIT_TERMINAL_PROMPT:GIT_OPTIONAL_LOCKS:GIT_LITERAL_PATHSPECS");
    const QString existing = environment.value(QStringLiteral("WSLENV"));
    environment.insert(QStringLiteral("WSLENV"), existing.isEmpty() ? shared : existing + QLatin1Char(':') + shared);
    return environment;
}

} // namespace

GitJob statusJob(const QString &root)
{
    GitJob job;
    job.root = root;
    job.title = QStringLiteral("git status");
    job.arguments = statusArguments();
    job.read = GitRead::Status;
    return job;
}

struct GitClient::Repository
{
    QString root;
    QString gitDirectory;
    QList<GitJob> queue;
    QProcess *process = nullptr;
    ProcessLaunch launch;
    // The job that holds `key`. Empty key: nothing is running.
    GitJob job;
    QString key;
    int token = 0;
    QByteArray output;
    QByteArray errors;
};

GitClient::GitClient(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<GitStatus>();
    qRegisterMetaType<GitBranch>();
    qRegisterMetaType<QList<GitBranch>>();
    qRegisterMetaType<GitCommit>();
    qRegisterMetaType<QList<GitCommit>>();
}

GitClient::~GitClient()
{
    cancelAll();
    for (Repository *repo : std::as_const(m_repositories)) {
        if (repo->process != nullptr && repo->process->state() != QProcess::NotRunning)
            repo->process->setParent(nullptr);
    }
    qDeleteAll(m_repositories);
}

GitClient::Repository *GitClient::repository(const QString &root)
{
    Repository *repo = m_repositories.value(root);
    if (repo != nullptr)
        return repo;
    repo = new Repository;
    repo->root = root;
    repo->gitDirectory = gitDirectory(root);
    repo->process = new QProcess(this);
    repo->process->setProcessEnvironment(gitEnvironment());
    connect(repo->process, &QProcess::readyReadStandardOutput, this,
            [repo] { repo->output += repo->process->readAllStandardOutput(); });
    connect(repo->process, &QProcess::readyReadStandardError, this, [this, repo] {
        const QByteArray bytes = repo->process->readAllStandardError();
        repo->errors += bytes;
        m_orchestrator->appendLog(repo->key, QString::fromUtf8(bytes));
    });
    connect(repo->process, &QProcess::finished, this, [this, repo](int exitCode, QProcess::ExitStatus status) {
        repo->output += repo->process->readAllStandardOutput();
        if (repo->process->processChannelMode() == QProcess::SeparateChannels)
            repo->errors += repo->process->readAllStandardError();
        complete(repo, status == QProcess::CrashExit ? -1 : exitCode, QString::fromUtf8(repo->errors).trimmed());
    });
    connect(repo->process, &QProcess::errorOccurred, this, [this, repo](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || repo->launch.retrying() || repo->key.isEmpty())
            return;
        complete(repo, -1, QStringLiteral("git could not be started: %1").arg(repo->process->errorString()));
    });
    m_repositories.insert(root, repo);
    return repo;
}

void GitClient::enqueue(const GitJob &job)
{
    Repository *repo = repository(job.root);
    if (job.read == GitRead::Status) {
        for (const GitJob &waiting : std::as_const(repo->queue)) {
            if (waiting.read == GitRead::Status)
                return;
        }
    }
    repo->queue.append(job);
    if (repo->key.isEmpty())
        startNext(repo);
}

void GitClient::startNext(Repository *repo)
{
    if (repo->queue.isEmpty())
        return;
    repo->job = repo->queue.takeFirst();
    repo->key = QStringLiteral("git:%1").arg(++m_serial);
    const QString key = repo->key;
    const QString detail = QStringLiteral("git ") + repo->job.arguments.join(QLatin1Char(' '));
    m_orchestrator->submit(key, repo->job.title, QDir::toNativeSeparators(repo->root), 0, detail,
                           [this, repo, key] { launch(repo, key); }, ProcessLane::Hold);
}

void GitClient::launch(Repository *repo, const QString &key)
{
    if (repo->key != key)
        return;
    repo->token = m_orchestrator->currentToken(key);
    repo->output.clear();
    repo->errors.clear();
    const bool operation = repo->job.read == GitRead::Operation;
    if (operation)
        emit operationStarted(repo->root, repo->job.title);
    repo->process->setProcessChannelMode(operation ? QProcess::MergedChannels : QProcess::SeparateChannels);
    const QStringList arguments = QStringList{QStringLiteral("-c"), QStringLiteral("color.ui=false"),
                                              QStringLiteral("-c"), QStringLiteral("core.quotepath=false")}
                                  + repo->job.arguments;
    repo->launch.startProgram(repo->process, repo->root, QStringLiteral("git"), arguments);
    if (repo->process->state() == QProcess::NotRunning)
        return;
    repo->process->write(repo->job.input);
    repo->process->closeWriteChannel();
}

void GitClient::complete(Repository *repo, int exitCode, const QString &error)
{
    const GitJob job = repo->job;
    m_orchestrator->release(repo->key, repo->token);
    repo->key.clear();
    const bool ok = exitCode == 0;
    switch (job.read) {
    case GitRead::Status:
        if (ok) {
            GitStatus status = parseStatus(repo->output);
            status.merging = QFileInfo::exists(QDir(repo->gitDirectory).filePath(QStringLiteral("MERGE_HEAD")));
            emit statusReady(repo->root, status);
        } else {
            emit failed(repo->root, error);
        }
        break;
    case GitRead::Operation: {
        QString output = operationText(repo->output);
        if (output.trimmed().isEmpty() && !error.isEmpty())
            output = error;
        emit operationFinished(repo->root, job.title, exitCode, output);
        repo->queue.removeIf([](const GitJob &waiting) { return waiting.read == GitRead::Status; });
        repo->queue.prepend(statusJob(repo->root));
        break;
    }
    case GitRead::Bytes:
        emit bytesReady(job.ticket, ok, repo->output);
        break;
    case GitRead::Branches:
        if (ok)
            emit branchesReady(repo->root, parseBranches(repo->output));
        else
            emit failed(repo->root, error);
        break;
    case GitRead::Commits:
        if (ok)
            emit commitsReady(repo->root, parseLog(repo->output));
        else
            emit failed(repo->root, error);
        break;
    }
    startNext(repo);
}

void GitClient::cancel(const QString &key)
{
    for (Repository *repo : std::as_const(m_repositories)) {
        if (repo->key == key) {
            cancelRepository(repo->root);
            return;
        }
    }
}

void GitClient::cancelRepository(const QString &root)
{
    Repository *repo = m_repositories.value(root);
    if (repo == nullptr || repo->key.isEmpty())
        return;
    if (repo->process->state() == QProcess::NotRunning) {
        complete(repo, -1, QStringLiteral("Cancelled."));
        return;
    }
    repo->launch.stopTree(this);
    repo->process->kill();
}

void GitClient::cancelAll()
{
    for (Repository *repo : std::as_const(m_repositories)) {
        repo->queue.clear();
        if (repo->process->state() != QProcess::NotRunning) {
            repo->launch.stopTree(this);
            repo->process->kill();
        }
    }
}
