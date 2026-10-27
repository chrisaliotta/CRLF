#include "syntax/LanguageDefinition.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QRegularExpression wordListRegex(const QJsonArray &words, QRegularExpression::PatternOptions options)
{
    if (words.isEmpty())
        return QRegularExpression();
    QStringList escaped;
    for (const QJsonValue &word : words)
        escaped.append(QRegularExpression::escape(word.toString()));
    return QRegularExpression(QStringLiteral("\\b(?:%1)\\b").arg(escaped.join(QLatin1Char('|'))),
                              options);
}

} // namespace

LanguageDefinition LanguageDefinition::fromJsonFile(const QString &path)
{
    QFile file(path);
    (void)file.open(QIODevice::ReadOnly);
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();

    LanguageDefinition def;
    def.name = root.value(QLatin1String("name")).toString();
    for (const QJsonValue &ext : root.value(QLatin1String("extensions")).toArray())
        def.extensions.append(ext.toString().toLower());

    def.lineComment = root.value(QLatin1String("lineComment")).toString();
    const QJsonObject blockComment = root.value(QLatin1String("blockComment")).toObject();
    def.blockCommentStart = blockComment.value(QLatin1String("start")).toString();
    def.blockCommentEnd = blockComment.value(QLatin1String("end")).toString();

    for (const QJsonValue &value : root.value(QLatin1String("strings")).toArray()) {
        const QJsonObject obj = value.toObject();
        StringRule rule;
        rule.start = obj.value(QLatin1String("start")).toString();
        rule.end = obj.value(QLatin1String("end")).toString();
        rule.escape = obj.value(QLatin1String("escape")).toString();
        rule.multiline = obj.value(QLatin1String("multiline")).toBool();
        def.strings.append(rule);
    }

    // Languages such as SQL treat SELECT, select and Select as the same word.
    const QRegularExpression::PatternOptions wordOptions =
        root.value(QLatin1String("ignoreCase")).toBool() ? QRegularExpression::CaseInsensitiveOption
                                                         : QRegularExpression::NoPatternOption;
    def.keywordRegex = wordListRegex(root.value(QLatin1String("keywords")).toArray(), wordOptions);
    def.typeRegex = wordListRegex(root.value(QLatin1String("types")).toArray(), wordOptions);

    for (const QJsonValue &value : root.value(QLatin1String("patterns")).toArray()) {
        const QJsonObject obj = value.toObject();
        PatternRule rule;
        rule.role = tokenRoleFromName(obj.value(QLatin1String("role")).toString());
        rule.regex = QRegularExpression(obj.value(QLatin1String("regex")).toString());
        def.patterns.append(rule);
    }

    for (const QJsonValue &value : root.value(QLatin1String("embedded")).toArray()) {
        const QJsonObject obj = value.toObject();
        EmbedRule rule;
        rule.start = QRegularExpression(obj.value(QLatin1String("start")).toString());
        rule.end = QRegularExpression(obj.value(QLatin1String("end")).toString());
        rule.languageCapture = obj.value(QLatin1String("languageCapture")).toInt(1);
        def.embedded.append(rule);
    }

    const QJsonObject folding = root.value(QLatin1String("folding")).toObject();
    def.foldMode = foldModeFromName(folding.value(QLatin1String("mode")).toString());
    for (const QJsonValue &value : folding.value(QLatin1String("pairs")).toArray()) {
        const QJsonObject obj = value.toObject();
        def.foldPairs.append({obj.value(QLatin1String("open")).toString(),
                              obj.value(QLatin1String("close")).toString()});
    }

    return def;
}
