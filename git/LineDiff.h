#pragma once

#include <QList>
#include <QStringList>

// One run of lines that differ between two texts. Starts are 0-based line
// indexes. An insertion has oldCount 0 and a deletion has newCount 0; a run
// with both counts set replaced those old lines with the new ones.
struct LineChange
{
    int oldStart = 0;
    int oldCount = 0;
    int newStart = 0;
    int newCount = 0;

    bool operator==(const LineChange &other) const
    {
        return oldStart == other.oldStart && oldCount == other.oldCount && newStart == other.newStart
               && newCount == other.newCount;
    }
};

// Myers' difference in linear space, so a large file does not need a trace
// table. Lines are compared exactly; callers split LF-normalized text.
QList<LineChange> diffLines(const QStringList &before, const QStringList &after);

// The lines of LF-normalized text. A trailing newline does not add an empty
// last line, so "a\n" and "a" are the same single line.
QStringList splitLines(const QString &text);
