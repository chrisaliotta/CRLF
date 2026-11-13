#include "app/AgentSession.h"

#include "app/AgentTerminals.h"
#include "app/JsonLineProcess.h"
#include "app/ProcessOrchestrator.h"
#include "io/WslPath.h"

#include <QCoreApplication>

namespace {

const QString kKey = QStringLiteral("agent");
constexpr int kProtocolVersion = 1;
// ACP's error for a session/new that needs authenticate first.
constexpr int kAuthRequired = -32000;

QString errorText(const RpcMessage &response)
{
    return response.error.value(QLatin1String("message")).toString();
}

} // namespace

AgentSession::AgentSession(QObject *parent)
    : QObject(parent)
    , m_process(new JsonLineProcess(kKey, this))
    , m_terminals(new AgentTerminals(this))
{
    qRegisterMetaType<AgentUpdate>();
    qRegisterMetaType<AgentRequest>();
    qRegisterMetaType<QList<AgentConfigOption>>();
    connect(m_process, &JsonLineProcess::started, this, [this] {
        m_orchestrator->setActivity(kKey, QStringLiteral("starting"));
        QJsonObject capabilities{
            {QStringLiteral("fs"),
             QJsonObject{{QStringLiteral("readTextFile"), true}, {QStringLiteral("writeTextFile"), true}}},
            {QStringLiteral("terminal"), true},
            {QStringLiteral("session"),
             QJsonObject{{QStringLiteral("configOptions"),
                          QJsonObject{{QStringLiteral("boolean"), QJsonObject()}}}}}};
        QJsonObject info{{QStringLiteral("name"), QCoreApplication::applicationName()},
                         {QStringLiteral("version"), QCoreApplication::applicationVersion()}};
        call(QStringLiteral("initialize"),
             {{QStringLiteral("protocolVersion"), kProtocolVersion},
              {QStringLiteral("clientCapabilities"), capabilities},
              {QStringLiteral("clientInfo"), info}},
             [this](const RpcMessage &response) { initialized(response); });
    });
    connect(m_process, &JsonLineProcess::received, this, &AgentSession::take);
    connect(m_process, &JsonLineProcess::unreadable, this, [this](const QByteArray &line) {
        m_orchestrator->appendLog(kKey, QStringLiteral("Not JSON-RPC: %1\n").arg(QString::fromUtf8(line)));
    });
    connect(m_process, &JsonLineProcess::exited, this, [this] {
        reset();
        emit stopped();
    });
    connect(m_process, &JsonLineProcess::failedToStart, this, &AgentSession::failed);
    connect(m_process, &JsonLineProcess::blocked, this, &AgentSession::stopped);
    connect(m_terminals, &AgentTerminals::outputReceived, this, &AgentSession::terminalOutput);
    connect(m_terminals, &AgentTerminals::stateChanged, this, &AgentSession::terminalState);
}

void AgentSession::setOrchestrator(ProcessOrchestrator *orchestrator)
{
    m_orchestrator = orchestrator;
    m_process->setOrchestrator(orchestrator);
    m_terminals->setOrchestrator(orchestrator);
}

void AgentSession::setLaunchAllowed(std::function<bool(const QString &id)> allowed)
{
    m_process->setLaunchAllowed(std::move(allowed));
}

void AgentSession::start(const AgentLaunch &launch)
{
    reset();
    m_name = launch.name;
    m_workingDirectory = launch.workingDirectory;
    m_process->start(launch.command, launch.workingDirectory, launch.variables,
                     QStringLiteral("agent:") + launch.name);
}

