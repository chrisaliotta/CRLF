#include "app/CompletionSession.h"

#include "app/ProcessLaunch.h"
#include "app/ProcessOrchestrator.h"
#include "io/CommandVariables.h"
#include "io/WslPath.h"

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

CompletionSession::CompletionSession(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<CompletionItem>();
    qRegisterMetaType<QList<CompletionItem>>();
    qRegisterMetaType<SignatureHint>();
    qRegisterMetaType<QList<SignatureHint>>();
    qRegisterMetaType<CompletionFileState>();
}

CompletionSession::~CompletionSession()
{
    stopTree();
}

void CompletionSession::start(const QString &command, const QString &workingDirectory,
                              const CommandVariables &variables, bool restart)
{
    if (!restart && m_process != nullptr && m_process->state() != QProcess::NotRunning && m_command == command
        && m_workingDirectory == workingDirectory) {
        flushHeldOpens();
        return;
    }
    stopTree();
    m_command = command;
    m_workingDirectory = workingDirectory;
    m_variables = variables;
    m_token = m_orchestrator == nullptr ? 0 : m_orchestrator->currentToken(QStringLiteral("autocomplete"));
    m_stopRequested = false;
    launch();
}

void CompletionSession::request(const CompletionRequest &request)
{
    if (m_process != nullptr && m_process->state() == QProcess::Running) {
        write(request);
        return;
    }
    // Starting is not running. A write in that window is discarded, so an
    // open waits for the process. A session that is stopped holds nothing,
    // and the status bar already says it is stopped.
    if (m_process != nullptr && m_process->state() == QProcess::Starting
        && request.kind == QLatin1String("open"))
        m_heldOpens.append(request);
}

void CompletionSession::flushHeldOpens()
{
    if (m_process == nullptr || m_process->state() != QProcess::Running)
        return;
    const QList<CompletionRequest> opens = m_heldOpens;
    m_heldOpens.clear();
    for (const CompletionRequest &open : opens)
        write(open);
}

void CompletionSession::stop()
{
    m_stopRequested = true;
    m_heldOpens.clear();
    if (m_orchestrator != nullptr)
        m_orchestrator->cancel(QStringLiteral("autocomplete"));
    stopTree();
}

void CompletionSession::launch()
{
    if (m_process == nullptr) {
        m_process = new QProcess(this);
        connect(m_process, &QProcess::started, this, [this] { flushHeldOpens(); });
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
            absorbStdout(m_process->readAllStandardOutput());
        });
        connect(m_process, &QProcess::readyReadStandardError, this, [this] {
            const QByteArray bytes = m_process->readAllStandardError();
            if (bytes.isEmpty() || m_orchestrator == nullptr)
                return;
            m_orchestrator->appendLog(QStringLiteral("autocomplete"), QString::fromUtf8(bytes));
        });
        connect(m_process, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
            absorbStdout(m_process->readAllStandardOutput());
            if (!m_stopRequested && m_orchestrator != nullptr)
                m_orchestrator->release(QStringLiteral("autocomplete"), m_token);
            if (!m_stopRequested)
                emit failed(QStringLiteral("Completion stopped."));
            m_stopRequested = false;
        });
    }

    m_pending.clear();
    m_editorPaths.clear();

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
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_launch.startShell(m_process, m_workingDirectory, expandCommand(m_command, vars));
    if (m_process->state() == QProcess::NotRunning && m_process->error() == QProcess::FailedToStart)
        emit failed(m_process->errorString());
}

