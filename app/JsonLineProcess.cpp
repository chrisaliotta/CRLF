#include "app/JsonLineProcess.h"

#include "app/ProcessOrchestrator.h"
#include "io/WslPath.h"

#include <QJsonDocument>
#include <QProcess>

namespace {

QString linuxPath(const QString &windowsPath)
{
    if (windowsPath.isEmpty())
        return windowsPath;
    const WslPath::LinuxPath converted = WslPath::toLinux(windowsPath);
    return converted.valid() ? converted.path : windowsPath;
}

} // namespace

JsonLineProcess::JsonLineProcess(const QString &key, QObject *parent)
    : QObject(parent)
    , m_key(key)
{
}

JsonLineProcess::~JsonLineProcess()
{
    stopTree();
}

void JsonLineProcess::start(const QString &command, const QString &workingDirectory,
                            const CommandVariables &variables)
{
    stopTree();
    m_command = command;
    m_workingDirectory = workingDirectory;
    m_token = m_orchestrator->currentToken(m_key);
    m_stopRequested = false;

    if (m_process == nullptr) {
        m_process = new QProcess(this);
        connect(m_process, &QProcess::started, this, &JsonLineProcess::started);
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
            absorbStdout(m_process->readAllStandardOutput());
        });
        connect(m_process, &QProcess::readyReadStandardError, this, [this] {
            m_orchestrator->appendLog(m_key, QString::fromUtf8(m_process->readAllStandardError()));
        });
        connect(m_process, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
            absorbStdout(m_process->readAllStandardOutput());
            if (m_stopRequested)
                return;
            m_orchestrator->release(m_key, m_token);
            emit exited();
        });
    }
    m_pending.clear();

    CommandVariables vars = variables;
    vars.cwd = workingDirectory;
#ifdef Q_OS_WIN
    const WslPath::LinuxPath linux = WslPath::toLinux(workingDirectory);
    if (linux.valid()) {
        vars.file = linuxPath(vars.file);
        vars.fileDir = linuxPath(vars.fileDir);
        vars.project = linuxPath(vars.project);
        vars.folder = linuxPath(vars.folder);
        vars.cwd = linux.path;
    }
#endif
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_launch.startShell(m_process, workingDirectory, expandCommand(command, vars));
    if (m_process->state() == QProcess::NotRunning && m_process->error() == QProcess::FailedToStart)
        emit failedToStart(m_process->errorString());
}

void JsonLineProcess::stop()
{
    m_stopRequested = true;
    m_orchestrator->cancel(m_key);
    stopTree();
}

void JsonLineProcess::write(const QJsonObject &message)
{
    // A reply worked out on another thread can arrive after stop().
    if (m_process == nullptr)
        return;
    m_process->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}

bool JsonLineProcess::running() const
{
    return m_process != nullptr && m_process->state() == QProcess::Running;
}

bool JsonLineProcess::starting() const
{
    return m_process != nullptr && m_process->state() == QProcess::Starting;
}

bool JsonLineProcess::runs(const QString &command, const QString &workingDirectory) const
{
    return m_process != nullptr && m_process->state() != QProcess::NotRunning && m_command == command
           && m_workingDirectory == workingDirectory;
}

void JsonLineProcess::absorbStdout(const QByteArray &bytes)
{
    m_pending += m_launch.takeOutput(bytes);
    int newline = m_pending.indexOf('\n');
    while (newline >= 0) {
        takeLine(m_pending.left(newline));
        m_pending = m_pending.mid(newline + 1);
        newline = m_pending.indexOf('\n');
    }
}

void JsonLineProcess::takeLine(const QByteArray &line)
{
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty())
        return;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(trimmed, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit unreadable(trimmed);
        return;
    }
    emit received(document.object());
}

void JsonLineProcess::stopTree()
{
    m_launch.stopTree(this);
    if (m_process == nullptr || m_process->state() == QProcess::NotRunning)
        return;
    // The killed process can take a moment to exit. It is detached so a new
    // process can start now, and its exit does not read as this one's.
    QProcess *old = m_process;
    m_process = nullptr;
    old->disconnect(this);
    connect(old, &QProcess::finished, old, &QObject::deleteLater);
    old->kill();
}
