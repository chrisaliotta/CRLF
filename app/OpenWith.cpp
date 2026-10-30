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
    const QString appName = QCoreApplication::applicationName();
    const QString appKey = QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\Applications\\")
                           + QFileInfo(exe).fileName();
    const QString label = QStringLiteral("Open with %1").arg(appName);
    const QString icon = QStringLiteral("\"%1\",0").arg(exe);
    const QString appliesTo = QStringLiteral("NOT System.Kind:folder");

    QStringList want;
    want.reserve(extensions.size());
    for (QString extension : extensions) {
        if (!extension.startsWith(QLatin1Char('.')))
            extension.prepend(QLatin1Char('.'));
        want.append(extension.toLower());
    }
    want.sort();
    want.removeDuplicates();

    const QString verb = QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\*\\shell\\") + appName;
    QSettings app(appKey, QSettings::NativeFormat);
    QSettings open(appKey + QStringLiteral("\\shell\\open\\command"), QSettings::NativeFormat);
    QSettings types(appKey + QStringLiteral("\\SupportedTypes"), QSettings::NativeFormat);
    QSettings menu(verb, QSettings::NativeFormat);
    QSettings menuCommand(verb + QStringLiteral("\\command"), QSettings::NativeFormat);

    QStringList have;
    for (const QString &key : types.childKeys())
        have.append(key.toLower());
    have.sort();
    const bool current = app.value(QStringLiteral("FriendlyAppName")).toString() == appName
                         && open.value(QStringLiteral(".")).toString() == command
                         && menu.value(QStringLiteral(".")).toString() == label
                         && menu.value(QStringLiteral("Icon")).toString() == icon
                         && menu.value(QStringLiteral("AppliesTo")).toString() == appliesTo
                         && menuCommand.value(QStringLiteral(".")).toString() == command
                         && have == want;
    if (current)
        return;

    app.setValue(QStringLiteral("FriendlyAppName"), appName);
    setDefault(open, command);
    const QStringList stale = types.childKeys();
    for (const QString &key : stale)
        types.remove(key);
    for (const QString &extension : want)
        types.setValue(extension, QString());

    setDefault(menu, label);
    menu.setValue(QStringLiteral("Icon"), icon);
    menu.setValue(QStringLiteral("AppliesTo"), appliesTo);
    setDefault(menuCommand, command);
}
