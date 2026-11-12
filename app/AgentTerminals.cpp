#include "app/AgentTerminals.h"

#include "app/ProcessLaunch.h"
#include "app/ProcessOrchestrator.h"
#include "io/WslPath.h"

#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>

namespace {

// One argument for bash -lc inside a distro, or for cmd.exe on Windows.
QString quoted(const QString &argument, bool posix)
{
    static const QRegularExpression plain(QStringLiteral("^[A-Za-z0-9_./:=@%+,-]+$"));
    if (plain.match(argument).hasMatch())
        return argument;
    if (posix) {
        QString escaped = argument;
        escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
        return QLatin1Char('\'') + escaped + QLatin1Char('\'');
    }
    QString escaped = argument;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QString shellLine(const QString &command, const QJsonArray &args, bool posix)
{
    QStringList parts{command};
    for (const QJsonValue &arg : args)
        parts.append(quoted(arg.toString(), posix));
    return parts.join(QLatin1Char(' '));
}

QString logName(const QString &line)
{
    constexpr int kWidth = 48;
    const QString single = line.simplified();
    return QStringLiteral("agent: ") + (single.size() > kWidth ? single.left(kWidth - 1) + QChar(0x2026) : single);
}

} // namespace

struct AgentTerminals::Terminal
{
    QString key;
    QString log;
    QString line;
    QString directory;
    QList<QPair<QString, QString>> environment;
    qsizetype limit = 0;
    QProcess *process = nullptr;
    ProcessLaunch launch;
    QString output;
    bool truncated = false;
    bool exited = false;
    bool killed = false;
    int exitCode = 0;
    int token = 0;
    QList<std::function<void(const QJsonObject &)>> waiters;
};

AgentTerminals::AgentTerminals(QObject *parent)
    : QObject(parent)
{
}

AgentTerminals::~AgentTerminals()
{
    releaseAll();
}

QString AgentTerminals::create(const QJsonObject &params, const QString &defaultDirectory)
{
    const QString id = QStringLiteral("t%1").arg(++m_next);
    auto *terminal = new Terminal;
    terminal->key = QStringLiteral("agent-terminal:") + id;
    const QString cwd = params.value(QLatin1String("cwd")).toString();
    terminal->directory = cwd.isEmpty() ? defaultDirectory : cwd;
    terminal->line = shellLine(params.value(QLatin1String("command")).toString(),
                               params.value(QLatin1String("args")).toArray(),
                               WslPath::toLinux(terminal->directory).valid());
    terminal->log = logName(terminal->line);
    terminal->limit = params.value(QLatin1String("outputByteLimit")).toInteger(0);
    for (const QJsonValue &value : params.value(QLatin1String("env")).toArray()) {
        const QJsonObject variable = value.toObject();
        terminal->environment.append({variable.value(QLatin1String("name")).toString(),
                                      variable.value(QLatin1String("value")).toString()});
    }
    m_terminals.insert(id, terminal);
    m_orchestrator->submit(terminal->key, QStringLiteral("Agent command"), terminal->directory, 0, terminal->line,
                           [this, id] { launch(id); }, ProcessLane::Hold);
    return id;
}

void AgentTerminals::launch(const QString &terminalId)
{
    // The agent can release a terminal before its queued start runs.
    Terminal *terminal = m_terminals.value(terminalId);
    if (terminal == nullptr)
        return;
    terminal->token = m_orchestrator->currentToken(terminal->key);
    terminal->process = new QProcess(this);
    terminal->process->setProcessChannelMode(QProcess::MergedChannels);
    if (!terminal->environment.isEmpty()) {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        for (const auto &variable : std::as_const(terminal->environment))
            environment.insert(variable.first, variable.second);
        terminal->process->setProcessEnvironment(environment);
    }

    const auto finish = [this, terminal](int exitCode) {
        terminal->exited = true;
        terminal->exitCode = exitCode;
        emit stateChanged(terminal->log, false, terminal->killed ? -1 : exitCode);
        m_orchestrator->release(terminal->key, terminal->token);
        const QJsonObject status = exitStatus(terminal);
        const auto waiters = terminal->waiters;
        terminal->waiters.clear();
        for (const auto &done : waiters)
            done(status);
    };
    connect(terminal->process, &QProcess::readyReadStandardOutput, this,
            [this, terminal] { absorb(terminal, terminal->process->readAllStandardOutput()); });
    connect(terminal->process, &QProcess::finished, this, [this, terminal, finish](int exitCode, QProcess::ExitStatus) {
        absorb(terminal, terminal->process->readAllStandardOutput() + terminal->launch.flushOutput());
        finish(exitCode);
    });
    connect(terminal->process, &QProcess::errorOccurred, this, [this, terminal, finish](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || terminal->launch.retrying())
            return;
        const QString message = terminal->process->errorString() + QLatin1Char('\n');
        terminal->output += message;
        emit outputReceived(terminal->log, message);
        finish(-1);
    });

