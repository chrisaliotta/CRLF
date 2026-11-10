#include "app/SyntaxRunner.h"

#include "app/ProcessOrchestrator.h"
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

struct SyntaxRunner::Running
{
    QProcess *process = nullptr;
    void *job = nullptr;
    bool stopRequested = false;
    bool discard = false;
    bool hasPending = false;
    SyntaxRequest pending;
    bool suppressFailure = false;
    int token = 0;
    QString distro;
    qint64 linuxGroup = 0;
    bool groupResolved = false;
    QByteArray pendingOutput;
    QByteArray input;
    QByteArray captured;
    QByteArray errors;
    SyntaxRequest request;
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

// The Linux process group is ended from this thread. The call is not waited on.
void startGroupKill(QObject *parent, const QString &distro, qint64 group)
{
    if (distro.isEmpty() || group <= 0)
        return;
    auto *killer = new QProcess(parent);
    killer->setProgram(QStringLiteral("C:/Windows/System32/wsl.exe"));
    killer->setArguments({QStringLiteral("-d"), distro, QStringLiteral("--"), QStringLiteral("kill"),
                          QStringLiteral("--"), QString::number(-group)});
    QObject::connect(killer, &QProcess::finished, killer, &QObject::deleteLater);
    QObject::connect(killer, &QProcess::errorOccurred, killer, [killer](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            killer->deleteLater();
    });
    killer->start();
}

#endif

} // namespace

SyntaxRunner::SyntaxRunner(QObject *parent)
    : QObject(parent)
{
}

SyntaxRunner::~SyntaxRunner()
{
    cancelAll();
    const auto processes = findChildren<QProcess *>(Qt::FindDirectChildrenOnly);
    for (QProcess *process : processes) {
        if (process->state() != QProcess::NotRunning)
            process->setParent(nullptr);
    }
    qDeleteAll(m_running);
    m_running.clear();
}

void SyntaxRunner::start(const SyntaxRequest &request)
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

void SyntaxRunner::cancel(quintptr editorId)
{
    if (m_orchestrator != nullptr)
        m_orchestrator->cancel(QStringLiteral("syntax:%1").arg(editorId));
    Running *running = m_running.value(editorId);
    if (!running)
        return;
    running->discard = true;
    running->hasPending = false;
    if (running->process->state() != QProcess::NotRunning)
        stopTree(*running);
}

void SyntaxRunner::cancelAll()
{
    const auto ids = m_running.keys();
    for (quintptr id : ids)
        cancel(id);
}

void SyntaxRunner::launch(const SyntaxRequest &request, bool withJob)
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
            const QByteArray bytes = running->process->readAllStandardError();
            running->errors += bytes;
            if (m_orchestrator != nullptr && !bytes.isEmpty())
                m_orchestrator->appendLog(QStringLiteral("syntax:%1").arg(editorId), QString::fromUtf8(bytes));
        });
        connect(running->process, &QProcess::finished, this, [this, editorId](int exitCode, QProcess::ExitStatus status) {
            Running *done = m_running.value(editorId);
            if (!done)
                return;
            absorbStdout(*done, done->process->readAllStandardOutput());
            const QByteArray tail = done->process->readAllStandardError();
            done->errors += tail;
            if (m_orchestrator != nullptr && !tail.isEmpty())
                m_orchestrator->appendLog(QStringLiteral("syntax:%1").arg(editorId), QString::fromUtf8(tail));
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
                const SyntaxRequest next = done->pending;
                done->hasPending = false;
                done->discard = false;
                done->stopRequested = false;
                launch(next);
                return;
            }
            const int token = done->token;
            if (done->discard || done->stopRequested) {
                done->discard = false;
                done->stopRequested = false;
                if (m_orchestrator != nullptr)
                    m_orchestrator->release(QStringLiteral("syntax:%1").arg(editorId), token);
                return;
            }

            const bool utf8 = !done->distro.isEmpty();
            if (status == QProcess::CrashExit || exitCode != 0) {
                QString message = decoded(done->errors, utf8).trimmed();
                if (message.isEmpty())
                    message = QStringLiteral("Syntax check failed.");
                emit failed(editorId, message);
            } else {
                QList<Diagnostic> spans;
                if (!diagnosticsFromJson(done->captured, &spans))
                    emit failed(editorId, QStringLiteral("Couldn't read syntax check results."));
                else
                    emit completed(editorId, done->request.revision, spans);
            }
            if (m_orchestrator != nullptr)
                m_orchestrator->release(QStringLiteral("syntax:%1").arg(editorId), token);
        });
        connect(running->process, &QProcess::errorOccurred, this, [this, editorId](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart)
                return;
            Running *failedRun = m_running.value(editorId);
            if (!failedRun || failedRun->suppressFailure || failedRun->discard || failedRun->stopRequested)
                return;
            emit failed(editorId, failedRun->process->errorString());
            if (m_orchestrator != nullptr)
                m_orchestrator->release(QStringLiteral("syntax:%1").arg(editorId), failedRun->token);
        });
    }

    running->request = request;
    running->token = m_orchestrator == nullptr
                         ? 0
                         : m_orchestrator->currentToken(QStringLiteral("syntax:%1").arg(request.editorId));
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

void SyntaxRunner::absorbStdout(Running &running, const QByteArray &bytes)
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

void SyntaxRunner::stopTree(Running &running)
{
    running.stopRequested = true;
#ifdef Q_OS_WIN
    startGroupKill(this, running.distro, running.linuxGroup);
    if (running.job) {
        CloseHandle(static_cast<HANDLE>(running.job));
        running.job = nullptr;
    }
#endif
    running.process->kill();
}