void CompletionSession::write(const CompletionRequest &request)
{
    QString filePath = request.path;
#ifdef Q_OS_WIN
    const WslPath::LinuxPath linux = WslPath::toLinux(request.path);
    if (linux.valid())
        filePath = linux.path;
#endif
    m_editorPaths.insert(filePath, request.path);
    QJsonObject obj;
    obj.insert(QLatin1String("kind"), request.kind);
    obj.insert(QLatin1String("id"), request.id);
    obj.insert(QLatin1String("revision"), request.revision);
    obj.insert(QLatin1String("path"), filePath);
    if (request.kind == QLatin1String("resolve")) {
        obj.insert(QLatin1String("data"), request.data);
    } else {
        obj.insert(QLatin1String("line"), request.line);
        obj.insert(QLatin1String("column"), request.column);
        obj.insert(QLatin1String("text"), request.text);
    }
    if (!request.trigger.isEmpty())
        obj.insert(QLatin1String("trigger"), request.trigger);
    m_process->write(QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n');
}

void CompletionSession::absorbStdout(const QByteArray &bytes)
{
    m_pending += m_launch.takeOutput(bytes);
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
    const QString kind = obj.value(QLatin1String("kind")).toString();
    if (kind == QLatin1String("state")) {
        CompletionFileState state;
        state.state = obj.value(QLatin1String("state")).toString();
        state.server = obj.value(QLatin1String("server")).toString();
        state.message = obj.value(QLatin1String("message")).toString();
        for (const QJsonValue &value : obj.value(QLatin1String("triggers")).toArray())
            state.triggers.append(value.toString());
        emit fileStateChanged(m_editorPaths.value(obj.value(QLatin1String("path")).toString()), state);
        return;
    }
    const int id = obj.value(QLatin1String("id")).toInt();
    const int revision = obj.value(QLatin1String("revision")).toInt();
    if (kind == QLatin1String("resolve")) {
        emit resolved(id, obj.value(QLatin1String("data")).toString(),
                      obj.value(QLatin1String("detail")).toString(),
                      obj.value(QLatin1String("documentation")).toString());
        return;
    }
    if (obj.contains(QLatin1String("signatures"))) {
        const QJsonArray signatures = obj.value(QLatin1String("signatures")).toArray();
        int which = obj.value(QLatin1String("activeSignature")).toInt();
        if (which < 0 || which >= signatures.size())
            which = 0;
        QList<SignatureHint> hints;
        for (const QJsonValue &value : signatures) {
            const QJsonObject signature = value.toObject();
            SignatureHint hint;
            hint.label = signature.value(QLatin1String("label")).toString();
            if (hint.label.isEmpty())
                continue;
            const QJsonArray parameters = signature.value(QLatin1String("parameters")).toArray();
            const int active = signature.value(QLatin1String("active")).toInt();
            if (active >= 0 && active < parameters.size())
                hint.parameter = parameters.at(active).toString();
            hints.append(hint);
        }
        if (which >= hints.size())
            which = 0;
        emit signatureReady(id, revision, hints, which);
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
        row.documentation = item.value(QLatin1String("documentation")).toString();
        row.data = item.value(QLatin1String("data")).toString();
        row.insert = item.value(QLatin1String("insert")).toString();
        if (row.insert.isEmpty())
            row.insert = row.label;
        row.kind = item.value(QLatin1String("kind")).toString();
        row.filterText = item.value(QLatin1String("filterText")).toString();
        row.preselect = item.value(QLatin1String("preselect")).toBool();
        for (const QJsonValue &extra : item.value(QLatin1String("additionalEdits")).toArray()) {
            const QJsonObject source = extra.toObject();
            CompletionEdit edit;
            edit.line = source.value(QLatin1String("line")).toInt();
            edit.column = source.value(QLatin1String("column")).toInt();
            edit.endLine = source.value(QLatin1String("endLine")).toInt();
            edit.endColumn = source.value(QLatin1String("endColumn")).toInt();
            edit.text = source.value(QLatin1String("text")).toString();
            row.additionalEdits.append(edit);
        }
        row.replaceLine = item.value(QLatin1String("replaceLine")).toInt();
        row.replaceColumn = item.value(QLatin1String("replaceColumn")).toInt();
        row.replaceEndColumn = item.value(QLatin1String("replaceEndColumn")).toInt();
        items.append(row);
    }
    emit completed(id, revision, items, obj.value(QLatin1String("incomplete")).toBool());
}

void CompletionSession::stopTree()
{
    m_launch.stopTree(this);
    if (m_process == nullptr || m_process->state() == QProcess::NotRunning)
        return;
    // The killed process can take a moment to exit. It is detached so a new
    // session can start now, and its exit does not read as this session's.
    QProcess *old = m_process;
    m_process = nullptr;
    old->disconnect(this);
    connect(old, &QProcess::finished, old, &QObject::deleteLater);
    old->kill();
}