    emit stateChanged(terminal->log, true, 0);
    emit outputReceived(terminal->log, QStringLiteral("$ %1\n").arg(terminal->line));
    terminal->launch.startShell(terminal->process, terminal->directory, terminal->line);
}

void AgentTerminals::absorb(Terminal *terminal, const QByteArray &bytes)
{
    const QByteArray payload = terminal->launch.takeOutput(bytes);
    if (payload.isEmpty())
        return;
    QString text = terminal->launch.distro().isEmpty() ? QString::fromLocal8Bit(payload) : QString::fromUtf8(payload);
    text.remove(QLatin1Char('\r'));
    terminal->output += text;
    // The limit is in bytes; the kept tail is measured in characters, which
    // is never more bytes than that.
    if (terminal->limit > 0 && terminal->output.size() > terminal->limit) {
        terminal->output = terminal->output.right(terminal->limit);
        terminal->truncated = true;
    }
    emit outputReceived(terminal->log, text);
}

QJsonObject AgentTerminals::exitStatus(const Terminal *terminal)
{
    QJsonObject status;
    status.insert(QLatin1String("exitCode"), terminal->killed ? QJsonValue() : QJsonValue(terminal->exitCode));
    status.insert(QLatin1String("signal"), terminal->killed ? QJsonValue(QStringLiteral("SIGKILL")) : QJsonValue());
    return status;
}

QJsonObject AgentTerminals::output(const QString &terminalId) const
{
    const Terminal *terminal = m_terminals.value(terminalId);
    QJsonObject result{{QStringLiteral("output"), terminal->output}, {QStringLiteral("truncated"), terminal->truncated}};
    if (terminal->exited)
        result.insert(QLatin1String("exitStatus"), exitStatus(terminal));
    return result;
}

void AgentTerminals::waitForExit(const QString &terminalId, std::function<void(const QJsonObject &)> done)
{
    Terminal *terminal = m_terminals.value(terminalId);
    if (terminal->exited) {
        done(exitStatus(terminal));
        return;
    }
    terminal->waiters.append(std::move(done));
}

void AgentTerminals::kill(const QString &terminalId)
{
    Terminal *terminal = m_terminals.value(terminalId);
    if (terminal == nullptr || terminal->exited || terminal->process == nullptr)
        return;
    terminal->killed = true;
    terminal->launch.stopTree(this);
    terminal->process->kill();
}

void AgentTerminals::release(const QString &terminalId)
{
    Terminal *terminal = m_terminals.take(terminalId);
    if (terminal == nullptr)
        return;
    if (terminal->process != nullptr) {
        QProcess *process = terminal->process;
        process->disconnect(this);
        if (process->state() != QProcess::NotRunning) {
            terminal->launch.stopTree(this);
            connect(process, &QProcess::finished, process, &QObject::deleteLater);
            process->kill();
            emit stateChanged(terminal->log, false, -1);
        } else {
            process->deleteLater();
        }
    }
    if (!terminal->exited)
        m_orchestrator->cancel(terminal->key);
    delete terminal;
}

void AgentTerminals::releaseAll()
{
    const QStringList ids = m_terminals.keys();
    for (const QString &id : ids)
        release(id);
}
