#include "app/AgentHost.h"

#include "app/AgentPanel.h"
#include "app/ProcessOrchestrator.h"
#include "app/SourceControl.h"
#include "app/TabWorkspace.h"
#include "app/TerminalPanel.h"
#include "editor/CodeEditor.h"
#include "git/LineDiff.h"
#include "io/DocumentFile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTextCursor>

namespace {

constexpr int kDetailLength = 600;

QString clipped(const QString &text)
{
    return text.size() > kDetailLength ? text.left(kDetailLength - 1) + QChar(0x2026) : text;
}

// `line` is 1-based. A limit of 0 keeps every line after it.
QString linesOf(const QString &text, int line, int limit)
{
    if (line <= 1 && limit <= 0)
        return text;
    const QStringList lines = text.split(QLatin1Char('\n'));
    const int start = qMax(0, line - 1);
    return lines.mid(start, limit > 0 ? limit : -1).join(QLatin1Char('\n'));
}

QString changeSummary(const QList<LineChange> &changes, bool exists, int lineCount)
{
    if (!exists)
        return QStringLiteral("New file, %1 lines").arg(lineCount);
    int added = 0;
    int removed = 0;
    for (const LineChange &change : changes) {
        added += change.newCount;
        removed += change.oldCount;
    }
    return QStringLiteral("+%1 \u2212%2 lines").arg(added).arg(removed);
}

} // namespace

AgentHost::AgentHost(AgentSession *session, AgentPanel *panel, TabWorkspace *tabs, SourceControl *sourceControl,
                     TerminalPanel *terminal, ProcessOrchestrator *orchestrator, QObject *parent)
    : QObject(parent)
    , m_session(session)
    , m_panel(panel)
    , m_tabs(tabs)
    , m_sourceControl(sourceControl)
    , m_terminal(terminal)
    , m_orchestrator(orchestrator)
{
    m_handlers = {
        {QStringLiteral("fs/read_text_file"), &AgentHost::readFile},
        {QStringLiteral("fs/write_text_file"), &AgentHost::writeFile},
        {QStringLiteral("session/request_permission"), &AgentHost::requestPermission},
        {QStringLiteral("terminal/create"), &AgentHost::createTerminal},
        {QStringLiteral("_crlf/editor/state"), &AgentHost::editorState},
        {QStringLiteral("_crlf/problems"), &AgentHost::problems},
        {QStringLiteral("_crlf/git/status"), &AgentHost::gitStatus},
        {QStringLiteral("_crlf/terminal/read"), &AgentHost::readTerminal},
        {QStringLiteral("_crlf/terminal/send"), &AgentHost::sendTerminal},
    };
}

void AgentHost::begin(const QString &workingDirectory, const AgentPermissions &permissions)
{
    m_root = workingDirectory;
    m_policy = AgentPolicy(workingDirectory, permissions);
}

void AgentHost::handle(const AgentRequest &request)
{
    const Handler handler = m_handlers.value(request.method);
    if (handler == nullptr) {
        reject(request.id, RpcCode::MethodNotFound, QStringLiteral("CRLF does not answer %1").arg(request.method));
        return;
    }
    (this->*handler)(request);
}

void AgentHost::readFile(const AgentRequest &request)
{
    const QString path = m_policy.resolve(request.params.value(QLatin1String("path")).toString());
    const int line = request.params.value(QLatin1String("line")).toInt();
    const int limit = request.params.value(QLatin1String("limit")).toInt();
    const QJsonValue id = request.id;
    authorize(request, AgentAction::Read, path, QStringLiteral("Read a file"), shown(path), [this, id, path, line, limit] {
        currentText(path, [this, id, path, line, limit](bool exists, const QString &text) {
            if (!exists) {
                reject(id, RpcCode::NotFound, QStringLiteral("%1 does not exist").arg(path));
                return;
            }
            reply(id, QJsonObject{{QStringLiteral("content"), linesOf(text, line, limit)}});
        });
    });
}

