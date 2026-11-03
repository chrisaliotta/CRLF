#include "editor/Diagnostic.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

bool diagnosticsFromJson(const QByteArray &bytes, QList<Diagnostic> *spans)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray())
        return false;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject obj = value.toObject();
        Diagnostic span;
        span.line = obj.value(QLatin1String("line")).toInt();
        span.column = obj.value(QLatin1String("column")).toInt();
        if (span.line < 1 || span.column < 1)
            continue;
        span.endLine = obj.value(QLatin1String("endLine")).toInt();
        span.endColumn = obj.value(QLatin1String("endColumn")).toInt();
        span.severity = obj.value(QLatin1String("severity")).toString();
        span.message = obj.value(QLatin1String("message")).toString();
        spans->append(span);
    }
    return true;
}
