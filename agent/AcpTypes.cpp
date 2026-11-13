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

AgentUsage readUsage(const QJsonObject &update)
{
    AgentUsage usage;
    usage.used = update.value(QLatin1String("used")).toInteger();
    usage.size = update.value(QLatin1String("size")).toInteger();
    const QJsonValue cost = update.value(QLatin1String("cost"));
    usage.hasCost = cost.isObject();
    usage.cost = cost.toObject().value(QLatin1String("amount")).toDouble();
    usage.currency = cost.toObject().value(QLatin1String("currency")).toString();
    const QJsonValue totals = update.value(QLatin1String("_meta")).toObject().value(QLatin1String("crlf"));
    usage.hasTotals = totals.isObject();
    usage.inputTokens = totals.toObject().value(QLatin1String("inputTokens")).toInteger();
    usage.cachedTokens = totals.toObject().value(QLatin1String("cachedTokens")).toInteger();
    usage.outputTokens = totals.toObject().value(QLatin1String("outputTokens")).toInteger();
    return usage;
}

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
    } else if (kind == QLatin1String("config_option_update")) {
        result.kind = AgentUpdateKind::ConfigOptions;
        result.configOptions = readConfigOptions(update.value(QLatin1String("configOptions")).toArray());
    } else if (kind == QLatin1String("usage_update")) {
        result.kind = AgentUpdateKind::Usage;
        result.usage = readUsage(update);
    }
    return result;
}

namespace {

constexpr int kNearlyFullPercent = 90;

// 842, 84k, 1.2M: enough to judge a size by.
QString tokenCount(qint64 tokens)
{
    if (tokens < 1000)
        return QString::number(tokens);
    if (tokens < 1000000)
        return QString::number(qRound64(tokens / 1000.0)) + QLatin1Char('k');
    return QString::number(tokens / 1000000.0, 'f', 1) + QLatin1Char('M');
}

QString money(double amount, const QString &currency)
{
    const bool dollars = currency == QLatin1String("USD");
    const auto shown = [&](double value) {
        const QString number = QString::number(value, 'f', 2);
        return dollars ? QLatin1Char('$') + number : number + QLatin1Char(' ') + currency;
    };
    // A cost too small for two decimals is still a cost.
    if (amount > 0 && amount < 0.01)
        return QStringLiteral("less than ") + shown(0.01);
    return QStringLiteral("about ") + shown(amount);
}

} // namespace

QString usageLine(const AgentUsage &usage)
{
    QString line;
    if (usage.size > 0) {
        const int percent = qRound(usage.used * 100.0 / double(usage.size));
        line = QStringLiteral("Context %1% (%2 of %3)")
                   .arg(percent)
                   .arg(tokenCount(usage.used), tokenCount(usage.size));
        if (percent >= kNearlyFullPercent)
            line += QStringLiteral(", nearly full");
    } else {
        line = QStringLiteral("Context %1 tokens").arg(tokenCount(usage.used));
    }
    if (usage.hasTotals) {
        line += QStringLiteral(" \u00B7 session %1 in").arg(tokenCount(usage.inputTokens));
        if (usage.cachedTokens > 0)
            line += QStringLiteral(" (%1 cached)").arg(tokenCount(usage.cachedTokens));
        line += QStringLiteral(", %1 out").arg(tokenCount(usage.outputTokens));
    }
    if (usage.hasCost)
        line += QStringLiteral(" \u00B7 ") + money(usage.cost, usage.currency);
    return line;
}

QList<AgentConfigOption> readConfigOptions(const QJsonArray &options)
{
    const auto choice = [](const QJsonObject &entry, const QString &group) {
        return AgentConfigChoice{entry.value(QLatin1String("value")).toString(),
                                 entry.value(QLatin1String("name")).toString(),
                                 entry.value(QLatin1String("description")).toString(), group};
    };
    QList<AgentConfigOption> result;
    for (const QJsonValue &value : options) {
        const QJsonObject source = value.toObject();
        AgentConfigOption option;
        option.type = source.value(QLatin1String("type")).toString();
        if (option.type != QLatin1String("select") && option.type != QLatin1String("boolean"))
            continue;
        option.id = source.value(QLatin1String("id")).toString();
        option.name = source.value(QLatin1String("name")).toString();
        option.description = source.value(QLatin1String("description")).toString();
        option.category = source.value(QLatin1String("category")).toString();
        option.currentValue = source.value(QLatin1String("currentValue")).toVariant();
        for (const QJsonValue &entryValue : source.value(QLatin1String("options")).toArray()) {
            const QJsonObject entry = entryValue.toObject();
            if (!entry.contains(QLatin1String("group"))) {
                option.choices.append(choice(entry, QString()));
                continue;
            }
            const QString group = entry.value(QLatin1String("name")).toString();
            for (const QJsonValue &member : entry.value(QLatin1String("options")).toArray())
                option.choices.append(choice(member.toObject(), group));
        }
        result.append(option);
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