void AgentHost::writeFile(const AgentRequest &request)
{
    const QString path = m_policy.resolve(request.params.value(QLatin1String("path")).toString());
    QString text = request.params.value(QLatin1String("content")).toString();
    text.remove(QLatin1Char('\r'));
    switch (m_policy.decide(AgentAction::Write, path)) {
    case AgentAccess::Allow:
        applyWrite(request, path, text);
        return;
    case AgentAccess::Deny:
        reject(request.id, RpcCode::Rejected, QStringLiteral("This project does not let the agent write files."));
        return;
    case AgentAccess::Ask:
    case AgentAccess::Review:
        reviewWrite(request, path, text);
        return;
    }
}

void AgentHost::reviewWrite(const AgentRequest &request, const QString &path, const QString &text)
{
    currentText(path, [this, request, path, text](bool exists, const QString &current) {
        const QList<LineChange> changes = diffLines(splitLines(current), splitLines(text));
        const QJsonValue id = request.id;
        DiffContent content;
        content.title = QStringLiteral("%1 (Current \u2194 Agent)").arg(QFileInfo(path).fileName());
        content.path = path;
        content.leftLabel = exists ? QStringLiteral("Current") : QStringLiteral("New file");
        content.rightLabel = QStringLiteral("Agent");
        content.left = current;
        content.right = text;
        QList<AgentChoice> choices{
            {QStringLiteral("Open Diff"), [this, content, changes] { m_tabs->showDiff(content, changes); }, false},
            {QStringLiteral("Accept"), [this, request, path, text] { applyWrite(request, path, text); }},
        };
        if (m_policy.inside(path)) {
            choices.append({QStringLiteral("Accept All"), [this, request, path, text] {
                                m_policy.grant(AgentAction::Write);
                                applyWrite(request, path, text);
                            }});
        }
        choices.append({QStringLiteral("Reject"),
                        [this, id] { reject(id, RpcCode::Rejected, QStringLiteral("The user rejected this change.")); }});
        m_panel->ask(QStringLiteral("Write %1").arg(shown(path)),
                     changeSummary(changes, exists, splitLines(text).size()), choices,
                     [this, id] { reject(id, RpcCode::Rejected, QStringLiteral("The turn was cancelled.")); });
    });
}

void AgentHost::applyWrite(const AgentRequest &request, const QString &path, const QString &text)
{
    const QJsonValue id = request.id;
    if (CodeEditor *editor = m_tabs->editorForPath(path)) {
        m_tabs->writeEditor(editor, text);
        reply(id, QJsonObject());
        return;
    }
    QPointer<AgentHost> alive(this);
    m_orchestrator->submit(QStringLiteral("agent-io:%1").arg(++m_reads), QStringLiteral("Agent write"), shown(path), 0,
                           path, [alive, id, path, text] {
        DocumentFile file(path);
        if (QFileInfo::exists(path)) {
            file.load();
        } else {
            QDir().mkpath(QFileInfo(path).absolutePath());
            file.decode(text.toUtf8());
        }
        file.save(text);
        QMetaObject::invokeMethod(QCoreApplication::instance(), [alive, id] {
            if (alive.isNull())
                return;
            alive->reply(id, QJsonObject());
            alive->m_sourceControl->refresh();
        }, Qt::QueuedConnection);
    });
}

void AgentHost::requestPermission(const AgentRequest &request)
{
    const QJsonObject toolCall = request.params.value(QLatin1String("toolCall")).toObject();
    QString title = toolCall.value(QLatin1String("title")).toString();
    if (title.isEmpty())
        title = QStringLiteral("The agent asks for permission");
    const QJsonValue rawInput = toolCall.value(QLatin1String("rawInput"));
    const QString detail = rawInput.isObject()
                               ? clipped(QString::fromUtf8(QJsonDocument(rawInput.toObject()).toJson(QJsonDocument::Compact)))
                               : QString();
    const QJsonValue id = request.id;
    const auto outcome = [this, id](const QJsonObject &chosen) {
        reply(id, QJsonObject{{QStringLiteral("outcome"), chosen}});
    };
    QList<AgentChoice> choices;
    for (const PermissionOption &option : readPermissionOptions(request.params.value(QLatin1String("options")).toArray())) {
        choices.append({option.name, [outcome, option] {
                            outcome({{QStringLiteral("outcome"), QStringLiteral("selected")},
                                     {QStringLiteral("optionId"), option.id}});
                        }});
    }
    m_panel->ask(title, detail, choices, [outcome] { outcome({{QStringLiteral("outcome"), QStringLiteral("cancelled")}}); });
}

