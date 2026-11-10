#include "syntax/Theme.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QList<QColor> paletteFrom(const char *const (&hex)[16])
{
    QList<QColor> colors;
    for (const char *entry : hex)
        colors.append(QColor(QLatin1String(entry)));
    return colors;
}

// Readable on the Dark editor background. Bright colors follow that theme's
// token colors so command output sits next to the editor.
QList<QColor> darkTerminalPalette()
{
    static const char *const hex[16] = {
        "#000000", "#f14c4c", "#6a9955", "#dcdcaa",
        "#569cd6", "#c586c0", "#4ec9b0", "#d4d4d4",
        "#808080", "#f44747", "#b5cea8", "#d7ba7d",
        "#9cdcfe", "#d670d6", "#29b8db", "#ffffff",
    };
    return paletteFrom(hex);
}

// Dark enough to stay visible on the Light editor background.
QList<QColor> lightTerminalPalette()
{
    static const char *const hex[16] = {
        "#1f1f1f", "#a31515", "#098658", "#795e26",
        "#0000ff", "#af00db", "#267f99", "#5a5a5a",
        "#767676", "#cd3131", "#107c10", "#949800",
        "#0451a5", "#bc05bc", "#0598bc", "#1f1f1f",
    };
    return paletteFrom(hex);
}

const char *const kTerminalKeys[16] = {
    "black", "red", "green", "yellow", "blue", "magenta", "cyan", "white",
    "brightBlack", "brightRed", "brightGreen", "brightYellow",
    "brightBlue", "brightMagenta", "brightCyan", "brightWhite",
};

} // namespace

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

    const QList<QColor> defaults = theme.background.lightness() >= 128
                                       ? lightTerminalPalette()
                                       : darkTerminalPalette();
    const QJsonObject terminal = root.value(QLatin1String("terminal")).toObject();
    for (int i = 0; i < 16; ++i) {
        const QColor named(terminal.value(QLatin1String(kTerminalKeys[i])).toString());
        theme.terminal.append(named.isValid() ? named : defaults.at(i));
    }

    const QColor diagnosticError(editor.value(QLatin1String("diagnosticError")).toString());
    const QColor diagnosticWarning(editor.value(QLatin1String("diagnosticWarning")).toString());
    theme.diagnosticError = diagnosticError.isValid() ? diagnosticError : theme.terminal.at(1);
    theme.diagnosticWarning = diagnosticWarning.isValid() ? diagnosticWarning : theme.terminal.at(3);

    const bool light = theme.background.lightness() >= 128;
    const QColor completionBackground(editor.value(QLatin1String("completionBackground")).toString());
    const QColor completionForeground(editor.value(QLatin1String("completionForeground")).toString());
    const QColor completionBorder(editor.value(QLatin1String("completionBorder")).toString());
    const QColor completionActive(editor.value(QLatin1String("completionActive")).toString());
    theme.completionBackground = completionBackground.isValid()
                                     ? completionBackground
                                     : (light ? QColor(QStringLiteral("#f3f3f3"))
                                              : theme.background.lighter(180));
    theme.completionForeground = completionForeground.isValid() ? completionForeground : theme.foreground;
    theme.completionBorder = completionBorder.isValid()
                                 ? completionBorder
                                 : QColor(light ? QStringLiteral("#8a8a8a") : QStringLiteral("#9a9a9a"));
    theme.completionActive = completionActive.isValid()
                                 ? completionActive
                                 : QColor(light ? QStringLiteral("#0451a5") : QStringLiteral("#9cdcfe"));

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
