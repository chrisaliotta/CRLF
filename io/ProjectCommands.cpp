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

void readCommands(Project &project, const QJsonArray &entries, const QString &directory)
{
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
}

void readGroups(Project &project, const QJsonArray &entries)
{
    for (const QJsonValue &value : entries) {
        const QJsonObject obj = value.toObject();
        const QString name = obj.value(QLatin1String("name")).toString().trimmed();
        if (name.isEmpty())
            continue;
        FileGroup group;
        group.name = name;
        group.icon = obj.value(QLatin1String("icon")).toString().trimmed();
        group.path = QDir::fromNativeSeparators(obj.value(QLatin1String("path")).toString().trimmed());
        if (group.path == QLatin1String(".") || group.path == QLatin1String("./"))
            group.path.clear();
        group.recursive = obj.value(QLatin1String("recursive")).toBool();
        group.showIfEmpty = obj.value(QLatin1String("showIfEmpty")).toBool();
        for (const QJsonValue &file : obj.value(QLatin1String("files")).toArray()) {
            const QString pattern = file.toString().trimmed();
            if (!pattern.isEmpty())
                group.files.append(pattern);
        }
        project.groups.append(group);
    }
}

IndentRule readIndentRule(const QJsonObject &obj, const IndentRule &base)
{
    IndentRule rule = base;
    rule.extensions.clear();
    const QString style = obj.value(QLatin1String("style")).toString();
    if (style == QLatin1String("spaces"))
        rule.spaces = true;
    else if (style == QLatin1String("tab"))
        rule.spaces = false;
    const int size = obj.value(QLatin1String("size")).toInt(base.size);
    rule.size = size < 1 ? base.size : size;
    for (const QJsonValue &extension : obj.value(QLatin1String("extensions")).toArray()) {
        const QString text = extension.toString().trimmed();
        if (!text.isEmpty())
            rule.extensions.append(text);
    }
    return rule;
}

void readIndent(IndentSettings &indent, const QJsonValue &value)
{
    if (!value.isObject())
        return;
    const QJsonObject obj = value.toObject();
    indent.standard = readIndentRule(obj, indent.standard);
    for (const QJsonValue &entry : obj.value(QLatin1String("types")).toArray()) {
        if (entry.isObject())
            indent.types.append(readIndentRule(entry.toObject(), indent.standard));
    }
}

void readHide(QStringList &hide, const QJsonArray &entries)
{
    for (const QJsonValue &entry : entries) {
        const QString pattern = entry.toString().trimmed();
        if (!pattern.isEmpty())
            hide.append(pattern);
    }
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
    if (parseError.error != QJsonParseError::NoError || (!document.isArray() && !document.isObject())) {
        project.error = QStringLiteral("Couldn't read .crlf");
        return project;
    }

    const QString directory = QFileInfo(project.filePath).absolutePath();
    if (document.isArray()) {
        readCommands(project, document.array(), directory);
        return project;
    }
    const QJsonObject root = document.object();
    readCommands(project, root.value(QLatin1String("commands")).toArray(), directory);
    readGroups(project, root.value(QLatin1String("groups")).toArray());
    readHide(project.hide, root.value(QLatin1String("hide")).toArray());
    readIndent(project.indent, root.value(QLatin1String("indent")));
    return project;
}

IndentRule indentForFileName(const IndentSettings &settings, const QString &fileName)
{
    const QFileInfo info(fileName);
    const QString suffix = info.suffix().toLower();
    const QString key = suffix.isEmpty() ? info.fileName().toLower() : suffix;
    for (const IndentRule &rule : settings.types) {
        for (const QString &extension : rule.extensions) {
            if (extension.compare(key, Qt::CaseInsensitive) == 0)
                return rule;
        }
    }
    return settings.standard;
}

bool saveProjectGroups(const QString &filePath, const QList<FileGroup> &groups)
{
    QJsonObject root;
    if (QFileInfo::exists(filePath)) {
        QFile existing(filePath);
        (void)existing.open(QIODevice::ReadOnly);
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(existing.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || (!document.isArray() && !document.isObject()))
            return false;
        if (document.isArray())
            root.insert(QLatin1String("commands"), document.array());
        else
            root = document.object();
    }

    QJsonArray stored;
    for (const FileGroup &group : groups) {
        QJsonObject obj;
        obj.insert(QLatin1String("name"), group.name);
        if (!group.icon.isEmpty())
            obj.insert(QLatin1String("icon"), group.icon);
        if (!group.path.isEmpty())
            obj.insert(QLatin1String("path"), group.path);
        if (group.recursive)
            obj.insert(QLatin1String("recursive"), true);
        if (group.showIfEmpty)
            obj.insert(QLatin1String("showIfEmpty"), true);
        QJsonArray files;
        for (const QString &pattern : group.files)
            files.append(pattern);
        obj.insert(QLatin1String("files"), files);
        stored.append(obj);
    }
    root.insert(QLatin1String("groups"), stored);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}