void AgentHost::createTerminal(const AgentRequest &request)
{
    QString cwd = request.params.value(QLatin1String("cwd")).toString();
    cwd = cwd.isEmpty() ? m_root : m_policy.resolve(cwd);
    QStringList line{request.params.value(QLatin1String("command")).toString()};
    for (const QJsonValue &arg : request.params.value(QLatin1String("args")).toArray())
        line.append(arg.toString());
    AgentRequest approved = request;
    approved.params.insert(QLatin1String("cwd"), cwd);
    authorize(request, AgentAction::Terminal, cwd, QStringLiteral("Run a command"),
              clipped(line.join(QLatin1Char(' '))) + QStringLiteral("\nin ") + QDir::toNativeSeparators(cwd),
              [this, approved] {
                  QMetaObject::invokeMethod(m_session, [session = m_session, approved] { session->createTerminal(approved); },
                                            Qt::QueuedConnection);
              });
}

void AgentHost::editorState(const AgentRequest &request)
{
    const QJsonValue id = request.id;
    authorize(request, AgentAction::Read, m_root, QStringLiteral("See the open files"), QString(), [this, id] {
        QJsonArray files;
        for (CodeEditor *editor : m_tabs->editors()) {
            if (editor->filePath().isEmpty())
                continue;
            files.append(QJsonObject{{QStringLiteral("path"), agentPath(m_root, editor->filePath())},
                                     {QStringLiteral("modified"), editor->document()->isModified()}});
        }
        QJsonObject state{{QStringLiteral("files"), files}};
        if (CodeEditor *editor = m_tabs->currentEditor(); editor != nullptr && !editor->filePath().isEmpty()) {
            const QTextCursor cursor = editor->textCursor();
            QString selection = cursor.selectedText();
            selection.replace(QChar(0x2029), QLatin1Char('\n'));
            state.insert(QLatin1String("activeFile"), agentPath(m_root, editor->filePath()));
            state.insert(QLatin1String("line"), cursor.blockNumber() + 1);
            state.insert(QLatin1String("column"), cursor.positionInBlock() + 1);
            state.insert(QLatin1String("selection"), selection);
        }
        reply(id, state);
    });
}

void AgentHost::problems(const AgentRequest &request)
{
    const QJsonValue id = request.id;
    authorize(request, AgentAction::Read, m_root, QStringLiteral("See the problems"), QString(), [this, id] {
        QJsonArray found;
        for (CodeEditor *editor : m_tabs->editors()) {
            if (editor->filePath().isEmpty())
                continue;
            for (const Diagnostic &span : editor->diagnostics()) {
                found.append(QJsonObject{{QStringLiteral("path"), agentPath(m_root, editor->filePath())},
                                         {QStringLiteral("line"), span.line},
                                         {QStringLiteral("column"), span.column},
                                         {QStringLiteral("severity"), span.severity},
                                         {QStringLiteral("message"), span.message}});
            }
        }
        reply(id, QJsonObject{{QStringLiteral("problems"), found}});
    });
}

void AgentHost::gitStatus(const AgentRequest &request)
{
    const QJsonValue id = request.id;
    authorize(request, AgentAction::Read, m_root, QStringLiteral("See git status"), QString(), [this, id] {
        QJsonArray repositories;
        for (const QString &root : m_sourceControl->roots()) {
            const GitStatus *status = m_sourceControl->status(root);
            if (status == nullptr)
                continue;
            QJsonArray changes;
            for (const GitChange &change : status->changes) {
                QJsonObject entry{{QStringLiteral("path"), change.path},
                                  {QStringLiteral("index"), QString(change.index)},
                                  {QStringLiteral("worktree"), QString(change.worktree)}};
                if (!change.originalPath.isEmpty())
                    entry.insert(QLatin1String("originalPath"), change.originalPath);
                if (change.conflicted)
                    entry.insert(QLatin1String("conflicted"), true);
                changes.append(entry);
            }
            repositories.append(QJsonObject{{QStringLiteral("root"), agentPath(m_root, root)},
                                            {QStringLiteral("branch"), status->branch},
                                            {QStringLiteral("head"), status->head},
                                            {QStringLiteral("upstream"), status->upstream},
                                            {QStringLiteral("ahead"), status->ahead},
                                            {QStringLiteral("behind"), status->behind},
                                            {QStringLiteral("merging"), status->merging},
                                            {QStringLiteral("changes"), changes}});
        }
        reply(id, QJsonObject{{QStringLiteral("enabled"), m_sourceControl->enabled()},
                              {QStringLiteral("repositories"), repositories}});
    });
}

