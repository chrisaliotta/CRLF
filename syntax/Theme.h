#pragma once

#include "Types.h"

#include <QColor>
#include <QHash>
#include <QList>
#include <QTextCharFormat>

// A color theme parsed from a JSON file in resources/themes/: editor chrome
// colors plus one QTextCharFormat per TokenRole.
class Theme
{
public:
    static Theme fromJsonFile(const QString &path);

    QTextCharFormat tokenFormat(TokenRole role) const { return m_formats.value(int(role)); }

    QString name;
    QColor background;
    QColor foreground;
    QColor currentLine;
    QColor selection;
    QColor gutterBackground;
    QColor gutterForeground;
    // Behind text inside embedded regions such as Markdown fenced code.
    QColor codeBlockBackground;
    // Wavy underlines and gutter marks for an external check. A theme that
    // omits these keeps the terminal red and yellow.
    QColor diagnosticError;
    QColor diagnosticWarning;
    // Source control: gutter bars, diff rows, and explorer badges. A theme
    // that omits these uses the terminal green, blue, red, and magenta.
    QColor gitAdded;
    QColor gitModified;
    QColor gitDeleted;
    QColor gitConflicted;
    // Completion list and the signature hint above the caret. A theme that
    // omits these gets a panel that stays readable on that editor background.
    QColor completionBackground;
    QColor completionForeground;
    QColor completionBorder;
    QColor completionActive;
    // ANSI colors 0–15 for terminal output. A theme file's `terminal` object
    // replaces these; a missing entry keeps the default for that theme's
    // background.
    QList<QColor> terminal;

private:
    QHash<int, QTextCharFormat> m_formats;
};
