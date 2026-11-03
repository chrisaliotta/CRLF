#include "app/CheckRunner.h"

#include "io/WslPath.h"

#include <QHash>
#include <QProcess>

namespace {

QString linuxPath(const QString &windowsPath)
{
    if (windowsPath.isEmpty())
        return windowsPath;
    const WslPath::LinuxPath converted = WslPath::toLinux(windowsPath);
    return converted.valid() ? converted.path : windowsPath;
}

QString decoded(const QByteArray &bytes, bool utf8)
{
    return utf8 ? QString::fromUtf8(bytes) : QString::fromLocal8Bit(bytes);
}

} // namespace

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

struct CheckRunner::Running
{
    QProcess *process = nullptr;
    void *job = nullptr;
    bool stopRequested = false;
    bool discard = false;
    bool hasPending = false;
    CheckRequest pending;
    bool suppressFailure = false;
    QString distro;
    qint64 linuxGroup = 0;
    bool groupResolved = false;
    QByteArray pendingOutput;
    QByteArray input;
    QByteArray captured;
    QByteArray errors;
    CheckRequest request;
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

CheckRunner::CheckRunner(QObject *parent)
    : QObject(parent)
{
}

CheckRunner::~CheckRunner()
{
    cancelAll();
    for (Running *running : m_running) {
        if (running->process->state() != QProcess::NotRunning)
            running->process->waitForFinished(2000);
    }
    qDeleteAll(m_running);
}

void CheckRunner::start(const CheckRequest &request)
{
    if (request.command.isEmpty())
        return;
    Running *running = m_running.value(request.editorId);
    if (running && running->process->state() != QProcess::NotRunning) {
        running->discard = false;
        running->pending = request;
        running->hasPending = true;
        stopTree(*running);
        return;
    }
    launch(request, true);
}

void CheckRunner::cancel(quintptr editorId)
{
    Running *running = m_running.value(editorId);
    if (!running)
        return;
    running->discard = true;
    running->hasPending = false;
    if (running->process->state() != QProcess::NotRunning)
        stopTree(*running);
}

void CheckRunner::cancelAll()
{
    const auto ids = m_running.keys();
    for (quintptr id : ids)
        cancel(id);
}

void CheckRunner::launch(const CheckRequest &request, bool withJob)
{
    Running *running = m_running.value(request.editorId);
    if (!running) {
        running = new Running;
        running->process = new QProcess(this);
        m_running.insert(request.editorId, running);
        const quintptr editorId = request.editorId;

        connect(running->process, &QProcess::readyReadStandardOutput, this, [this, editorId] {
            Running *running = m_running.value(editorId);
            if (running)
                absorbStdout(*running, running->process->readAllStandardOutput());
        });
        connect(running->process, &QProcess::readyReadStandardError, this, [this, editorId] {
            Running *running = m_running.value(editorId);
            if (!running)
                return;
            running->errors += running->process->readAllStandardError();
        });
        connect(running->process, &QProcess::finished, this, [this, editorId](int exitCode, QProcess::ExitStatus status) {
            Running *done = m_running.value(editorId);
            if (!done)
                return;
            absorbStdout(*done, done->process->readAllStandardOutput());
            done->errors += done->process->readAllStandardError();
            if (!done->distro.isEmpty() && !done->groupResolved && !done->pendingOutput.isEmpty()) {
                const QByteArray first = done->pendingOutput;
                done->pendingOutput.clear();
                done->groupResolved = true;
                const QString line = QString::fromUtf8(first).trimmed();
                const QString marker = QStringLiteral("CRLF_PGID:");
                if (!line.startsWith(marker))
                    done->captured += first;
            }

            if (done->hasPending) {
                const CheckRequest next = done->pending;
                done->hasPending = false;
                done->discard = false;
                done->stopRequested = false;
                launch(next);
                return;
            }
            if (done->discard || done->stopRequested) {
                done->discard = false;
                done->stopRequested = false;
                return;
            }

            const bool utf8 = !done->distro.isEmpty();
            if (status == QProcess::CrashExit || exitCode != 0) {
                QString message = decoded(done->errors, utf8).trimmed();
                if (message.isEmpty())
                    message = QStringLiteral("Check failed.");
                emit failed(editorId, message);
                return;
            }
            QList<Diagnostic> spans;
            if (!diagnosticsFromJson(done->captured, &spans)) {
                emit failed(editorId, QStringLiteral("Couldn't read check results."));
                return;
            }
            emit completed(editorId, done->request.revision, spans);
        });
        connect(running->process, &QProcess::errorOccurred, this, [this, editorId](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart)
                return;
            Running *failedRun = m_running.value(editorId);
            if (!failedRun || failedRun->suppressFailure || failedRun->discard || failedRun->stopRequested)
                return;
            emit failed(editorId, failedRun->process->errorString());
        });
    }

