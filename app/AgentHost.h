#pragma once

#include "agent/AgentPolicy.h"
#include "app/AgentSession.h"

#include <QHash>
#include <QObject>

#include <functional>

class AgentPanel;
class ProcessOrchestrator;
class SourceControl;
class TabWorkspace;
class TerminalPanel;

// Answers the agent's requests on the UI thread with the editor's own
// services: open buffers, files, git status, problems, and the terminal.
// Every request passes AgentPolicy first; a question goes to the Agent panel.
// Each method is one handler in a table, so adding one leaves the rest
// alone. See docs/agent.md.
class AgentHost : public QObject
{
    Q_OBJECT

public:
    AgentHost(AgentSession *session, AgentPanel *panel, TabWorkspace *tabs, SourceControl *sourceControl,
              TerminalPanel *terminal, ProcessOrchestrator *orchestrator, QObject *parent = nullptr);

    // A new session: its working directory and the project's permissions.
    void begin(const QString &workingDirectory, const AgentPermissions &permissions);
    void handle(const AgentRequest &request);

private:
    using Handler = void (AgentHost::*)(const AgentRequest &);

    void readFile(const AgentRequest &request);
    void writeFile(const AgentRequest &request);
    // A write as far as the policy goes. An open tab keeps its text, as it
    // does when the explorer deletes its file.
    void deleteFile(const AgentRequest &request);
    void requestPermission(const AgentRequest &request);
    void createTerminal(const AgentRequest &request);
    void editorState(const AgentRequest &request);
    void problems(const AgentRequest &request);
    void gitStatus(const AgentRequest &request);
    void readTerminal(const AgentRequest &request);
    void sendTerminal(const AgentRequest &request);

    // Runs `allowed` when the policy, or the user on a card, permits it.
    void authorize(const AgentRequest &request, AgentAction action, const QString &path, const QString &title,
                   const QString &detail, std::function<void()> allowed);
    // The open buffer, or the file read on the pool. `exists` is false for
    // a file that is neither open nor on disk.
    void currentText(const QString &path, std::function<void(bool exists, const QString &text)> done);
    void reviewWrite(const AgentRequest &request, const QString &path, const QString &text);
    void applyWrite(const AgentRequest &request, const QString &path, const QString &text);
    void reply(const QJsonValue &id, const QJsonValue &result);
    void reject(const QJsonValue &id, int code, const QString &message);
    QString shown(const QString &path) const;

    AgentSession *m_session;
    AgentPanel *m_panel;
    TabWorkspace *m_tabs;
    SourceControl *m_sourceControl;
    TerminalPanel *m_terminal;
    ProcessOrchestrator *m_orchestrator;
    QHash<QString, Handler> m_handlers;
    QString m_root;
    AgentPolicy m_policy;
    int m_reads = 0;
};
