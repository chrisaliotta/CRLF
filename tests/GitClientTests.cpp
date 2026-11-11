// Confirms that GitClient drives a real git: jobs for one repository run in
// the order they were queued, a commit message reaches git on standard input,
// every operation is followed by a fresh status, and object reads return the
// committed bytes. Needs git on PATH.

#include "app/GitClient.h"
#include "app/ProcessOrchestrator.h"
#include "git/GitParse.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace {

void git(const QString &root, const QStringList &arguments)
{
    QProcess process;
    process.setWorkingDirectory(root);
    process.start(QStringLiteral("git"), arguments);
    QVERIFY(process.waitForFinished(30000));
    QCOMPARE(process.exitCode(), 0);
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

GitJob operation(const QString &root, const QStringList &arguments, const QByteArray &input = {})
{
    GitJob job;
    job.root = root;
    job.title = QStringLiteral("git ") + arguments.first();
    job.arguments = arguments;
    job.input = input;
    return job;
}

} // namespace

class GitClientTests : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_directory = std::make_unique<QTemporaryDir>();
        QVERIFY(m_directory->isValid());
        m_root = QDir(m_directory->path()).absolutePath();
        git(m_root, {QStringLiteral("init"), QStringLiteral("-q"), QStringLiteral("-b"), QStringLiteral("main")});
        git(m_root, {QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("Test")});
        git(m_root, {QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("test@example.com")});
        git(m_root, {QStringLiteral("config"), QStringLiteral("core.autocrlf"), QStringLiteral("false")});
        m_client.setOrchestrator(&m_orchestrator);
    }

    // git writes its objects read-only, which QTemporaryDir cannot delete.
    void cleanup()
    {
        QDirIterator files(m_root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (files.hasNext())
            QFile::setPermissions(files.next(), QFile::ReadOwner | QFile::WriteOwner);
        m_directory.reset();
    }

    // An untracked file is reported; staging and committing it through the
    // queue leaves a clean status on a real commit.
    void stageAndCommitThroughTheQueue()
    {
        writeFile(QDir(m_root).filePath(QStringLiteral("notes file.txt")), "first\n");
        QSignalSpy statuses(&m_client, &GitClient::statusReady);
        QSignalSpy finished(&m_client, &GitClient::operationFinished);

        m_client.enqueue(statusJob(m_root));
        QVERIFY(statuses.wait(30000));
        GitStatus status = statuses.takeFirst().at(1).value<GitStatus>();
        QCOMPARE(status.head, QStringLiteral("(initial)"));
        QCOMPARE(status.changes.size(), 1);
        QVERIFY(status.changes.first().untracked());

        m_client.enqueue(operation(m_root, {QStringLiteral("add"), QStringLiteral("--"), QStringLiteral("notes file.txt")}));
        m_client.enqueue(operation(m_root, {QStringLiteral("commit"), QStringLiteral("-F"), QStringLiteral("-")},
                                   "Add notes\n\nWith a body line."));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 30000);
        QCOMPARE(finished.at(0).at(2).toInt(), 0);
        QCOMPARE(finished.at(1).at(2).toInt(), 0);
        // One status follows each operation, before the next queued job runs.
        QTRY_COMPARE_WITH_TIMEOUT(statuses.size(), 2, 30000);
        QVERIFY(statuses.first().at(1).value<GitStatus>().changes.first().staged());
        status = statuses.last().at(1).value<GitStatus>();
        QCOMPARE(status.branch, QStringLiteral("main"));
        QVERIFY(status.head != QLatin1String("(initial)"));
        QVERIFY(status.changes.isEmpty());

        QSignalSpy commits(&m_client, &GitClient::commitsReady);
        GitJob log;
        log.root = m_root;
        log.title = QStringLiteral("git log");
        log.arguments = logArguments(10, QString());
        log.read = GitRead::Commits;
        m_client.enqueue(log);
        QVERIFY(commits.wait(30000));
        const QList<GitCommit> history = commits.first().at(1).value<QList<GitCommit>>();
        QCOMPARE(history.size(), 1);
        QCOMPARE(history.first().subject, QStringLiteral("Add notes"));
    }

    // The change bars and the diff view read staged and committed text this way.
    void readsObjectBytes()
    {
        writeFile(QDir(m_root).filePath(QStringLiteral("a.txt")), "one\ntwo\n");
        git(m_root, {QStringLiteral("add"), QStringLiteral("a.txt")});
        git(m_root, {QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("a")});

        QSignalSpy bytes(&m_client, &GitClient::bytesReady);
        GitJob read;
        read.root = m_root;
        read.title = QStringLiteral("git cat-file");
        read.arguments = {QStringLiteral("cat-file"), QStringLiteral("blob"), QStringLiteral("HEAD:a.txt")};
        read.read = GitRead::Bytes;
        read.ticket = 7;
        m_client.enqueue(read);
        read.arguments.last() = QStringLiteral(":missing.txt");
        read.ticket = 8;
        m_client.enqueue(read);
        QTRY_COMPARE_WITH_TIMEOUT(bytes.size(), 2, 30000);
        QCOMPARE(bytes.at(0).at(0).toInt(), 7);
        QVERIFY(bytes.at(0).at(1).toBool());
        QCOMPARE(bytes.at(0).at(2).toByteArray(), QByteArray("one\ntwo\n"));
        QCOMPARE(bytes.at(1).at(0).toInt(), 8);
        QVERIFY(!bytes.at(1).at(1).toBool());
    }

private:
    std::unique_ptr<QTemporaryDir> m_directory;
    QString m_root;
    ProcessOrchestrator m_orchestrator;
    GitClient m_client;
};

QTEST_GUILESS_MAIN(GitClientTests)

#include "GitClientTests.moc"
