#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

// An edit the server wants made along with a completion, such as the import
// for the type that was chosen. Lines and columns are 1-based. The end is
// exclusive.
struct CompletionEdit
{
    int line = 0;
    int column = 0;
    int endLine = 0;
    int endColumn = 0;
    QString text;
};

// One row from a completion session. Columns are 1-based. replaceEndColumn
// is exclusive. See docs/completion.md.
struct CompletionItem
{
    QString label;
    QString detail;
    QString documentation;
    // The session's name for this row, sent back as kind "resolve".
    QString data;
    QString insert;
    // What the row is: "class", "enum", "enumMember", "method", "keyword", ...
    QString kind;
    // What the typed word is compared with. Empty means the label.
    QString filterText;
    // The row the server expects to be chosen, such as the enum a value is
    // being assigned to.
    bool preselect = false;
    QList<CompletionEdit> additionalEdits;
    int replaceLine = 0;
    int replaceColumn = 0;
    int replaceEndColumn = 0;
};

// One overload from signature help. parameter is the one being filled in.
struct SignatureHint
{
    QString label;
    QString parameter;
};

// Where one file stands with its language server. `state` is "starting",
// "loading", "ready", "unsupported", "missing", or "failed". `message` is
// the sentence for the status bar. `triggers` are the characters that server
// wants a completion asked after.
struct CompletionFileState
{
    QString state;
    QString server;
    QString message;
    QStringList triggers;
};

Q_DECLARE_METATYPE(CompletionItem)
Q_DECLARE_METATYPE(QList<CompletionItem>)
Q_DECLARE_METATYPE(SignatureHint)
Q_DECLARE_METATYPE(QList<SignatureHint>)
Q_DECLARE_METATYPE(CompletionFileState)
