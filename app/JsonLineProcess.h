#pragma once

#include "app/ProcessLaunch.h"
#include "io/CommandVariables.h"

#include <QJsonObject>
#include <QObject>

class ProcessOrchestrator;
class QProcess;

// One long-lived child process that reads and writes one JSON object per
// line: the autocomplete session and the agent. It lives on the orchestrator
// thread with its owner. Standard error goes to the Processes log of `key`;
// standard output is the protocol. See docs/processes.md.
class JsonLineProcess : public QObject
{
    Q_OBJECT

public:
    explicit JsonLineProcess(const QString &key, QObject *parent = nullptr);
    ~JsonLineProcess() override;

    void setOrchestrator(ProcessOrchestrator *orchestrator) { m_orchestrator = orchestrator; }
    // Replaces a process that is still running. Called from the Hold work of
    // `key`, so the record's token is the current one. A working directory
    // inside a WSL distro runs the command there, and the path variables are
    // given to it as Linux paths.
    void start(const QString &command, const QString &workingDirectory, const CommandVariables &variables);
    // Ends the process tree and drops the record. Does not wait.
    void stop();
    // Dropped once the process has been stopped.
    void write(const QJsonObject &message);
    bool running() const;
    bool starting() const;
    // A process is starting or running for this command in this directory.
    bool runs(const QString &command, const QString &workingDirectory) const;
    // Empty unless the process runs inside a WSL distro.
    const QString &distro() const { return m_launch.distro(); }

signals:
    void started();
    void received(const QJsonObject &message);
    // A line on standard output that is not a JSON object.
    void unreadable(const QByteArray &line);
    // The process ended without stop().
    void exited();
    void failedToStart(const QString &error);

private:
    void absorbStdout(const QByteArray &bytes);
    void takeLine(const QByteArray &line);
    void stopTree();

    QString m_key;
    ProcessOrchestrator *m_orchestrator = nullptr;
    QProcess *m_process = nullptr;
    ProcessLaunch m_launch;
    QString m_command;
    QString m_workingDirectory;
    QByteArray m_pending;
    int m_token = 0;
    bool m_stopRequested = false;
};
