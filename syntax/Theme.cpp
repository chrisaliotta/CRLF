#include "syntax/Theme.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

Theme Theme::fromJsonFile(const QString &path)
{
    QFile file(path);
    (void)file.open(QIODevice::ReadOnly);
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();

    Theme theme;
    theme.name = root.value(QLatin1String("name")).toString();

    const QJsonObject editor = root.value(QLatin1String("editor")).toObject();
    theme.background = QColor(editor.value(QLatin1String("background")).toString());
    theme.foreground = QColor(editor.value(QLatin1String("foreground")).toString());
    theme.currentLine = QColor(editor.value(QLatin1String("currentLine")).toString());
    theme.selection = QColor(editor.value(QLatin1String("selection")).toString());
    theme.gutterBackground = QColor(editor.value(QLatin1String("gutterBackground")).toString());
    theme.gutterForeground = QColor(editor.value(QLatin1String("gutterForeground")).toString());
    theme.codeBlockBackground = QColor(editor.value(QLatin1String("codeBlockBackground")).toString());

    const QJsonObject tokens = root.value(QLatin1String("tokens")).toObject();
    for (auto it = tokens.begin(); it != tokens.end(); ++it) {
        const QJsonObject spec = it.value().toObject();
        QTextCharFormat format;
        format.setForeground(QColor(spec.value(QLatin1String("color")).toString()));
        if (spec.value(QLatin1String("bold")).toBool())
            format.setFontWeight(QFont::Bold);
        if (spec.value(QLatin1String("italic")).toBool())
            format.setFontItalic(true);
        theme.m_formats.insert(int(tokenRoleFromName(it.key())), format);
    }
    return theme;
}