void AgentHost::readTerminal(const AgentRequest &request)
{
    const QJsonValue id = request.id;
    authorize(request, AgentAction::Read, m_root, QStringLiteral("Read your terminal"), QString(), [this, id] {
        reply(id, QJsonObject{{QStringLiteral("text"), m_terminal->transcript()}});
    });
}

void AgentHost::sendTerminal(const AgentRequest &request)
{
    if (m_terminal->sessionCount() == 0) {
        reject(request.id, RpcCode::Internal, QStringLiteral("No terminal is open."));
        return;
    }
    const QString text = request.params.value(QLatin1String("text")).toString();
    const QJsonValue id = request.id;
    authorize(request, AgentAction::Terminal, m_root, QStringLiteral("Type into your terminal"), clipped(text),
              [this, id, text] {
                  m_terminal->sendInput(text);
                  reply(id, QJsonObject());
              });
}

void AgentHost::authorize(const AgentRequest &request, AgentAction action, const QString &path, const QString &title,
                          const QString &detail, std::function<void()> allowed)
{
    const QJsonValue id = request.id;
    switch (m_policy.decide(action, path)) {
    case AgentAccess::Allow:
        allowed();
        return;
    case AgentAccess::Deny:
        reject(id, RpcCode::Rejected, QStringLiteral("This project does not allow that."));
        return;
    case AgentAccess::Ask:
    case AgentAccess::Review:
        break;
    }
    QList<AgentChoice> choices{{QStringLiteral("Allow"), allowed}};
    if (m_policy.inside(path)) {
        choices.append({QStringLiteral("Always Allow"), [this, action, allowed] {
                            m_policy.grant(action);
                            allowed();
                        }});
    }
    choices.append({QStringLiteral("Reject"),
                    [this, id] { reject(id, RpcCode::Rejected, QStringLiteral("The user rejected this request.")); }});
    m_panel->ask(title, detail, choices,
                 [this, id] { reject(id, RpcCode::Rejected, QStringLiteral("The turn was cancelled.")); });
}

void AgentHost::currentText(const QString &path, std::function<void(bool exists, const QString &text)> done)
{
    if (CodeEditor *editor = m_tabs->editorForPath(path)) {
        done(true, editor->toPlainText());
        return;
    }
    QPointer<AgentHost> alive(this);
    m_orchestrator->submit(QStringLiteral("agent-io:%1").arg(++m_reads), QStringLiteral("Agent read"), shown(path), 0,
                           path, [alive, path, done] {
        const bool exists = QFileInfo(path).isFile();
        DocumentFile file(path);
        const QString text = exists ? file.load() : QString();
        QMetaObject::invokeMethod(QCoreApplication::instance(), [alive, done, exists, text] {
            if (!alive.isNull())
                done(exists, text);
        }, Qt::QueuedConnection);
    });
}

void AgentHost::reply(const QJsonValue &id, const QJsonValue &result)
{
    QMetaObject::invokeMethod(m_session, [session = m_session, id, result] { session->reply(id, result); },
                              Qt::QueuedConnection);
}

void AgentHost::reject(const QJsonValue &id, int code, const QString &message)
{
    QMetaObject::invokeMethod(m_session, [session = m_session, id, code, message] { session->replyError(id, code, message); },
                              Qt::QueuedConnection);
}

QString AgentHost::shown(const QString &path) const
{
    return QDir::toNativeSeparators(m_policy.inside(path) ? QDir(m_root).relativeFilePath(path) : path);
}
