#include "io/ProjectCommands.h"

#include "io/WslPath.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace {

QStringList menuPath(const QJsonValue &value)
{
    QStringList path;
    const auto append = [&path](const QString &title) {
        const QString trimmed = title.trimmed();
        if (!trimmed.isEmpty())
            path.append(trimmed);
    };
    if (value.isArray()) {
        for (const QJsonValue &part : value.toArray())
            append(part.toString());
        return path;
    }
    // A slash or backslash separates nested menus. An array entry is one
    // title, so a menu name can itself contain a slash.
    for (const QString &part : value.toString().split(QRegularExpression(QStringLiteral("[/\\\\]"))))
        append(part);
    return path;
}

QString findProjectFile(const QString &start)
{
    const QFileInfo info(start);
    QDir dir = info.isDir() ? QDir(info.absoluteFilePath()) : info.absoluteDir();
    while (true) {
        const QString candidate = dir.filePath(QStringLiteral(".crlf"));
        if (QFileInfo::exists(candidate))
            return QFileInfo(candidate).absoluteFilePath();
        // \\wsl.localhost is not a parent of the distro.
        if (WslPath::isDistroRoot(dir.absolutePath()) || !dir.cdUp())
            return {};
    }
}

} // namespace

Project projectFor(const QString &start)
{
    Project project;
    if (start.isEmpty())
        return project;
    project.filePath = findProjectFile(start);
    if (project.filePath.isEmpty())
        return project;

    QFile file(project.filePath);
    (void)file.open(QIODevice::ReadOnly);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const QJsonArray entries = document.array();
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        project.error = QStringLiteral("Couldn't read .crlf");
        return project;
    }

    const QString directory = QFileInfo(project.filePath).absolutePath();
    QSet<int> usedKeys;
    for (const QJsonValue &value : entries) {
        const QJsonObject obj = value.toObject();
        const QString name = obj.value(QLatin1String("name")).toString();
        const QString command = obj.value(QLatin1String("command")).toString();
        if (name.isEmpty() || command.isEmpty())
            continue;
        bool duplicate = false;
        for (const ProjectCommand &existing : project.commands)
            duplicate = duplicate || existing.name == name;
        if (duplicate)
            continue;
        QString workingDirectory = directory;
        const QString cwd = obj.value(QLatin1String("cwd")).toString();
        if (!cwd.isEmpty())
            workingDirectory = QDir(directory).filePath(cwd);

        QKeySequence key;
        const QString keyText = obj.value(QLatin1String("key")).toString();
        if (!keyText.isEmpty()) {
            const QKeySequence parsed(keyText);
            const bool functionKey = parsed.count() == 1
                                     && parsed[0].keyboardModifiers() == Qt::NoModifier
                                     && parsed[0].key() >= Qt::Key_F1
                                     && parsed[0].key() <= Qt::Key_F35
                                     && !usedKeys.contains(parsed[0].toCombined());
            if (functionKey) {
                key = parsed;
                usedKeys.insert(parsed[0].toCombined());
            }
        }
        project.commands.append(
            {name, command, QDir(workingDirectory).absolutePath(), menuPath(obj.value(QLatin1String("menu"))), key});
    }
    return project;
}
