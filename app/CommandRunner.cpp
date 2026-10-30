#include "app/CommandRunner.h"

#include "io/WslPath.h"

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

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

struct CommandRunner::Running
{
    QProcess *process = nullptr;
    void *job = nullptr;
    bool stopRequested = false;
    bool restart = false;
    // Set while start() is inside a job launch that may be retried, so the
    // failure signal from that attempt is not shown.
    bool suppressFailure = false;
    // Set when the command runs inside a distro. linuxGroup is filled from
    // the CRLF_PGID line bash prints before the command.
    QString distro;
    qint64 linuxGroup = 0;
    bool groupResolved = false;
    QByteArray pendingOutput;
};

namespace {

#ifdef Q_OS_WIN

void *createKillJob()
{
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
    return job;
}

// Puts the new process in the job at creation, so every program it launches
// (cmd -> pnpm -> node) dies with the job instead of only the shell.
// `job` must stay alive through CreateProcess, which runs after the modifier
// returns. Children created by the new process inherit the job.
void assignJobOnCreate(HANDLE *job, QProcess::CreateProcessArguments *args, STARTUPINFOEXW *extended,
                       LPPROC_THREAD_ATTRIBUTE_LIST *attributes)
{
    auto *source = reinterpret_cast<STARTUPINFOW *>(args->startupInfo);
    extended->StartupInfo = *source;
    extended->StartupInfo.cb = sizeof(STARTUPINFOEXW);

    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    *attributes = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, size));
    InitializeProcThreadAttributeList(*attributes, 1, 0, &size);
    UpdateProcThreadAttribute(*attributes, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, job, sizeof(HANDLE),
                              nullptr, nullptr);
    extended->lpAttributeList = *attributes;

    args->startupInfo = reinterpret_cast<Q_STARTUPINFO *>(extended);
    args->flags |= EXTENDED_STARTUPINFO_PRESENT;
}

void freeAttributes(LPPROC_THREAD_ATTRIBUTE_LIST attributes)
{
    if (!attributes)
        return;
    DeleteProcThreadAttributeList(attributes);
    HeapFree(GetProcessHeap(), 0, attributes);
}

#endif

} // namespace

CommandRunner::CommandRunner(QObject *parent)
    : QObject(parent)
{
}

CommandRunner::~CommandRunner()
{
    stopAll();
    qDeleteAll(m_running);
}

void CommandRunner::setContextProvider(std::function<CommandVariables()> provider)
{
    m_contextProvider = std::move(provider);
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
    if (!command(name))
        return;
    Running *running = m_running.value(name);
    if (running && running->process->state() != QProcess::NotRunning) {
        running->restart = true;
        stop(name);
        return;
    }
    launch(name, true);
}

