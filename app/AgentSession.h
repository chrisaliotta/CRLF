#pragma once

#include "agent/AcpTypes.h"
#include "agent/JsonRpc.h"
#include "io/CommandVariables.h"

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QStringList>

#include <functional>

class AgentTerminals;
class JsonLineProcess;
class ProcessOrchestrator;

// What starts one agent. `workingDirectory` is the session's cwd.
struct AgentLaunch
{
    QString name;
    QString command;
    QString workingDirectory;
    CommandVariables variables;
};

// One turn's input. `files` are named to the agent as links; a non-empty
// `selection` is sent as the text of `selectionPath`.
struct AgentPrompt
{
    QString text;
    QStringList files;
    QString selectionPath;
    QString selection;
};

// A request from the agent that the UI thread answers. `params` carries
// editor paths: a distro path is already the \\wsl.localhost form.
struct AgentRequest
{
    QJsonValue id;
    QString method;
    QJsonObject params;
};

Q_DECLARE_METATYPE(AgentRequest)

// An agent started in a WSL distro sees Linux paths. These convert between
// that path and the editor's for an agent whose cwd is `workingDirectory`.
QString agentPath(const QString &workingDirectory, const QString &path);
QString editorPath(const QString &workingDirectory, const QString &path);

// The Agent Client Protocol session with one agent process. The agent is
// the child; this is the client. It lives on the orchestrator thread; the UI
// queues start, prompt, cancel, stop, and the replies. Terminal requests
// other than terminal/create are answered here. See docs/agent.md.
class AgentSession : public QObject
{
    Q_OBJECT

public:
    explicit AgentSession(QObject *parent = nullptr);

    void setOrchestrator(ProcessOrchestrator *orchestrator);
    // Replaces a running agent.
    void start(const AgentLaunch &launch);
    void prompt(const AgentPrompt &prompt);
    // session/cancel. The turn still ends with its own reply.
    void cancel();
    void stop();
    void reply(const QJsonValue &id, const QJsonValue &result);
    void replyError(const QJsonValue &id, int code, const QString &message);
    // session/set_config_option. `value` is a choice's value, or a bool.
    // The agent's reply is the whole list, sent on as configOptionsChanged.
    void setConfigOption(const QString &id, const QVariant &value);
    // Answers an approved terminal/create.
    void createTerminal(const AgentRequest &request);
    // From the Processes page.
    void killTerminal(const QString &terminalId);

signals:
    // session/new answered. The agent is ready for a prompt.
    void ready(const QString &agentName);
    // The settings the agent offers and their values: before ready for a
    // new session, after each setConfigOption, and when the agent changes
    // them itself. A refused change sends the list as it still stands.
    void configOptionsChanged(const QList<AgentConfigOption> &options);
    void updated(const AgentUpdate &update);
    void requested(const AgentRequest &request);
    // `stopReason` is the agent's, or "error" when the turn failed.
    void turnEnded(const QString &stopReason);
    void failed(const QString &message);
    // The process exited without stop().
    void stopped();
    void terminalOutput(const QString &log, const QString &text);
    void terminalState(const QString &log, bool running, int exitCode);

private:
    using Handler = std::function<void(const RpcMessage &)>;

    void call(const QString &method, const QJsonObject &params, Handler handler);
    void take(const QJsonObject &message);
    void takeRequest(const RpcMessage &request);
    void initialized(const RpcMessage &response);
    void sessionCreated(const RpcMessage &response);
    void reset();
    void takeConfigOptions(const QList<AgentConfigOption> &options);

    JsonLineProcess *m_process = nullptr;
    AgentTerminals *m_terminals = nullptr;
    ProcessOrchestrator *m_orchestrator = nullptr;
    QString m_name;
    QString m_workingDirectory;
    QString m_sessionId;
    QString m_agentName;
    // promptCapabilities.embeddedContext from initialize.
    bool m_embeddedContext = false;
    QList<AgentConfigOption> m_configOptions;
    int m_nextId = 0;
    QHash<int, Handler> m_pending;
};
