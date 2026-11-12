#include "app/CompletionSession.h"

#include "app/JsonLineProcess.h"
#include "io/WslPath.h"

#include <QJsonArray>

namespace {

const QString kKey = QStringLiteral("autocomplete");

} // namespace

CompletionSession::CompletionSession(QObject *parent)
    : QObject(parent)
    , m_process(new JsonLineProcess(kKey, this))
{
    qRegisterMetaType<CompletionItem>();
    qRegisterMetaType<QList<CompletionItem>>();
    qRegisterMetaType<SignatureHint>();
    qRegisterMetaType<QList<SignatureHint>>();
    qRegisterMetaType<CompletionFileState>();
    connect(m_process, &JsonLineProcess::started, this, &CompletionSession::flushHeldOpens);
    connect(m_process, &JsonLineProcess::received, this, &CompletionSession::take);
    connect(m_process, &JsonLineProcess::unreadable, this,
            [this] { emit failed(QStringLiteral("Couldn't read completion results.")); });
    connect(m_process, &JsonLineProcess::exited, this, [this] { emit failed(QStringLiteral("Completion stopped.")); });
    connect(m_process, &JsonLineProcess::failedToStart, this, &CompletionSession::failed);
}

void CompletionSession::setOrchestrator(ProcessOrchestrator *orchestrator)
{
    m_process->setOrchestrator(orchestrator);
}

void CompletionSession::start(const QString &command, const QString &workingDirectory,
                              const CommandVariables &variables, bool restart)
{
    if (!restart && m_process->runs(command, workingDirectory)) {
        flushHeldOpens();
        return;
    }
    m_editorPaths.clear();
    m_process->start(command, workingDirectory, variables);
}

void CompletionSession::request(const CompletionRequest &request)
{
    if (m_process->running()) {
        write(request);
        return;
    }
    // Starting is not running. A write in that window is discarded, so an
    // open waits for the process. A session that is stopped holds nothing,
    // and the status bar already says it is stopped.
    if (m_process->starting() && request.kind == QLatin1String("open"))
        m_heldOpens.append(request);
}

void CompletionSession::flushHeldOpens()
{
    if (!m_process->running())
        return;
    const QList<CompletionRequest> opens = m_heldOpens;
    m_heldOpens.clear();
    for (const CompletionRequest &open : opens)
        write(open);
}

void CompletionSession::stop()
{
    m_heldOpens.clear();
    m_process->stop();
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
    m_process->write(obj);
}

void CompletionSession::take(const QJsonObject &obj)
{
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
