#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// Root types file: everything exposed across module boundaries lives here.

enum class TokenRole {
    Text,
    Keyword,
    Type,
    String,
    Number,
    Comment,
    Preprocessor,
    Function,
    Operator,
};

enum class FoldMode {
    None,
    Delimiter,
    Indent,
    // Markdown-style: a heading of level N owns everything up to the next
    // heading of level <= N.
    Heading,
};

using LanguageId = QString;

struct FoldPair {
    QString open;
    QString close;
};

// One tab as captured for session persistence. Buffer contents (text, or
// bytes for hex tabs) are carried only for modified tabs; clean tabs reload
// from disk on restore.
struct SessionTab {
    QString path;
    QString title;
    // Syntax name chosen for the tab; empty means detect from the path.
    QString language;
    // True when the tab shows the hex editor instead of the text editor.
    bool hex = false;
    QString text;
    QByteArray bytes;
    bool modified = false;
    // Text cursor position, or byte offset for hex tabs.
    int cursorPosition = 0;
};

struct Session {
    int activeIndex = 0;
    QList<SessionTab> tabs;
};

inline TokenRole tokenRoleFromName(const QString &name)
{
    if (name == QLatin1String("keyword")) return TokenRole::Keyword;
    if (name == QLatin1String("type")) return TokenRole::Type;
    if (name == QLatin1String("string")) return TokenRole::String;
    if (name == QLatin1String("number")) return TokenRole::Number;
    if (name == QLatin1String("comment")) return TokenRole::Comment;
    if (name == QLatin1String("preprocessor")) return TokenRole::Preprocessor;
    if (name == QLatin1String("function")) return TokenRole::Function;
    if (name == QLatin1String("operator")) return TokenRole::Operator;
    return TokenRole::Text;
}

inline FoldMode foldModeFromName(const QString &name)
{
    if (name == QLatin1String("delimiter")) return FoldMode::Delimiter;
    if (name == QLatin1String("indent")) return FoldMode::Indent;
    if (name == QLatin1String("heading")) return FoldMode::Heading;
    return FoldMode::None;
}
