#include "app/CommandRunner.h"

#include "app/ProcessLaunch.h"
#include "app/ProcessOrchestrator.h"
#include "io/WslPath.h"

#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QProcess>

namespace {

QString linuxPath(const QString &windowsPath)
{
    if (windowsPath.isEmpty())
        return windowsPath;
    const WslPath::LinuxPath converted = WslPath::toLinux(windowsPath);
    if (converted.valid())
        return converted.path;
    // A payload file lives on a Windows drive. The distro sees it under /mnt.
    const QString clean = QDir::fromNativeSeparators(windowsPath);
    if (clean.size() >= 3 && clean.at(1) == QLatin1Char(':'))
        return QStringLiteral("/mnt/") + clean.at(0).toLower() + clean.mid(2);
    return windowsPath;
}

// The editor stores LF. A tool's stdout is the file bytes, decoded as UTF-8.
QString commandEditorText(const QByteArray &bytes)
{
    QString text = QString::fromUtf8(bytes);
    if (!text.isEmpty() && text.at(0) == QChar(0xFEFF))
        text.remove(0, 1);
    text.remove(QLatin1Char('\r'));
    return text;
}

} // namespace

struct CommandRunner::Running
{
    QProcess *process = nullptr;
    ProcessLaunch launch;
    bool stopRequested = false;
    bool restart = false;
    int token = 0;
    // output "editor": stdin is input, stdout accumulates in captured, and
    // stderr stays on its own channel so it is not inserted into the document.
    bool captureEditor = false;
    QByteArray input;
    QByteArray captured;
};

CommandRunner::CommandRunner(QObject *parent)
    : QObject(parent)
{
}

CommandRunner::~CommandRunner()
{
    stopAll();
    const auto payloads = m_launchVars.keys();
    for (const QString &name : payloads)
        discardPayloads(name);
    const auto processes = findChildren<QProcess *>(Qt::FindDirectChildrenOnly);
    for (QProcess *process : processes) {
        if (process->state() != QProcess::NotRunning)
            process->setParent(nullptr);
    }
    qDeleteAll(m_running);
    m_running.clear();
}

void CommandRunner::setContextProvider(std::function<CommandVariables(const QString &command)> provider)
{
    m_contextProvider = std::move(provider);
}

void CommandRunner::setEditorInputProvider(std::function<EditorFeed(const QString &name)> provider)
{
    m_editorInput = std::move(provider);
}