void CommandRunner::launch(const QString &name, bool withJob)
{
    const ProjectCommand *spec = command(name);
    Running *running = m_running.value(name);
    if (!running) {
        running = new Running;
        running->process = new QProcess(this);
        m_running.insert(name, running);

        connect(running->process, &QProcess::readyReadStandardOutput, this, [this, name] {
            Running *running = m_running.value(name);
            const QByteArray bytes = running->process->readAllStandardOutput();
            if (running->distro.isEmpty()) {
                emit outputReceived(name, QString::fromLocal8Bit(bytes));
                return;
            }
            if (running->groupResolved) {
                emit outputReceived(name, QString::fromUtf8(bytes));
                return;
            }
            running->pendingOutput += bytes;
            const int newline = running->pendingOutput.indexOf('\n');
            if (newline < 0)
                return;
            const QByteArray first = running->pendingOutput.left(newline);
            QByteArray rest = running->pendingOutput.mid(newline + 1);
            running->pendingOutput.clear();
            running->groupResolved = true;
            const QString line = QString::fromUtf8(first).trimmed();
            const QString marker = QStringLiteral("CRLF_PGID:");
            if (line.startsWith(marker))
                running->linuxGroup = line.mid(marker.size()).toLongLong();
            else
                rest.prepend(first + '\n');
            if (!rest.isEmpty())
                emit outputReceived(name, QString::fromUtf8(rest));
        });
        connect(running->process, &QProcess::finished, this,
                [this, name](int exitCode, QProcess::ExitStatus) {
                    Running *done = m_running.value(name);
                    const bool restart = done->restart;
                    const bool stopped = done->stopRequested;
                    done->restart = false;
                    done->stopRequested = false;
                    emit stateChanged(name, false, stopped ? -1 : exitCode);
                    if (restart)
                        launch(name);
                });
        connect(running->process, &QProcess::errorOccurred, this,
                [this, name](QProcess::ProcessError error) {
                    if (error != QProcess::FailedToStart)
                        return;
                    Running *failed = m_running.value(name);
                    if (failed->suppressFailure)
                        return;
                    failed->restart = false;
                    emit outputReceived(name, failed->process->errorString() + QLatin1Char('\n'));
                    emit stateChanged(name, false, -1);
                });
    }

    running->stopRequested = false;
    running->restart = false;
    running->distro.clear();
    running->linuxGroup = 0;
    running->groupResolved = false;
    running->pendingOutput.clear();

    CommandVariables vars = m_contextProvider ? m_contextProvider() : CommandVariables{};
    vars.cwd = spec->workingDirectory;
#ifdef Q_OS_WIN
    const WslPath::LinuxPath linux = WslPath::toLinux(spec->workingDirectory);
    if (linux.valid()) {
        vars.file = linuxPath(vars.file);
        vars.fileDir = linuxPath(vars.fileDir);
        vars.project = linuxPath(vars.project);
        vars.folder = linuxPath(vars.folder);
        vars.cwd = linux.path;
    }
#endif
    const QString commandText = expandCommand(spec->command, vars);

#ifdef Q_OS_WIN
    if (running->job) {
        CloseHandle(static_cast<HANDLE>(running->job));
        running->job = nullptr;
    }
    STARTUPINFOEXW extended = {};
    LPPROC_THREAD_ATTRIBUTE_LIST attributes = nullptr;
    HANDLE jobHandle = nullptr;
    if (withJob) {
        running->job = createKillJob();
        jobHandle = static_cast<HANDLE>(running->job);
        withJob = jobHandle != nullptr;
    }
    if (withJob) {
        running->process->setCreateProcessArgumentsModifier(
            [&extended, &attributes, &jobHandle](QProcess::CreateProcessArguments *args) {
                assignJobOnCreate(&jobHandle, args, &extended, &attributes);
            });
    } else {
        running->process->setCreateProcessArgumentsModifier({});
    }
    if (linux.valid()) {
        running->distro = linux.distro;
        const QString script = QStringLiteral("printf 'CRLF_PGID:%s\\n' \"$$\"; ") + commandText;
        running->process->setProgram(QStringLiteral("C:/Windows/System32/wsl.exe"));
        running->process->setNativeArguments({});
        running->process->setArguments({QStringLiteral("-d"), linux.distro, QStringLiteral("--cd"),
                                        linux.path, QStringLiteral("--"), QStringLiteral("bash"),
                                        QStringLiteral("-lc"), script});
        // cmd.exe cannot use a UNC path as its current directory, and wsl.exe
        // takes the Linux path through --cd instead.
        running->process->setWorkingDirectory(QString());
    } else {
        running->process->setProgram(qEnvironmentVariable("ComSpec", QStringLiteral("C:\\Windows\\System32\\cmd.exe")));
        running->process->setArguments({});
        running->process->setNativeArguments(QStringLiteral("/d /s /c ") + commandText);
        running->process->setWorkingDirectory(spec->workingDirectory);
    }
#else
    running->process->setProgram(QStringLiteral("/bin/sh"));
    running->process->setArguments({QStringLiteral("-c"), commandText});
    running->process->setWorkingDirectory(spec->workingDirectory);
#endif
    running->process->setProcessChannelMode(QProcess::MergedChannels);
    running->suppressFailure = withJob;
    running->process->start();
    running->suppressFailure = false;
#ifdef Q_OS_WIN
    running->process->setCreateProcessArgumentsModifier({});
    freeAttributes(attributes);
    // Some parent jobs reject a nested job. Run the command anyway; stop()
    // still kills the tree with taskkill /T.
    if (withJob && running->process->error() == QProcess::FailedToStart) {
        launch(name, false);
        return;
    }
#endif
    emit stateChanged(name, running->process->state() != QProcess::NotRunning, 0);
}

void CommandRunner::stopTree(Running &running)
{
    running.stopRequested = true;
#ifdef Q_OS_WIN
    if (!running.distro.isEmpty() && running.linuxGroup > 0) {
        QProcess killer;
        killer.setProgram(QStringLiteral("C:/Windows/System32/wsl.exe"));
        killer.setArguments({QStringLiteral("-d"), running.distro, QStringLiteral("--"),
                             QStringLiteral("kill"), QStringLiteral("--"),
                             QString::number(-running.linuxGroup)});
        killer.start();
        killer.waitForFinished(3000);
    }
    const qint64 pid = running.process->processId();
    if (pid > 0) {
        QProcess killer;
        killer.setProgram(QStringLiteral("taskkill"));
        killer.setArguments({QStringLiteral("/F"), QStringLiteral("/T"), QStringLiteral("/PID"),
                             QString::number(pid)});
        killer.start();
        killer.waitForFinished(3000);
    }
    if (running.job) {
        CloseHandle(static_cast<HANDLE>(running.job));
        running.job = nullptr;
    }
#else
    running.process->terminate();
#endif
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
        running->process->waitForFinished(2000);
    }
}
