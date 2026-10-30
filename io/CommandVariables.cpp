#include "io/CommandVariables.h"

#include <QHash>
#include <QRegularExpression>

QString expandCommand(const QString &command, const CommandVariables &vars)
{
    const QHash<QString, QString> values = {
        {QStringLiteral("file"), vars.file},
        {QStringLiteral("fileName"), vars.fileName},
        {QStringLiteral("fileBase"), vars.fileBase},
        {QStringLiteral("fileExt"), vars.fileExt},
        {QStringLiteral("fileDir"), vars.fileDir},
        {QStringLiteral("relativeFile"), vars.relativeFile},
        {QStringLiteral("line"), vars.line},
        {QStringLiteral("column"), vars.column},
        {QStringLiteral("selection"), vars.selection},
        {QStringLiteral("language"), vars.language},
        {QStringLiteral("project"), vars.project},
        {QStringLiteral("folder"), vars.folder},
        {QStringLiteral("cwd"), vars.cwd},
    };

    static const QRegularExpression token(QStringLiteral("\\$\\{([A-Za-z]+)\\}"));
    QString result;
    int cursor = 0;
    QRegularExpressionMatchIterator matches = token.globalMatch(command);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        result += QStringView(command).mid(cursor, int(match.capturedStart()) - cursor);
        const auto found = values.constFind(match.captured(1));
        result += found == values.cend() ? match.captured(0) : *found;
        cursor = int(match.capturedEnd());
    }
    result += QStringView(command).mid(cursor);
    return result;
}
