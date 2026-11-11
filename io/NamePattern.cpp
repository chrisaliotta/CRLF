#include "io/NamePattern.h"

#include <QRegularExpression>

bool namePatternMatches(const QString &pattern, const QString &name)
{
    if (!pattern.contains(QLatin1Char('*')) && !pattern.contains(QLatin1Char('?')))
        return name.compare(pattern, Qt::CaseInsensitive) == 0;
    const QRegularExpression expression(QRegularExpression::wildcardToRegularExpression(pattern),
                                        QRegularExpression::CaseInsensitiveOption);
    return expression.match(name).hasMatch();
}

bool nameMatchesAny(const QStringList &patterns, const QString &name)
{
    for (const QString &pattern : patterns) {
        if (namePatternMatches(pattern, name))
            return true;
    }
    return false;
}
