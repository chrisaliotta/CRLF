#include "app/OpenWith.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace {

void setDefault(QSettings &key, const QString &value)
{
    // "." is the registry key's default value.
    key.setValue(QStringLiteral("."), value);
}

} // namespace

void registerOpenWith(const QStringList &extensions)
{
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString command = QStringLiteral("\"%1\" \"%2\"").arg(exe, QStringLiteral("%1"));
    const QString appKey = QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\Applications\\")
                           + QFileInfo(exe).fileName();

    QSettings app(appKey, QSettings::NativeFormat);
    app.setValue(QStringLiteral("FriendlyAppName"), QCoreApplication::applicationName());

    QSettings open(appKey + QStringLiteral("\\shell\\open\\command"), QSettings::NativeFormat);
    setDefault(open, command);

    // Extensions listed here are offered in Explorer's Open with chooser.
    QSettings types(appKey + QStringLiteral("\\SupportedTypes"), QSettings::NativeFormat);
    for (QString extension : extensions) {
        if (!extension.startsWith(QLatin1Char('.')))
            extension.prepend(QLatin1Char('.'));
        types.setValue(extension, QString());
    }

    const QString verb = QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\*\\shell\\")
                         + QCoreApplication::applicationName();
    QSettings menu(verb, QSettings::NativeFormat);
    setDefault(menu, QStringLiteral("Open with %1").arg(QCoreApplication::applicationName()));
    menu.setValue(QStringLiteral("Icon"), QStringLiteral("\"%1\",0").arg(exe));
    menu.setValue(QStringLiteral("AppliesTo"), QStringLiteral("NOT System.Kind:folder"));

    QSettings menuCommand(verb + QStringLiteral("\\command"), QSettings::NativeFormat);
    setDefault(menuCommand, command);
}
