#include "app/CompletionSession.h"

#include "app/ProcessOrchestrator.h"
#include "io/CommandVariables.h"
#include "io/WslPath.h"

#include <QJsonArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

CompletionSession::CompletionSession(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<CompletionItem>();
    qRegisterMetaType<QList<CompletionItem>>();
}

CompletionSession::~CompletionSession()
{
    stopTree();
}

void CompletionSession::start(const QString &command, const QString &workingDirectory,
                              const CommandVariables &variables)
{
    if (m_process != nullptr && m_process->state() != QProcess::NotRunning && m_command == command
        && m_workingDirectory == workingDirectory)
        return;
    stopTree();
    m_command = command;
    m_workingDirectory = workingDirectory;
    m_variables = variables;
    m_token = m_orchestrator == nullptr ? 0 : m_orchestrator->currentToken(QStringLiteral("complete"));
    m_stopRequested = false;
    launch(true);
}

void CompletionSession::request(int id, int revision, const QString &path, int line, int column,
                                const QString &text)
{
    if (m_process == nullptr || m_process->state() == QProcess::NotRunning) {
        emit failed(QStringLiteral("Completion is not running."));
        return;
    }
    writeRequest(id, revision, path, line, column, text);
}

void CompletionSession::stop()
{
    m_stopRequested = true;
    if (m_orchestrator != nullptr)
        m_orchestrator->cancel(QStringLiteral("complete"));
    stopTree();
}

void CompletionSession::launch(bool withJob)
{
    if (m_process == nullptr) {
        m_process = new QProcess(this);
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
            absorbStdout(m_process->readAllStandardOutput());
        });
        connect(m_process, &QProcess::readyReadStandardError, this, [this] {
            const QString text = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
            if (!text.isEmpty())
                emit failed(text);
        });
        connect(m_process, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
            absorbStdout(m_process->readAllStandardOutput());
            if (!m_stopRequested && m_orchestrator != nullptr)
                m_orchestrator->release(QStringLiteral("complete"), m_token);
            if (!m_stopRequested)
                emit failed(QStringLiteral("Completion stopped."));
            m_stopRequested = false;
        });
    }

    m_distro.clear();
    m_linuxGroup = 0;
    m_groupResolved = false;
    m_pending.clear();

    CommandVariables vars = m_variables;
    vars.cwd = m_workingDirectory;
#ifdef Q_OS_WIN
    const WslPath::LinuxPath linux = WslPath::toLinux(m_workingDirectory);
    if (linux.valid()) {
        vars.file = linuxPath(vars.file);
        vars.fileDir = linuxPath(vars.fileDir);
        vars.project = linuxPath(vars.project);
        vars.folder = linuxPath(vars.folder);
        vars.cwd = linux.path;
    }
#endif
    const QString commandText = expandCommand(m_command, vars);

#ifdef Q_OS_WIN
    if (m_job) {
        CloseHandle(static_cast<HANDLE>(m_job));
        m_job = nullptr;
    }
    STARTUPINFOEXW extended = {};
    LPPROC_THREAD_ATTRIBUTE_LIST attributes = nullptr;
    HANDLE jobHandle = nullptr;
    if (withJob) {
        m_job = createKillJob();
        jobHandle = static_cast<HANDLE>(m_job);
        withJob = jobHandle != nullptr;
    }
    if (withJob) {
        m_process->setCreateProcessArgumentsModifier(
            [&extended, &attributes, &jobHandle](QProcess::CreateProcessArguments *args) {
                assignJobOnCreate(&jobHandle, args, &extended, &attributes);
            });
    } else {
        m_process->setCreateProcessArgumentsModifier({});
    }
    if (linux.valid()) {
        m_distro = linux.distro;
        const QString script = QStringLiteral("printf 'CRLF_PGID:%s\\n' \"$$\"; ") + commandText;
        m_process->setProgram(QStringLiteral("C:/Windows/System32/wsl.exe"));
        m_process->setNativeArguments({});
        m_process->setArguments({QStringLiteral("-d"), linux.distro, QStringLiteral("--cd"), linux.path,
                                 QStringLiteral("--"), QStringLiteral("bash"), QStringLiteral("-lc"), script});
        m_process->setWorkingDirectory(QString());
    } else {
        m_process->setProgram(qEnvironmentVariable("ComSpec", QStringLiteral("C:\\Windows\\System32\\cmd.exe")));
        m_process->setArguments({});
        m_process->setNativeArguments(QStringLiteral("/d /s /c ") + commandText);
        m_process->setWorkingDirectory(m_workingDirectory);
    }
