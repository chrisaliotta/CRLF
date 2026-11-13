#include "io/ProjectCommands.h"

#include "io/CommandVariables.h"
#include "io/ProjectMerge.h"
#include "io/WslPath.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

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
        QString workingDirectory = directory;
        const QString cwd = obj.value(QLatin1String("cwd")).toString();
        if (!cwd.isEmpty())
            workingDirectory = QDir(directory).filePath(cwd);
        workingDirectory = QDir(workingDirectory).absolutePath();
        const QString command = commandFor(obj.value(QLatin1String("command")), workingDirectory);
        if (name.isEmpty() || command.isEmpty())
            continue;
        bool duplicate = false;
        for (const ProjectCommand &existing : project.commands)
            duplicate = duplicate || existing.name == name;
        if (duplicate)
            continue;

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
        const bool outputToEditor =
            obj.value(QLatin1String("output")).toString() == QLatin1String("editor");
        project.commands.append({name, command, workingDirectory,
                                 menuPath(obj.value(QLatin1String("menu"))), key, outputToEditor});
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
    rule.keep = obj.value(QLatin1String("keep")).toBool(base.keep);
    for (const QJsonValue &extension : obj.value(QLatin1String("extensions")).toArray()) {
        const QString text = extension.toString().trimmed();
        if (!text.isEmpty())
            rule.extensions.append(text);
    }
    return rule;
}

// `command`, `cwd`, and `watch`, shared by the services and the agents.
void readProcess(ServiceSettings &service, const QJsonObject &obj, const QString &directory)
{
    const QString cwd = obj.value(QLatin1String("cwd")).toString();
    if (!cwd.isEmpty())
        service.workingDirectory = QDir(directory).filePath(cwd);
    service.workingDirectory = QDir(service.workingDirectory).absolutePath();
    service.command = commandFor(obj.value(QLatin1String("command")), service.workingDirectory);
    for (const QJsonValue &path : obj.value(QLatin1String("watch")).toArray())
        service.watch.append(QDir(service.workingDirectory).absoluteFilePath(path.toString()));
}

void readService(ServiceSettings &service, const QJsonValue &value, const QString &directory)
{
    service.workingDirectory = QDir(directory).absolutePath();
    if (value.isBool()) {
        service.enabled = value.toBool();
        return;
    }
    if (!value.isObject())
        return;
    const QJsonObject obj = value.toObject();
    service.enabled = obj.value(QLatin1String("enabled")).toBool(false);
    readProcess(service, obj, directory);
}

void readAgents(Project &project, const QJsonArray &entries, const QString &directory)
{
    for (const QJsonValue &value : entries) {
        const QJsonObject obj = value.toObject();
        AgentSettings agent;
        agent.name = obj.value(QLatin1String("name")).toString().trimmed();
        agent.process.workingDirectory = QDir(directory).absolutePath();
        readProcess(agent.process, obj, directory);
        if (agent.name.isEmpty() || agent.process.command.isEmpty())
            continue;
        bool duplicate = false;
        for (const AgentSettings &existing : project.agents)
            duplicate = duplicate || existing.name == agent.name;
        if (duplicate)
            continue;
        const QJsonObject permissions = obj.value(QLatin1String("permissions")).toObject();
        const auto access = [&permissions](const char *key, AgentAccess fallback) {
            return accessNamed(permissions.value(QLatin1String(key)).toString(), fallback);
        };
        agent.permissions.read = access("read", agent.permissions.read);
        agent.permissions.write = access("write", agent.permissions.write);
        agent.permissions.terminal = access("terminal", agent.permissions.terminal);
        project.agents.append(agent);
    }
}

