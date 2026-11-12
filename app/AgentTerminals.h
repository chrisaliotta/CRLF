#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>

#include <functional>

class ProcessOrchestrator;

// The commands an agent runs with terminal/create. Each one is a process on
// the orchestrator thread, started through ProcessLaunch like a menu
// command, keyed `agent-terminal:<id>` on the Processes page, and copied to
// an Output log. Its output is kept for terminal/output. See docs/agent.md.
class AgentTerminals : public QObject
{
    Q_OBJECT

public:
    explicit AgentTerminals(QObject *parent = nullptr);
    ~AgentTerminals() override;

    void setOrchestrator(ProcessOrchestrator *orchestrator) { m_orchestrator = orchestrator; }
    // `params` is the request's, with an editor-side `cwd`. Returns the id.
    QString create(const QJsonObject &params, const QString &defaultDirectory);
    bool contains(const QString &terminalId) const { return m_terminals.contains(terminalId); }
    // { output, truncated, exitStatus }. exitStatus is present once it exited.
    QJsonObject output(const QString &terminalId) const;
    // `done` gets { exitCode, signal } when the process exits, at once if it has.
    void waitForExit(const QString &terminalId, std::function<void(const QJsonObject &)> done);
    void kill(const QString &terminalId);
    // Kills the process if it is still running and forgets the terminal.
    void release(const QString &terminalId);
    void releaseAll();

signals:
    void outputReceived(const QString &log, const QString &text);
    // exitCode is negative when the process was killed.
    void stateChanged(const QString &log, bool running, int exitCode);

private:
    struct Terminal;

    void launch(const QString &terminalId);
    void absorb(Terminal *terminal, const QByteArray &bytes);
    static QJsonObject exitStatus(const Terminal *terminal);

    ProcessOrchestrator *m_orchestrator = nullptr;
    QHash<QString, Terminal *> m_terminals;
    int m_next = 0;
};