    running->request = request;
    running->stopRequested = false;
    running->discard = false;
    running->distro.clear();
    running->linuxGroup = 0;
    running->groupResolved = false;
    running->pendingOutput.clear();
    running->captured.clear();
    running->errors.clear();
    running->input = request.text.toUtf8();

    CommandVariables vars = request.variables;
    vars.cwd = request.workingDirectory;
#ifdef Q_OS_WIN
    const WslPath::LinuxPath linux = WslPath::toLinux(request.workingDirectory);
    if (linux.valid()) {
        vars.file = linuxPath(vars.file);
        vars.fileDir = linuxPath(vars.fileDir);
        vars.project = linuxPath(vars.project);
        vars.folder = linuxPath(vars.folder);
        vars.cwd = linux.path;
    }
#endif
    const QString commandText = expandCommand(request.command, vars);

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
        running->process->setWorkingDirectory(QString());
    } else {
        running->process->setProgram(qEnvironmentVariable("ComSpec", QStringLiteral("C:\\Windows\\System32\\cmd.exe")));
        running->process->setArguments({});
        running->process->setNativeArguments(QStringLiteral("/d /s /c ") + commandText);
        running->process->setWorkingDirectory(request.workingDirectory);
    }
#else
    Q_UNUSED(withJob);
    running->process->setProgram(QStringLiteral("/bin/sh"));
    running->process->setArguments({QStringLiteral("-c"), commandText});
    running->process->setWorkingDirectory(request.workingDirectory);
#endif
    running->process->setProcessChannelMode(QProcess::SeparateChannels);
    running->suppressFailure = withJob;
    running->process->start();
    running->suppressFailure = false;
#ifdef Q_OS_WIN
    running->process->setCreateProcessArgumentsModifier({});
    freeAttributes(attributes);
    if (withJob && running->process->error() == QProcess::FailedToStart) {
        launch(request, false);
        return;
    }
#endif
    if (running->process->state() != QProcess::NotRunning) {
        running->process->write(running->input);
        running->process->closeWriteChannel();
    }
}

void CheckRunner::absorbStdout(Running &running, const QByteArray &bytes)
{
    QByteArray payload = bytes;
    if (!running.distro.isEmpty() && !running.groupResolved) {
        running.pendingOutput += bytes;
        const int newline = running.pendingOutput.indexOf('\n');
        if (newline < 0)
            return;
        const QByteArray first = running.pendingOutput.left(newline);
        payload = running.pendingOutput.mid(newline + 1);
        running.pendingOutput.clear();
        running.groupResolved = true;
        const QString line = QString::fromUtf8(first).trimmed();
        const QString marker = QStringLiteral("CRLF_PGID:");
        if (line.startsWith(marker))
            running.linuxGroup = line.mid(marker.size()).toLongLong();
        else
            payload.prepend(first + '\n');
    }
    if (!payload.isEmpty())
        running.captured += payload;
}

void CheckRunner::stopTree(Running &running)
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