void AgentSession::prompt(const AgentPrompt &prompt)
{
    QJsonArray blocks{textBlock(prompt.text)};
    for (const QString &file : prompt.files)
        blocks.append(resourceLinkBlock(agentPath(m_workingDirectory, file)));
    if (!prompt.selection.isEmpty()) {
        const QString path = agentPath(m_workingDirectory, prompt.selectionPath);
        if (m_embeddedContext)
            blocks.append(resourceBlock(path, prompt.selection));
        else
            blocks.append(textBlock(QStringLiteral("Selected in %1:\n```\n%2\n```").arg(path, prompt.selection)));
    }
    m_orchestrator->setActivity(kKey, QStringLiteral("working"));
    call(QStringLiteral("session/prompt"), {{QStringLiteral("sessionId"), m_sessionId}, {QStringLiteral("prompt"), blocks}},
         [this](const RpcMessage &response) {
             m_orchestrator->setActivity(kKey, QStringLiteral("ready"));
             if (!response.error.isEmpty()) {
                 emit failed(errorText(response));
                 emit turnEnded(QStringLiteral("error"));
                 return;
             }
             emit turnEnded(response.result.toObject().value(QLatin1String("stopReason")).toString());
         });
}

void AgentSession::cancel()
{
    m_process->write(rpcNotification(QStringLiteral("session/cancel"), {{QStringLiteral("sessionId"), m_sessionId}}));
}

void AgentSession::stop()
{
    reset();
    m_process->stop();
}

void AgentSession::reply(const QJsonValue &id, const QJsonValue &result)
{
    m_process->write(rpcResult(id, result));
}

void AgentSession::replyError(const QJsonValue &id, int code, const QString &message)
{
    m_process->write(rpcError(id, code, message));
}

void AgentSession::setConfigOption(const QString &id, const QVariant &value)
{
    QJsonObject params{{QStringLiteral("sessionId"), m_sessionId},
                       {QStringLiteral("configId"), id},
                       {QStringLiteral("value"), QJsonValue::fromVariant(value)}};
    if (value.typeId() == QMetaType::Bool)
        params.insert(QLatin1String("type"), QStringLiteral("boolean"));
    call(QStringLiteral("session/set_config_option"), params, [this](const RpcMessage &response) {
        if (!response.error.isEmpty()) {
            emit failed(errorText(response));
            emit configOptionsChanged(m_configOptions);
            return;
        }
        takeConfigOptions(readConfigOptions(response.result.toObject().value(QLatin1String("configOptions")).toArray()));
    });
}

void AgentSession::takeConfigOptions(const QList<AgentConfigOption> &options)
{
    m_configOptions = options;
    emit configOptionsChanged(options);
}

void AgentSession::createTerminal(const AgentRequest &request)
{
    const QString id = m_terminals->create(request.params, m_workingDirectory);
    reply(request.id, QJsonObject{{QStringLiteral("terminalId"), id}});
}

void AgentSession::killTerminal(const QString &terminalId)
{
    m_terminals->kill(terminalId);
}

void AgentSession::call(const QString &method, const QJsonObject &params, Handler handler)
{
    const int id = ++m_nextId;
    m_pending.insert(id, std::move(handler));
    m_process->write(rpcRequest(id, method, params));
}

void AgentSession::take(const QJsonObject &message)
{
    const RpcMessage rpc = readRpc(message);
    if (rpc.kind == RpcMessage::Kind::Response) {
        const Handler handler = m_pending.take(rpc.id.toInt());
        if (handler)
            handler(rpc);
        return;
    }
    if (rpc.kind == RpcMessage::Kind::Request) {
        takeRequest(rpc);
        return;
    }
    const QJsonObject body = rpc.params.value(QLatin1String("update")).toObject();
    if (rpc.method == QLatin1String("_crlf/usage")) {
        AgentUpdate usage;
        usage.kind = AgentUpdateKind::Usage;
        usage.usage = readUsage(body);
        emit updated(usage);
        return;
    }
    if (rpc.method != QLatin1String("session/update"))
        return;
    AgentUpdate update = readUpdate(body);
    if (update.kind == AgentUpdateKind::ConfigOptions) {
        takeConfigOptions(update.configOptions);
        return;
    }
    for (AgentToolLocation &location : update.locations)
        location.path = editorPath(m_workingDirectory, location.path);
    emit updated(update);
}

