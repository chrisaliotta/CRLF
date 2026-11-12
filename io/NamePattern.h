#pragma once

#include <QString>
#include <QStringList>

// The rule a .crlf file uses for `hide`, group `files`, and icon patterns: a
// pattern containing * or ? is a wildcard, anything else is an exact name.
// Case is ignored either way.
bool namePatternMatches(const QString &pattern, const QString &name);
bool nameMatchesAny(const QStringList &patterns, const QString &name);
