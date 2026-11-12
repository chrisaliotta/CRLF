#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QVariant>

// The parts of the Agent Client Protocol the editor reads and writes. The
// agent is the child process; the editor is the client. See docs/agent.md.

enum class AgentUpdateKind { AgentText, Thought, UserText, ToolCall, ToolCallUpdate, Plan, ConfigOptions, Ignored };

// One value a select option can take. `group` is the label of the group the
// agent listed it under, or empty.
struct AgentConfigChoice
{
    QString value;
    QString name;
    QString description;
    QString group;
};

// One setting an agent offers for its session: a model, a reasoning level,
// or anything else it names. The editor draws it and sends back the choice;
// it does not know what the setting means.
struct AgentConfigOption
{
    QString id;
    QString name;
    QString description;
    // model, mode, model_config, thought_level, or the agent's own.
    QString category;
    // "select" or "boolean".
    QString type;
    // A choice's `value` for a select, a bool for a boolean.
    QVariant currentValue;
    QList<AgentConfigChoice> choices;
};

Q_DECLARE_METATYPE(AgentConfigOption)

// A `configOptions` array. Groups are flattened, in order. An option whose
// type is neither select nor boolean is left out; the agent keeps its value.
QList<AgentConfigOption> readConfigOptions(const QJsonArray &options);

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
    // The whole list, on a config_option_update.
    QList<AgentConfigOption> configOptions;
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
