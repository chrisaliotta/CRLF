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
    // ANSI colors 0–15 for terminal output. A theme file's `terminal` object
    // replaces these; a missing entry keeps the default for that theme's
    // background.
    QList<QColor> terminal;

private:
    QHash<int, QTextCharFormat> m_formats;
};