void readGit(GitSettings &git, const QJsonValue &value)
{
    if (value.isBool()) {
        git.enabled = value.toBool();
        return;
    }
    if (!value.isObject())
        return;
    const QJsonObject obj = value.toObject();
    git.enabled = obj.value(QLatin1String("enabled")).toBool(true);
    git.depth = qMax(0, obj.value(QLatin1String("depth")).toInt(0));
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

void readIcons(ProjectIcons &icons, const QJsonValue &value)
{
    if (!value.isObject())
        return;
    const QJsonObject obj = value.toObject();
    icons.theme = obj.value(QLatin1String("theme")).toString().trimmed();
    for (const QJsonValue &entry : obj.value(QLatin1String("rules")).toArray()) {
        const QJsonObject rule = entry.toObject();
        IconRule item;
        item.kind = rule.value(QLatin1String("kind")).toString().trimmed();
        if (item.kind == QLatin1String("directory"))
            item.kind = QStringLiteral("folder");
        const QString pattern = rule.value(QLatin1String("pattern")).toString().trimmed();
        item.pattern = pattern.isEmpty() ? rule.value(QLatin1String("key")).toString().trimmed() : pattern;
        item.from = rule.value(QLatin1String("from")).toString().trimmed();
        item.icon = rule.value(QLatin1String("icon")).toString().trimmed();
        item.iconOpen = rule.value(QLatin1String("iconOpen")).toString().trimmed();
        if (item.kind.isEmpty() || item.pattern.isEmpty() || item.from.isEmpty() || item.icon.isEmpty())
            continue;
        icons.rules.append(item);
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

QJsonObject groupObject(const FileGroup &group)
{
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
    return obj;
}

QJsonObject iconRuleObject(const IconRule &rule)
{
    QJsonObject obj;
    obj.insert(QLatin1String("kind"), rule.kind);
    obj.insert(QLatin1String("pattern"), rule.pattern);
    obj.insert(QLatin1String("from"), rule.from);
    obj.insert(QLatin1String("icon"), rule.icon);
    if (!rule.iconOpen.isEmpty())
        obj.insert(QLatin1String("iconOpen"), rule.iconOpen);
    return obj;
}

// The nearest folder, walking up from `start`, that holds a settings file.
// The application folder is not a project: its file is the application's.
QString findProjectDirectory(const QString &start, const QString &applicationFolder)
{
    if (start.isEmpty())
        return {};
    const QFileInfo info(start);
    QDir dir = info.isDir() ? QDir(info.absoluteFilePath()) : info.absoluteDir();
    while (true) {
        if (dir != QDir(applicationFolder) && !settingsFiles(dir.absolutePath()).isEmpty())
            return dir.absolutePath();
        // \\wsl.localhost is not a parent of the distro.
        if (WslPath::isDistroRoot(dir.absolutePath()) || !dir.cdUp())
            return {};
    }
}

// `.crlf` used to be the one project file. The name now belongs to the
// project's folder, which cannot be created while a file holds it.
void renameLegacyFile(Project &project)
{
    const QDir dir(project.directory);
    const QString legacy = dir.filePath(QLatin1String(kProjectFolder));
    if (!QFileInfo(legacy).isFile())
        return;
    QString target = dir.filePath(QLatin1String(kWorkspaceFile));
    if (QFileInfo::exists(target))
        target = dir.filePath(QStringLiteral("legacy.crlf"));
    // A read-only folder refuses. The file is then read under its old name.
    if (!QFile::rename(legacy, target))
        return;
    project.renamedFrom = legacy;
    project.renamedTo = target;
}

// Reads one settings file. An array of commands becomes an object. A
// missing file yields an empty object. Invalid JSON returns false.
bool loadProjectObject(const QString &filePath, QJsonObject *root)
{
    if (!QFileInfo::exists(filePath))
        return true;
    QFile existing(filePath);
    (void)existing.open(QIODevice::ReadOnly);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(existing.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || (!document.isArray() && !document.isObject()))
        return false;
    if (document.isArray())
        root->insert(QLatin1String("commands"), document.array());
    else
        *root = document.object();
    return true;
}

// What every version before the configured application file shipped as the
// defaults: an empty command list and nothing else.
bool holdsOldDefault(const QString &filePath)
{
    QJsonObject root;
    return QFileInfo(filePath).isFile() && loadProjectObject(filePath, &root) && root.size() == 1
           && root.value(QLatin1String("commands")).isArray()
           && root.value(QLatin1String("commands")).toArray().isEmpty();
}

bool writeProjectObject(const QString &filePath, const QJsonObject &root)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

struct SettingsFile
{
    QString path;
    QJsonObject root;
    // False for a file that is not JSON. It applies nothing.
    bool readable = false;
};

QList<SettingsFile> loadSettings(const QStringList &paths)
{
    QList<SettingsFile> files;
    for (const QString &path : paths) {
        SettingsFile file;
        file.path = path;
        file.readable = loadProjectObject(path, &file.root);
        files.append(file);
    }
    return files;
}

QList<QJsonObject> readableObjects(const QList<SettingsFile> &files)
{
    QList<QJsonObject> objects;
    for (const SettingsFile &file : files) {
        if (file.readable)
            objects.append(file.root);
    }
    return objects;
}

// What the workspace files in `files` take from the application file. A save
// leaves these out, so an inherited entry is not copied into the workspace.
QJsonObject inheritedBy(const QList<SettingsFile> &files, const QString &applicationFile)
{
    QJsonObject application;
    loadProjectObject(applicationFile, &application);
    return inheritedSettings(readableObjects(files), application);
}

// Puts `value` under `key` in the workspace file that owns the key: the
// first one that has it. The later files lose the key, so one file owns it
// afterwards. With no owner, workspace.crlf in `directory` becomes it. An
// undefined `value` removes the key.
bool writeOwnedKey(const QString &directory, const QList<SettingsFile> &files, const QString &key,
                   const QJsonValue &value)
{
    QString owner;
    bool written = true;
    for (const SettingsFile &file : files) {
        if (!file.readable || !file.root.contains(key))
            continue;
        if (owner.isEmpty()) {
            owner = file.path;
            continue;
        }
        QJsonObject root = file.root;
        root.remove(key);
        written = writeProjectObject(file.path, root) && written;
    }
    if (owner.isEmpty()) {
        if (value.isUndefined())
            return written;
        owner = QDir(directory).filePath(QLatin1String(kWorkspaceFile));
    }
    QJsonObject root;
    if (!loadProjectObject(owner, &root))
        return false;
    // The folder's first settings file would otherwise replace the
    // application's settings, which applied alone until now.
    if (files.isEmpty())
        root.insert(QLatin1String("inherit"), true);
    if (value.isUndefined())
        root.remove(key);
    else
        root.insert(key, value);
    return writeProjectObject(owner, root) && written;
}

} // namespace

QString applicationFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QStringList settingsFiles(const QString &directory)
{
    const QDir dir(directory);
    QStringList names = dir.entryList({QStringLiteral("*.crlf")}, QDir::Files | QDir::Hidden);
    // A workspace's copy of the application settings is its base: every
    // other file in the folder is read before it, and so wins over it.
    const auto isApplication = [](const QString &name) {
        return name.compare(QLatin1String(kApplicationFile), Qt::CaseInsensitive) == 0;
    };
    std::sort(names.begin(), names.end(), [&isApplication](const QString &a, const QString &b) {
        if (isApplication(a) != isApplication(b))
            return isApplication(b);
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    QStringList paths;
    for (const QString &name : std::as_const(names))
        paths.append(dir.absoluteFilePath(name));
    return paths;
}

QString activeApplicationFile(const QString &applicationFolder, const QString &name)
{
    const QDir folder(applicationFolder);
    const bool named = !name.isEmpty() && QFileInfo(folder.filePath(name)).isFile();
    return folder.filePath(named ? name : QString::fromLatin1(kApplicationFile));
}

// A name in the workspace merge is that file's entry: workspace files are
// merged first, and the first definition of a name wins. syntax and
// autocomplete are one value each, so the key's presence is enough.
static void markWorkspaceOrigin(Project &project, const QJsonObject &workspace)
{
    QSet<QString> commands;
    for (const QJsonValue &value : workspace.value(QLatin1String("commands")).toArray())
        commands.insert(value.toObject().value(QLatin1String("name")).toString());
    for (ProjectCommand &command : project.commands)
        command.workspaceOrigin = commands.contains(command.name);

    QSet<QString> agents;
    for (const QJsonValue &value : workspace.value(QLatin1String("agents")).toArray())
        agents.insert(value.toObject().value(QLatin1String("name")).toString());
    for (AgentSettings &agent : project.agents)
        agent.workspaceOrigin = agents.contains(agent.name);

    project.syntax.workspaceOrigin = workspace.contains(QLatin1String("syntax"));
    project.autocomplete.workspaceOrigin = workspace.contains(QLatin1String("autocomplete"));
}

QString seedApplicationFolder(const QString &applicationFolder, const QString &programFolder)
{
    const QDir folder(applicationFolder);
    const QDir program(programFolder);
    // An application.crlf that holds only the old default was put there by
    // an earlier version, which copied the defaults file beside the program
    // under that name. Nobody wrote it, and left in place it would keep the
    // configured file from ever arriving, so it is treated as absent.
    const QString application = folder.filePath(QLatin1String(kApplicationFile));
    if (holdsOldDefault(application))
        QFile::remove(application);
    const bool firstRun = !QFileInfo::exists(application);
    for (const char *name : {"scripts", "keys", "rules"})
        folder.mkpath(QLatin1String(kProjectFolder) + QLatin1Char('/') + QLatin1String(name));
    // QFile::copy leaves a file that is already there as it is.
    for (const char *name : {kApplicationFile, kBlankFile})
        QFile::copy(program.filePath(QLatin1String(name)), folder.filePath(QLatin1String(name)));
    // The price list the shipped agents read is the user's to keep current.
    QFile::copy(program.filePath(QLatin1String(kPricesFile)),
                folder.filePath(QLatin1String(kProjectFolder) + QLatin1Char('/') + QLatin1String(kPricesFile)));

    // Before the application folder, the user's defaults were the file named
    // .crlf beside the program. An upgrade leaves that file there. One that
    // was never edited still holds what shipped then, and one that sets
    // nothing has nothing to keep; neither is worth having in place of the
    // configured file.
    const QString previous = program.filePath(QLatin1String(kProjectFolder));
    if (!firstRun || !QFileInfo(previous).isFile())
        return {};
    QJsonObject settings;
    loadProjectObject(previous, &settings);
    if (settings.isEmpty() || holdsOldDefault(previous))
        return {};
    QFile::copy(previous, folder.filePath(QLatin1String(kPreviousFile)));
    return QString::fromLatin1(kPreviousFile);
}

Project projectFor(const QString &start, const QString &applicationFile)
{
    const QString applicationFolder = QFileInfo(applicationFile).absolutePath();
    Project project;
    project.directory = findProjectDirectory(start, applicationFolder);
    QList<SettingsFile> files;
    if (!project.directory.isEmpty()) {
        renameLegacyFile(project);
        files = loadSettings(settingsFiles(project.directory));
    }
    const QList<QJsonObject> workspace = readableObjects(files);
    QJsonObject application;
    if (project.directory.isEmpty() || inheritsApplication(workspace)) {
        files += loadSettings({applicationFile});
        application = files.last().root;
    }

    QStringList unreadable;
    for (const SettingsFile &file : std::as_const(files)) {
        project.files.append(file.path);
        if (!file.readable)
            unreadable.append(QFileInfo(file.path).fileName());
    }
    if (!unreadable.isEmpty())
        project.error = QStringLiteral("Couldn't read ") + unreadable.join(QStringLiteral(", "));

    // Every relative path in the merged settings resolves against one
    // folder, whichever file the entry came from.
    QString directory = project.directory;
    if (directory.isEmpty())
        directory = QFileInfo(start).isDir() ? start : applicationFolder;

    const QJsonObject root = mergeProjects(workspace, application);
    readCommands(project, root.value(QLatin1String("commands")).toArray(), directory);
    readGroups(project, root.value(QLatin1String("groups")).toArray());
    readHide(project.hide, root.value(QLatin1String("hide")).toArray());
    readIcons(project.icons, root.value(QLatin1String("icons")));
    readIndent(project.indent, root.value(QLatin1String("indent")));
    project.navigation = root.value(QLatin1String("navigation")).toBool(true);
    project.externalNavigation = root.value(QLatin1String("externalNavigation")).toBool(true);
    readService(project.syntax, root.value(QLatin1String("syntax")), directory);
    readService(project.autocomplete, root.value(QLatin1String("autocomplete")), directory);
    readAgents(project, root.value(QLatin1String("agents")).toArray(), directory);
    project.unlimitedLog = root.value(QLatin1String("unlimitedLog")).toBool(false);
    readGit(project.git, root.value(QLatin1String("git")));
    markWorkspaceOrigin(project, mergeProjects(workspace, {}));
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

bool saveProjectGroups(const QString &directory, const QList<FileGroup> &groups, const QString &applicationFile)
{
    const QList<SettingsFile> files = loadSettings(settingsFiles(directory));
    Project inherited;
    readGroups(inherited, inheritedBy(files, applicationFile).value(QLatin1String("groups")).toArray());
    QJsonArray unchanged;
    for (const FileGroup &group : std::as_const(inherited.groups))
        unchanged.append(groupObject(group));

    QJsonArray stored;
    for (const FileGroup &group : groups) {
        const QJsonObject obj = groupObject(group);
        if (!unchanged.contains(obj))
            stored.append(obj);
    }
    return writeOwnedKey(directory, files, QStringLiteral("groups"),
                         stored.isEmpty() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(stored));
}

bool saveProjectIcons(const QString &directory, const ProjectIcons &icons, const QString &applicationFile)
{
    const QList<SettingsFile> files = loadSettings(settingsFiles(directory));
    ProjectIcons inherited;
    readIcons(inherited, inheritedBy(files, applicationFile).value(QLatin1String("icons")));
    QJsonArray unchanged;
    for (const IconRule &rule : std::as_const(inherited.rules))
        unchanged.append(iconRuleObject(rule));
    // A theme a workspace file names is the workspace's, even when the
    // application names the same one.
    bool workspaceTheme = false;
    for (const SettingsFile &file : files)
        workspaceTheme = workspaceTheme
                         || file.root.value(QLatin1String("icons")).toObject().contains(QLatin1String("theme"));

    QJsonObject stored;
    if (!icons.theme.isEmpty() && (workspaceTheme || icons.theme != inherited.theme))
        stored.insert(QLatin1String("theme"), icons.theme);
    QJsonArray rules;
    for (const IconRule &rule : icons.rules) {
        const QJsonObject obj = iconRuleObject(rule);
        // Each inherited rule accounts for one rule in the list.
        int match = -1;
        for (int i = 0; i < unchanged.size() && match < 0; ++i) {
            if (unchanged.at(i) == obj)
                match = i;
        }
        if (match < 0)
            rules.append(obj);
        else
            unchanged.removeAt(match);
    }
    if (!rules.isEmpty())
        stored.insert(QLatin1String("rules"), rules);
    return writeOwnedKey(directory, files, QStringLiteral("icons"),
                         stored.isEmpty() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(stored));
}
