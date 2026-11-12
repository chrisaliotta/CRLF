#pragma once

#include "app/AgentSession.h"
#include "io/ProjectCommands.h"

#include <QList>
#include <QObject>
#include <QStringList>

#include <functional>

class AgentHost;
class AgentPanel;
class OutputPanel;
class ProcessOrchestrator;
class QFileSystemWatcher;
class TabWorkspace;

// Starts and stops the chosen agent and carries each turn between the Agent
// panel and the session. The session runs on the orchestrator thread; this
// object and the host stay on the UI thread. See docs/agent.md.
class AgentController : public QObject
{
    Q_OBJECT

public:
    AgentController(AgentSession *session, AgentHost *host, AgentPanel *panel, TabWorkspace *tabs,
                    OutputPanel *output, ProcessOrchestrator *orchestrator, QObject *parent = nullptr);

    // The ${...} values a starting agent's command is filled with.
    void setVariablesProvider(std::function<CommandVariables()> provider);
    // The .crlf `agents`. A running agent whose entry changed or disappeared
    // is stopped; the next prompt starts it again.
    void setAgents(const QList<AgentSettings> &agents);
    // Starts the chosen agent unless it is already running.
    void ensureStarted();
    // A new session, with an empty transcript.
    void restart();
    void stop();
    // From the Processes page.
    void killTerminal(const QString &terminalId);

private:
    const AgentSettings *current() const;
    void start();
    void send(const AgentPrompt &prompt);
    void submit(const QString &text, bool attachCurrentFile);
    void cancelTurn();
    void ended(const QString &stopReason);
    void watchCurrent();

    AgentSession *m_session;
    AgentHost *m_host;
    AgentPanel *m_panel;
    TabWorkspace *m_tabs;
    OutputPanel *m_output;
    ProcessOrchestrator *m_orchestrator;
    QFileSystemWatcher *m_watcher = nullptr;
    std::function<CommandVariables()> m_variables;
    QList<AgentSettings> m_agents;
    QString m_chosen;
    // The entry the running process was started from.
    AgentSettings m_running;
    bool m_started = false;
    bool m_ready = false;
    bool m_busy = false;
    QList<AgentPrompt> m_queued;
};
