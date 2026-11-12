#include "agent/AcpTypes.h"

#include <QFileInfo>
#include <QUrl>

namespace {

QString blockText(const QJsonObject &block)
{
    return block.value(QLatin1String("type")).toString() == QLatin1String("text")
               ? block.value(QLatin1String("text")).toString()
               : QString();
}

// A tool call's `content`: text blocks, diffs, and terminals.
QString toolText(const QJsonArray &content)
{
    QStringList parts;
    for (const QJsonValue &value : content) {
        const QJsonObject item = value.toObject();
        const QString type = item.value(QLatin1String("type")).toString();
        if (type == QLatin1String("content"))
            parts.append(blockText(item.value(QLatin1String("content")).toObject()));
        else if (type == QLatin1String("diff"))
            parts.append(QStringLiteral("Edited ") + item.value(QLatin1String("path")).toString());
    }
    parts.removeAll(QString());
    return parts.join(QLatin1Char('\n'));
}

} // namespace

AgentUpdate readUpdate(const QJsonObject &update)
{
    AgentUpdate result;
    const QString kind = update.value(QLatin1String("sessionUpdate")).toString();
    const QJsonObject content = update.value(QLatin1String("content")).toObject();
    if (kind == QLatin1String("agent_message_chunk")) {
        result.kind = AgentUpdateKind::AgentText;
        result.text = blockText(content);
    } else if (kind == QLatin1String("agent_thought_chunk")) {
        result.kind = AgentUpdateKind::Thought;
        result.text = blockText(content);
    } else if (kind == QLatin1String("user_message_chunk")) {
        result.kind = AgentUpdateKind::UserText;
        result.text = blockText(content);
    } else if (kind == QLatin1String("tool_call") || kind == QLatin1String("tool_call_update")) {
        result.kind = kind == QLatin1String("tool_call") ? AgentUpdateKind::ToolCall : AgentUpdateKind::ToolCallUpdate;
        result.toolCallId = update.value(QLatin1String("toolCallId")).toString();
        result.title = update.value(QLatin1String("title")).toString();
        result.toolKind = update.value(QLatin1String("kind")).toString();
        result.status = update.value(QLatin1String("status")).toString();
        result.text = toolText(update.value(QLatin1String("content")).toArray());
        for (const QJsonValue &value : update.value(QLatin1String("locations")).toArray()) {
            const QJsonObject location = value.toObject();
            result.locations.append({location.value(QLatin1String("path")).toString(),
                                     location.value(QLatin1String("line")).toInt()});
        }
    } else if (kind == QLatin1String("plan")) {
        result.kind = AgentUpdateKind::Plan;
        for (const QJsonValue &value : update.value(QLatin1String("entries")).toArray()) {
            const QJsonObject entry = value.toObject();
            result.plan.append({entry.value(QLatin1String("content")).toString(),
                                entry.value(QLatin1String("status")).toString()});
        }
    }
    return result;
}

QList<PermissionOption> readPermissionOptions(const QJsonArray &options)
{
    QList<PermissionOption> result;
    for (const QJsonValue &value : options) {
        const QJsonObject option = value.toObject();
        result.append({option.value(QLatin1String("optionId")).toString(),
                       option.value(QLatin1String("name")).toString(),
                       option.value(QLatin1String("kind")).toString()});
    }
    return result;
}

QJsonObject textBlock(const QString &text)
{
    return {{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), text}};
}

QJsonObject resourceLinkBlock(const QString &path)
{
    return {{QStringLiteral("type"), QStringLiteral("resource_link")},
            {QStringLiteral("uri"), fileUri(path)},
            {QStringLiteral("name"), QFileInfo(path).fileName()}};
}

QJsonObject resourceBlock(const QString &path, const QString &text)
{
    return {{QStringLiteral("type"), QStringLiteral("resource")},
            {QStringLiteral("resource"),
             QJsonObject{{QStringLiteral("uri"), fileUri(path)}, {QStringLiteral("text"), text}}}};
}

QString fileUri(const QString &path)
{
    return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
}
