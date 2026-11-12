// Confirms that AgentSession speaks the Agent Client Protocol with a real
// child process: initialize and session/new reach ready, a prompt's file
// link reaches the agent, the agent's fs/read_text_file comes back to the
// editor with an editor path and takes the editor's reply, an approved
// terminal/create runs a command whose exit code and output the agent reads
// back, and the turn ends with the agent's stop reason. A second case sets
// one of the session's config options. The agent is
// tests/fake_acp_agent.py, so the test needs python on PATH.

#include "app/AgentSession.h"
#include "app/ProcessOrchestrator.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class AgentSessionTests : public QObject
{
    Q_OBJECT

private slots:
    void promptRoundTrip()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString root = QDir(directory.path()).absolutePath();
        const QString notes = QDir(root).filePath(QStringLiteral("notes.txt"));
        QFile file(notes);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("on disk");
        file.close();

        ProcessOrchestrator orchestrator;
        AgentSession session;
        session.setOrchestrator(&orchestrator);
        QSignalSpy ready(&session, &AgentSession::ready);
        QSignalSpy requested(&session, &AgentSession::requested);
        QSignalSpy updated(&session, &AgentSession::updated);
        QSignalSpy ended(&session, &AgentSession::turnEnded);

        const AgentLaunch launch{QStringLiteral("Fake"), QStringLiteral("python \"%1\"").arg(QStringLiteral(FAKE_AGENT)),
                                 root, CommandVariables()};
        orchestrator.submit(QStringLiteral("agent"), launch.name, root, 0, launch.command,
                            [&session, launch] { session.start(launch); }, ProcessLane::Hold);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 30000);
        QCOMPARE(ready.first().at(0).toString(), QStringLiteral("Fake"));

        AgentPrompt prompt;
        prompt.text = QStringLiteral("Read the notes");
        prompt.files = {notes};
        session.prompt(prompt);

        // The editor answers from its buffer, which need not match the disk.
        QTRY_COMPARE_WITH_TIMEOUT(requested.size(), 1, 30000);
        const AgentRequest read = requested.at(0).at(0).value<AgentRequest>();
        QCOMPARE(read.method, QStringLiteral("fs/read_text_file"));
        QCOMPARE(QDir::cleanPath(read.params.value(QLatin1String("path")).toString()).toLower(), notes.toLower());
        session.reply(read.id, QJsonObject{{QStringLiteral("content"), QStringLiteral("in the buffer")}});

        QTRY_COMPARE_WITH_TIMEOUT(requested.size(), 2, 30000);
        const AgentRequest create = requested.at(1).at(0).value<AgentRequest>();
        QCOMPARE(create.method, QStringLiteral("terminal/create"));
        session.createTerminal(create);

        QTRY_COMPARE_WITH_TIMEOUT(ended.size(), 1, 30000);
        QCOMPARE(ended.first().at(0).toString(), QStringLiteral("end_turn"));
        QString text;
        for (const QList<QVariant> &arguments : std::as_const(updated)) {
            const AgentUpdate update = arguments.at(0).value<AgentUpdate>();
            if (update.kind == AgentUpdateKind::AgentText)
                text += update.text;
        }
        QCOMPARE(text, QStringLiteral("content=in the buffer|output=hello|exit=0"));
        session.stop();
    }

    // The settings row is drawn from what the agent reports: the list that
    // comes with the new session, the list it returns for a choice, and,
    // for a value it refuses, the list as it still stands.
    void setsAConfigOption()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString root = QDir(directory.path()).absolutePath();

        ProcessOrchestrator orchestrator;
        AgentSession session;
        session.setOrchestrator(&orchestrator);
        QSignalSpy ready(&session, &AgentSession::ready);
        QSignalSpy options(&session, &AgentSession::configOptionsChanged);
        QSignalSpy failed(&session, &AgentSession::failed);
        const auto model = [&options](int index) {
            const QList<AgentConfigOption> list = options.at(index).at(0).value<QList<AgentConfigOption>>();
            return list.size() == 1 && list.first().id == QLatin1String("model") ? list.first().currentValue.toString()
                                                                               : QString();
        };

        const AgentLaunch launch{QStringLiteral("Fake"), QStringLiteral("python \"%1\"").arg(QStringLiteral(FAKE_AGENT)),
                                 root, CommandVariables()};
        orchestrator.submit(QStringLiteral("agent"), launch.name, root, 0, launch.command,
                            [&session, launch] { session.start(launch); }, ProcessLane::Hold);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 30000);
        QCOMPARE(options.size(), 1);
        QCOMPARE(model(0), QStringLiteral("small"));

        session.setConfigOption(QStringLiteral("model"), QStringLiteral("large"));
        QTRY_COMPARE_WITH_TIMEOUT(options.size(), 2, 30000);
        QCOMPARE(model(1), QStringLiteral("large"));

        session.setConfigOption(QStringLiteral("model"), QStringLiteral("huge"));
        QTRY_COMPARE_WITH_TIMEOUT(options.size(), 3, 30000);
        QCOMPARE(failed.size(), 1);
        QCOMPARE(model(2), QStringLiteral("large"));
        session.stop();
    }
};

QTEST_GUILESS_MAIN(AgentSessionTests)

#include "AgentSessionTests.moc"