#else
    Q_UNUSED(withJob);
    m_process->setProgram(QStringLiteral("/bin/sh"));
    m_process->setArguments({QStringLiteral("-c"), commandText});
    m_process->setWorkingDirectory(m_workingDirectory);
#endif
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->start();
#ifdef Q_OS_WIN
    m_process->setCreateProcessArgumentsModifier({});
    freeAttributes(attributes);
    if (withJob && m_process->error() == QProcess::FailedToStart) {
        launch(false);
        return;
    }
#endif
    if (m_process->error() == QProcess::FailedToStart)
        emit failed(m_process->errorString());
}

void CompletionSession::writeRequest(int id, int revision, const QString &path, int line, int column,
                                     const QString &text)
{
    QJsonObject obj;
    obj.insert(QLatin1String("id"), id);
    obj.insert(QLatin1String("revision"), revision);
    obj.insert(QLatin1String("path"), path);
    obj.insert(QLatin1String("line"), line);
    obj.insert(QLatin1String("column"), column);
    obj.insert(QLatin1String("text"), text);
    const QByteArray lineBytes = QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
    m_process->write(lineBytes);
}

void CompletionSession::absorbStdout(const QByteArray &bytes)
{
    m_pending += bytes;
    if (!m_distro.isEmpty() && !m_groupResolved) {
        const int newline = m_pending.indexOf('\n');
        if (newline < 0)
            return;
        const QByteArray first = m_pending.left(newline);
        m_pending = m_pending.mid(newline + 1);
        m_groupResolved = true;
        const QString marker = QStringLiteral("CRLF_PGID:");
        const QString header = QString::fromUtf8(first).trimmed();
        if (header.startsWith(marker))
            m_linuxGroup = header.mid(marker.size()).toLongLong();
        else
            m_pending.prepend(first + '\n');
    }
    int newline = m_pending.indexOf('\n');
    while (newline >= 0) {
        takeLine(m_pending.left(newline));
        m_pending = m_pending.mid(newline + 1);
        newline = m_pending.indexOf('\n');
    }
}

void CompletionSession::takeLine(const QByteArray &line)
{
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty())
        return;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(trimmed, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit failed(QStringLiteral("Couldn't read completion results."));
        return;
    }
    const QJsonObject obj = document.object();
    const QString error = obj.value(QLatin1String("error")).toString();
    if (!error.isEmpty()) {
        emit failed(error);
        return;
    }
    QList<CompletionItem> items;
    for (const QJsonValue &value : obj.value(QLatin1String("items")).toArray()) {
        const QJsonObject item = value.toObject();
        CompletionItem row;
        row.label = item.value(QLatin1String("label")).toString();
        if (row.label.isEmpty())
            continue;
        row.detail = item.value(QLatin1String("detail")).toString();
        row.insert = item.value(QLatin1String("insert")).toString();
        if (row.insert.isEmpty())
            row.insert = row.label;
        row.replaceLine = item.value(QLatin1String("replaceLine")).toInt();
        row.replaceColumn = item.value(QLatin1String("replaceColumn")).toInt();
        row.replaceEndColumn = item.value(QLatin1String("replaceEndColumn")).toInt();
        items.append(row);
    }
    emit completed(obj.value(QLatin1String("id")).toInt(), obj.value(QLatin1String("revision")).toInt(), items);
}

void CompletionSession::stopTree()
{
#ifdef Q_OS_WIN
    startGroupKill(this, m_distro, m_linuxGroup);
    if (m_job) {
        CloseHandle(static_cast<HANDLE>(m_job));
        m_job = nullptr;
    }
#endif
    if (m_process != nullptr && m_process->state() != QProcess::NotRunning)
        m_process->kill();
}
