#pragma once

#include "Types.h"

#include <QColor>
#include <QHash>
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

private:
    QHash<int, QTextCharFormat> m_formats;
};
