#include "io/CommandVariables.h"

#include "io/WslPath.h"

#include <QDir>
#include <QHash>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

QString linuxPath(const QString &windowsPath)
{
    if (windowsPath.isEmpty())
        return windowsPath;
    const WslPath::LinuxPath converted = WslPath::toLinux(windowsPath);
    if (converted.valid())
        return converted.path;
    // The file lives on a Windows drive. The distro sees it under /mnt.
    const QString clean = QDir::fromNativeSeparators(windowsPath);
    if (clean.size() >= 3 && clean.at(1) == QLatin1Char(':'))
        return QStringLiteral("/mnt/") + clean.at(0).toLower() + clean.mid(2);
    return windowsPath;
}

} // namespace

QString expandCommand(const QString &command, const CommandVariables &vars)
{
    const QHash<QString, QString> values = {
        {QStringLiteral("file"), vars.file},
        {QStringLiteral("fileName"), vars.fileName},
        {QStringLiteral("fileBase"), vars.fileBase},
        {QStringLiteral("fileExt"), vars.fileExt},
        {QStringLiteral("fileDir"), vars.fileDir},
        {QStringLiteral("relativeFile"), vars.relativeFile},
        {QStringLiteral("line"), vars.line},
        {QStringLiteral("column"), vars.column},
        {QStringLiteral("selection"), vars.selection},
        {QStringLiteral("buffer"), vars.buffer},
        {QStringLiteral("terminal"), vars.terminal},
        {QStringLiteral("language"), vars.language},
        {QStringLiteral("project"), vars.project},
        {QStringLiteral("folder"), vars.folder},
        {QStringLiteral("cwd"), vars.cwd},
        {QStringLiteral("programDir"), vars.programDir},
        {QStringLiteral("appDir"), vars.appDir},
    };

    static const QRegularExpression token(QStringLiteral("\\$\\{([A-Za-z]+)\\}"));
    QString result;
    int cursor = 0;
    QRegularExpressionMatchIterator matches = token.globalMatch(command);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        result += QStringView(command).mid(cursor, int(match.capturedStart()) - cursor);
        const auto found = values.constFind(match.captured(1));
        result += found == values.cend() ? match.captured(0) : *found;
        cursor = int(match.capturedEnd());
    }
    result += QStringView(command).mid(cursor);
    return result;
}

CommandVariables launchVariables(CommandVariables vars, const QString &workingDirectory)
{
    vars.cwd = workingDirectory;
    const WslPath::LinuxPath inDistro = WslPath::toLinux(workingDirectory);
    if (!inDistro.valid())
        return vars;
    for (QString *path : {&vars.file, &vars.fileDir, &vars.project, &vars.folder, &vars.buffer, &vars.terminal,
                          &vars.programDir, &vars.appDir})
        *path = linuxPath(*path);
    vars.cwd = inDistro.path;
    return vars;
}

QString commandFor(const QJsonValue &command, const QString &workingDirectory)
{
    if (!command.isObject())
        return command.toString();
    const bool distro = WslPath::toLinux(workingDirectory).valid();
    return command.toObject().value(distro ? QLatin1String("wsl") : QLatin1String("windows")).toString();
}
