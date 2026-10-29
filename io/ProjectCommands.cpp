#include "io/ProjectCommands.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QString findProjectFile(const QString &start)
{
    const QFileInfo info(start);
    QDir dir = info.isDir() ? QDir(info.absoluteFilePath()) : info.absoluteDir();
    while (true) {
        const QString candidate = dir.filePath(QStringLiteral(".crlf"));
        if (QFileInfo::exists(candidate))
            return QFileInfo(candidate).absoluteFilePath();
        if (!dir.cdUp())
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
        project.commands.append({name, command, QDir(workingDirectory).absolutePath()});
    }
    return project;
}
