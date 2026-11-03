#pragma once

#include <QList>
#include <QString>

// One span from a check script. See docs/diagnostics.md.
// `line` and `column` are 1-based. `endLine` and `endColumn` are optional;
// zero means the mark runs from `column` through the end of `line`.
// A present end is exclusive. `severity` is "error" or "warning".
struct Diagnostic
{
    int line = 0;
    int column = 0;
    int endLine = 0;
    int endColumn = 0;
    QString severity;
    QString message;

    bool warning() const { return severity == QLatin1String("warning"); }
};

// False when `bytes` is not a JSON array. Entries without a line and column
// are skipped. The checker writes this array to stdout.
bool diagnosticsFromJson(const QByteArray &bytes, QList<Diagnostic> *spans);