void AgentSession::takeRequest(const RpcMessage &request)
{
    if (request.method.startsWith(QLatin1String("terminal/")) && request.method != QLatin1String("terminal/create")) {
        const QString terminalId = request.params.value(QLatin1String("terminalId")).toString();
        if (!m_terminals->contains(terminalId)) {
            replyError(request.id, RpcCode::InvalidParams, QStringLiteral("No terminal %1").arg(terminalId));
            return;
        }
        if (request.method == QLatin1String("terminal/output")) {
            reply(request.id, m_terminals->output(terminalId));
        } else if (request.method == QLatin1String("terminal/wait_for_exit")) {
            const QJsonValue id = request.id;
            m_terminals->waitForExit(terminalId, [this, id](const QJsonObject &status) { reply(id, status); });
        } else if (request.method == QLatin1String("terminal/kill")) {
            m_terminals->kill(terminalId);
            reply(request.id, QJsonObject());
        } else if (request.method == QLatin1String("terminal/release")) {
            m_terminals->release(terminalId);
            reply(request.id, QJsonObject());
        } else {
            replyError(request.id, RpcCode::MethodNotFound, request.method);
        }
        return;
    }
    QJsonObject params = request.params;
    for (const char *key : {"path", "cwd"}) {
        const QString path = params.value(QLatin1String(key)).toString();
        if (!path.isEmpty())
            params.insert(QLatin1String(key), editorPath(m_workingDirectory, path));
    }
    emit requested({request.id, request.method, params});
}

void AgentSession::initialized(const RpcMessage &response)
{
    if (!response.error.isEmpty()) {
        emit failed(QStringLiteral("%1 did not initialize: %2").arg(m_name, errorText(response)));
        return;
    }
    const QJsonObject result = response.result.toObject();
    const QJsonObject info = result.value(QLatin1String("agentInfo")).toObject();
    m_agentName = info.value(QLatin1String("title")).toString(info.value(QLatin1String("name")).toString(m_name));
    m_embeddedContext = result.value(QLatin1String("agentCapabilities"))
                            .toObject()
                            .value(QLatin1String("promptCapabilities"))
                            .toObject()
                            .value(QLatin1String("embeddedContext"))
                            .toBool();
    call(QStringLiteral("session/new"),
         {{QStringLiteral("cwd"), agentPath(m_workingDirectory, m_workingDirectory)}, {QStringLiteral("mcpServers"), QJsonArray()}},
         [this](const RpcMessage &created) { sessionCreated(created); });
}

void AgentSession::sessionCreated(const RpcMessage &response)
{
    if (!response.error.isEmpty()) {
        const int code = response.error.value(QLatin1String("code")).toInt();
        emit failed(code == kAuthRequired
                        ? QStringLiteral("%1 needs you to sign in. Run its login command in a terminal, then start "
                                         "the agent again.")
                              .arg(m_agentName)
                        : QStringLiteral("%1 did not start a session: %2").arg(m_agentName, errorText(response)));
        return;
    }
    const QJsonObject result = response.result.toObject();
    m_sessionId = result.value(QLatin1String("sessionId")).toString();
    takeConfigOptions(readConfigOptions(result.value(QLatin1String("configOptions")).toArray()));
    m_orchestrator->setActivity(kKey, QStringLiteral("ready"));
    emit ready(m_agentName);
}

void AgentSession::reset()
{
    m_terminals->releaseAll();
    m_pending.clear();
    m_sessionId.clear();
    m_configOptions.clear();
}

QString agentPath(const QString &workingDirectory, const QString &path)
{
    if (!WslPath::toLinux(workingDirectory).valid())
        return path;
    const WslPath::LinuxPath linux = WslPath::toLinux(path);
    return linux.valid() ? linux.path : path;
}

QString editorPath(const QString &workingDirectory, const QString &path)
{
    const WslPath::LinuxPath root = WslPath::toLinux(workingDirectory);
    if (!root.valid() || !path.startsWith(QLatin1Char('/')))
        return path;
    return WslPath::toWindows(root.distro, path);
}
