#pragma once

#include <QList>
#include <QMetaType>
#include <QString>

// One row from a completion session. Columns are 1-based. replaceEndColumn
// is exclusive. See docs/completion.md.
struct CompletionItem
{
    QString label;
    QString detail;
    QString insert;
    int replaceLine = 0;
    int replaceColumn = 0;
    int replaceEndColumn = 0;
};

Q_DECLARE_METATYPE(CompletionItem)
Q_DECLARE_METATYPE(QList<CompletionItem>)