void CommandRunner::requestLaunch(const QString &name)
{
    const bool toEditor = sendsOutputToEditor(name);
    const ProjectCommand *spec = command(name);
    const QString commandText = spec != nullptr ? spec->command : QString();
    QMetaObject::invokeMethod(m_ui, [this, name, toEditor, commandText] {
        CommandVariables vars = m_contextProvider ? m_contextProvider(commandText) : CommandVariables{};
        EditorFeed feed;
        if (toEditor && m_editorInput)
            feed = m_editorInput(name);
        QMetaObject::invokeMethod(this, [this, name, vars, feed] {
            m_launchVars.insert(name, vars);
            m_launchFeed.insert(name, feed);
            start(name);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

bool CommandRunner::sendsOutputToEditor(const QString &name) const
{
    const ProjectCommand *spec = command(name);
    return spec && spec->outputToEditor;
}

void CommandRunner::setProject(const QList<ProjectCommand> &commands)
{
    m_commands = commands;
    const auto running = m_running.keys();
    for (const QString &name : running) {
        bool kept = false;
        for (const ProjectCommand &command : m_commands)
            kept = kept || command.name == name;
        if (!kept)
            stop(name);
    }
}

const ProjectCommand *CommandRunner::command(const QString &name) const
{
    for (const ProjectCommand &command : m_commands) {
        if (command.name == name)
            return &command;
    }
    return nullptr;
}

bool CommandRunner::isRunning(const QString &name) const
{
    const Running *running = m_running.value(name);
    return running && running->process->state() != QProcess::NotRunning;
}

void CommandRunner::start(const QString &name)
{
    if (!command(name)) {
        if (m_orchestrator != nullptr) {
            const QString key = QStringLiteral("command:%1").arg(name);
            m_orchestrator->release(key, m_orchestrator->currentToken(key));
        }
        return;
    }
    Running *running = m_running.value(name);
    if (running && running->process->state() != QProcess::NotRunning) {
        running->restart = true;
        stop(name);
        return;
    }
    launch(name);
}

void CommandRunner::launch(const QString &name)
{
    const ProjectCommand *spec = command(name);
    Running *running = m_running.value(name);
    if (!running) {
        running = new Running;
        running->process = new QProcess(this);
        m_running.insert(name, running);

        connect(running->process, &QProcess::readyReadStandardOutput, this, [this, name] {
            Running *running = m_running.value(name);
            absorbStdout(name, running->process->readAllStandardOutput());
        });
        connect(running->process, &QProcess::readyReadStandardError, this, [this, name] {
            Running *running = m_running.value(name);
            if (!running->captureEditor)
                return;
            const QByteArray bytes = running->process->readAllStandardError();
            if (bytes.isEmpty())
                return;
            emit outputReceived(name, running->launch.distro().isEmpty() ? QString::fromLocal8Bit(bytes)
                                                                         : QString::fromUtf8(bytes));
        });
        connect(running->process, &QProcess::finished, this,
                [this, name](int exitCode, QProcess::ExitStatus) {
                    Running *done = m_running.value(name);
                    if (done->captureEditor) {
                        absorbStdout(name, done->process->readAllStandardOutput());
                        const QByteArray err = done->process->readAllStandardError();
                        if (!err.isEmpty())
                            emit outputReceived(name, done->launch.distro().isEmpty() ? QString::fromLocal8Bit(err)
                                                                                     : QString::fromUtf8(err));
                        if (!done->stopRequested && !done->restart) {
                            const QString text = commandEditorText(done->captured);
                            if (exitCode != 0) {
                                if (!text.isEmpty())
                                    emit outputReceived(name, text);
                            } else if (text.isEmpty() && !done->input.isEmpty()) {
                                emit outputReceived(name, QStringLiteral("The command wrote nothing.\n"));
                            } else {
                                emit editorOutput(name, text);
                            }
                        }
                    }
                    const bool restart = done->restart;
                    const bool stopped = done->stopRequested;
                    const int token = done->token;
                    done->restart = false;
                    done->stopRequested = false;
                    emit stateChanged(name, false, stopped ? -1 : exitCode);
                    discardPayloads(name);
                    if (restart)
                        requestLaunch(name);
                    else if (m_orchestrator != nullptr)
                        m_orchestrator->release(QStringLiteral("command:%1").arg(name), token);
                });
        connect(running->process, &QProcess::errorOccurred, this,
                [this, name](QProcess::ProcessError error) {
                    if (error != QProcess::FailedToStart)
                        return;
                    Running *failed = m_running.value(name);
                    if (failed->launch.retrying())
                        return;
                    failed->restart = false;
                    emit outputReceived(name, failed->process->errorString() + QLatin1Char('\n'));
                    emit stateChanged(name, false, -1);
                    discardPayloads(name);
                    if (m_orchestrator != nullptr)
                        m_orchestrator->release(QStringLiteral("command:%1").arg(name), failed->token);
                });
    }

    running->stopRequested = false;
    running->restart = false;
    running->token = 0;
    if (m_orchestrator != nullptr) {
        const QString key = QStringLiteral("command:%1").arg(name);
        running->token = m_orchestrator->currentToken(key);
    }
    running->captureEditor = false;
    running->input.clear();
    running->captured.clear();
    if (spec->outputToEditor) {
        const EditorFeed feed = m_launchFeed.value(name);
        if (feed.active) {
            running->captureEditor = true;
            running->input = feed.text.toUtf8();
        }
    }

    CommandVariables vars = m_launchVars.value(name);
    vars.cwd = spec->workingDirectory;
#ifdef Q_OS_WIN
    const WslPath::LinuxPath linux = WslPath::toLinux(spec->workingDirectory);
    if (linux.valid()) {
        vars.file = linuxPath(vars.file);
        vars.fileDir = linuxPath(vars.fileDir);
        vars.project = linuxPath(vars.project);
        vars.folder = linuxPath(vars.folder);
        vars.buffer = linuxPath(vars.buffer);
        vars.terminal = linuxPath(vars.terminal);
        vars.cwd = linux.path;
    }
#endif
    running->process->setProcessChannelMode(running->captureEditor ? QProcess::SeparateChannels
                                                                   : QProcess::MergedChannels);
    running->launch.startShell(running->process, spec->workingDirectory, expandCommand(spec->command, vars));
    if (running->captureEditor && running->process->state() != QProcess::NotRunning) {
        running->process->write(running->input);
        running->process->closeWriteChannel();
    }
    emit stateChanged(name, running->process->state() != QProcess::NotRunning, 0);
}

void CommandRunner::discardPayloads(const QString &name)
{
    const CommandVariables vars = m_launchVars.take(name);
    if (!vars.buffer.isEmpty())
        QFile::remove(vars.buffer);
    if (!vars.terminal.isEmpty())
        QFile::remove(vars.terminal);
}

void CommandRunner::absorbStdout(const QString &name, const QByteArray &bytes)
{
    Running *running = m_running.value(name);
    if (!running)
        return;

    const QByteArray payload = running->launch.takeOutput(bytes);
    if (payload.isEmpty())
        return;
    if (running->captureEditor) {
        running->captured += payload;
        return;
    }
    emit outputReceived(name, running->launch.distro().isEmpty() ? QString::fromLocal8Bit(payload)
                                                                 : QString::fromUtf8(payload));
}

void CommandRunner::stopTree(Running &running)
{
    running.stopRequested = true;
    running.launch.stopTree(this);
    running.process->kill();
}

void CommandRunner::stop(const QString &name)
{
    Running *running = m_running.value(name);
    if (!running || running->process->state() == QProcess::NotRunning)
        return;
    stopTree(*running);
}

void CommandRunner::stopAll()
{
    for (Running *running : m_running) {
        if (running->process->state() == QProcess::NotRunning)
            continue;
        running->restart = false;
        stopTree(*running);
    }
}
