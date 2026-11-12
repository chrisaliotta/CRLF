#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QString>

// The parts of the Agent Client Protocol the editor reads and writes. The
// agent is the child process; the editor is the client. See docs/agent.md.

enum class AgentUpdateKind { AgentText, Thought, UserText, ToolCall, ToolCallUpdate, Plan, Ignored };

struct AgentToolLocation
{
    QString path;
    // 1-based. Zero when the agent named only the file.
    int line = 0;
};

struct AgentPlanEntry
{
    QString content;
    // pending, in_progress, or completed.
    QString status;
};

// One `session/update` notification.
struct AgentUpdate
{
    AgentUpdateKind kind = AgentUpdateKind::Ignored;
    // The chunk of a message or thought. On a tool call, its text content,
    // with a line for each file it edited.
    QString text;
    QString toolCallId;
    QString title;
    // read, edit, delete, move, search, execute, think, fetch, or other.
    QString toolKind;
    // pending, in_progress, completed, or failed. Empty on an update that
    // does not change it.
    QString status;
    QList<AgentToolLocation> locations;
    QList<AgentPlanEntry> plan;
};

Q_DECLARE_METATYPE(AgentUpdate)

// `update` is the notification's `update` object.
AgentUpdate readUpdate(const QJsonObject &update);

// One button of a `session/request_permission`.
struct PermissionOption
{
    QString id;
    QString name;
    // allow_once, allow_always, reject_once, or reject_always.
    QString kind;
};

QList<PermissionOption> readPermissionOptions(const QJsonArray &options);

// Prompt content blocks.
QJsonObject textBlock(const QString &text);
QJsonObject resourceLinkBlock(const QString &path);
QJsonObject resourceBlock(const QString &path, const QString &text);

// A percent-encoded file URI for an agent-side path.
QString fileUri(const QString &path);
