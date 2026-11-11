#include "app/ProcessLaunch.h"

#include "io/WslPath.h"

#include <QProcess>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

const QByteArray kGroupMarker = QByteArrayLiteral("CRLF_PGID:");
const QString kWslExe = QStringLiteral("C:/Windows/System32/wsl.exe");

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

ProcessLaunch::~ProcessLaunch()
{
    closeJob();
}

void ProcessLaunch::startShell(QProcess *process, const QString &workingDirectory, const QString &commandText)
{
    m_distro.clear();
    m_linuxGroup = 0;
    m_groupPending = false;
    m_head.clear();
#ifdef Q_OS_WIN
    const WslPath::LinuxPath linux = WslPath::toLinux(workingDirectory);
    if (linux.valid()) {
        m_distro = linux.distro;
        m_groupPending = true;
        const QString script = QStringLiteral("printf 'CRLF_PGID:%s\\n' \"$$\"; ") + commandText;
        process->setProgram(kWslExe);
        process->setNativeArguments({});
        process->setArguments({QStringLiteral("-d"), linux.distro, QStringLiteral("--cd"), linux.path,
                               QStringLiteral("--"), QStringLiteral("bash"), QStringLiteral("-lc"), script});
        // cmd.exe cannot use a UNC path as its current directory, and wsl.exe
        // takes the Linux path through --cd instead.
        process->setWorkingDirectory(QString());
    } else {
        process->setProgram(qEnvironmentVariable("ComSpec", QStringLiteral("C:\\Windows\\System32\\cmd.exe")));
        process->setArguments({});
        process->setNativeArguments(QStringLiteral("/d /s /c ") + commandText);
        process->setWorkingDirectory(workingDirectory);
    }
#else
    process->setProgram(QStringLiteral("/bin/sh"));
    process->setArguments({QStringLiteral("-c"), commandText});
    process->setWorkingDirectory(workingDirectory);
#endif
    start(process, true);
}

void ProcessLaunch::startProgram(QProcess *process, const QString &workingDirectory, const QString &program,
                                 const QStringList &arguments)
{
    m_distro.clear();
    m_linuxGroup = 0;
    m_groupPending = false;
    m_head.clear();
#ifdef Q_OS_WIN
    process->setNativeArguments({});
    const WslPath::LinuxPath linux = WslPath::toLinux(workingDirectory);
    if (linux.valid()) {
        m_distro = linux.distro;
        process->setProgram(kWslExe);
        process->setArguments(QStringList{QStringLiteral("-d"), linux.distro, QStringLiteral("--cd"), linux.path,
                                          QStringLiteral("--exec"), program}
                              + arguments);
        process->setWorkingDirectory(QString());
        start(process, true);
        return;
    }
#endif
    process->setProgram(program);
    process->setArguments(arguments);
    process->setWorkingDirectory(workingDirectory);
    start(process, true);
}

void ProcessLaunch::start(QProcess *process, bool withJob)
{
#ifdef Q_OS_WIN
    closeJob();
    STARTUPINFOEXW extended = {};
    LPPROC_THREAD_ATTRIBUTE_LIST attributes = nullptr;
    HANDLE jobHandle = nullptr;
    if (withJob) {
        m_job = createKillJob();
        jobHandle = static_cast<HANDLE>(m_job);
        withJob = jobHandle != nullptr;
    }
    if (withJob) {
        process->setCreateProcessArgumentsModifier(
            [&extended, &attributes, &jobHandle](QProcess::CreateProcessArguments *args) {
                assignJobOnCreate(&jobHandle, args, &extended, &attributes);
            });
    } else {
        process->setCreateProcessArgumentsModifier({});
    }
    m_retrying = withJob;
    process->start();
    m_retrying = false;
    process->setCreateProcessArgumentsModifier({});
    freeAttributes(attributes);
    // Some parent jobs reject a nested job. Run the process anyway.
    if (withJob && process->state() == QProcess::NotRunning && process->error() == QProcess::FailedToStart)
        start(process, false);
#else
    Q_UNUSED(withJob);
    process->start();
#endif
}

void ProcessLaunch::closeJob()
{
#ifdef Q_OS_WIN
    if (m_job) {
        CloseHandle(static_cast<HANDLE>(m_job));
        m_job = nullptr;
    }
#endif
}

void ProcessLaunch::stopTree(QObject *owner)
{
#ifdef Q_OS_WIN
    if (!m_distro.isEmpty() && m_linuxGroup > 0) {
        auto *killer = new QProcess(owner);
        killer->setProgram(kWslExe);
        killer->setArguments({QStringLiteral("-d"), m_distro, QStringLiteral("--"), QStringLiteral("kill"),
                              QStringLiteral("--"), QString::number(-m_linuxGroup)});
        QObject::connect(killer, &QProcess::finished, killer, &QObject::deleteLater);
        QObject::connect(killer, &QProcess::errorOccurred, killer, [killer](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
                killer->deleteLater();
        });
        killer->start();
    }
    closeJob();
#else
    Q_UNUSED(owner);
#endif
}

QByteArray ProcessLaunch::takeOutput(const QByteArray &bytes)
{
    if (!m_groupPending)
        return bytes;
    m_head += bytes;
    const int newline = m_head.indexOf('\n');
    if (newline < 0)
        return {};
    const QByteArray first = m_head.left(newline).trimmed();
    QByteArray rest = m_head.mid(newline + 1);
    m_head.clear();
    m_groupPending = false;
    if (first.startsWith(kGroupMarker))
        m_linuxGroup = first.mid(kGroupMarker.size()).toLongLong();
    else
        rest.prepend(first + '\n');
    return rest;
}

QByteArray ProcessLaunch::flushOutput()
{
    if (!m_groupPending)
        return {};
    m_groupPending = false;
    const QByteArray held = m_head;
    m_head.clear();
    return held.trimmed().startsWith(kGroupMarker) ? QByteArray() : held;
}
