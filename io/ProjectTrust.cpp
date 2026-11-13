#include "io/ProjectTrust.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace {

constexpr auto kTrustSetting = "workspaceTrust";

#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif

QString clean(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

QString trustKey(const QString &root)
{
    if (root.isEmpty())
        return {};
    // canonicalFilePath opens the directory to resolve links. On a
    // \\wsl.localhost path that call waits for the distro, so the key is
    // the cleaned absolute path instead.
    return clean(QFileInfo(root).absoluteFilePath());
}

bool insideRoot(const QString &root, const QString &path)
{
    const QString base = clean(root);
    const QString full = clean(path);
    if (full.compare(base, kPathCase) == 0)
        return true;
    return full.startsWith(base + QLatin1Char('/'), kPathCase);
}

QString relativeTo(const QString &root, const QString &path)
{
    const QString base = clean(root);
    const QString full = clean(path);
    if (full.compare(base, kPathCase) == 0)
        return QStringLiteral(".");
    const QString prefix = base + QLatin1Char('/');
    if (full.startsWith(prefix, kPathCase))
        return full.mid(prefix.size());
    return full;
}

bool excludedRelative(const QString &relative)
{
    const QStringList parts = relative.split(QLatin1Char('/'));
    for (const QString &part : parts) {
        if (part.compare(QLatin1String("__pycache__"), kPathCase) == 0)
            return true;
    }
    if (parts.size() >= 2 && parts.at(0).compare(QLatin1String(".crlf"), kPathCase) == 0) {
        const QString &second = parts.at(1);
        if (second.compare(QLatin1String("keys"), kPathCase) == 0
            || second.compare(QLatin1String("data"), kPathCase) == 0
            || second.compare(QLatin1String("rules"), kPathCase) == 0)
            return true;
    }
    return false;
}

bool isScript(const QString &fileName)
{
    const QString suffix = QFileInfo(fileName).suffix();
    return suffix.compare(QLatin1String("py"), Qt::CaseInsensitive) == 0
           || suffix.compare(QLatin1String("sh"), Qt::CaseInsensitive) == 0
           || suffix.compare(QLatin1String("cmd"), Qt::CaseInsensitive) == 0
           || suffix.compare(QLatin1String("bat"), Qt::CaseInsensitive) == 0
           || suffix.compare(QLatin1String("ps1"), Qt::CaseInsensitive) == 0;
}

QString accessText(AgentAccess access)
{
    switch (access) {
    case AgentAccess::Allow:
        return QStringLiteral("allow");
    case AgentAccess::Ask:
        return QStringLiteral("ask");
    case AgentAccess::Review:
        return QStringLiteral("review");
    case AgentAccess::Deny:
        return QStringLiteral("deny");
    }
    return QStringLiteral("ask");
}

// Quotes group one token. A token that still contains ${ is data filled in
// at launch, not a path the project named.
QStringList commandTokens(const QString &command)
{
    QStringList tokens;
    QString current;
    QChar quote;
    for (const QChar c : command) {
        if (!quote.isNull()) {
            if (c == quote)
                quote = QChar();
            else
                current.append(c);
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            quote = c;
            continue;
        }
        if (c.isSpace()) {
            if (!current.isEmpty())
                tokens.append(current);
            current.clear();
            continue;
        }
        current.append(c);
    }
    if (!current.isEmpty())
        tokens.append(current);

    QStringList paths;
    for (const QString &token : tokens) {
        if (!token.contains(QStringLiteral("${")))
            paths.append(token);
    }
    return paths;
}

QString resolveToken(const QString &directory, const QString &token)
{
    const QString path = QDir::fromNativeSeparators(token);
    if (QDir::isAbsolutePath(path))
        return clean(path);
    return clean(directory + QLatin1Char('/') + path);
}

bool isRulesPath(const QString &root, const QString &path)
{
    const QString rules = clean(QDir(root).filePath(QStringLiteral(".crlf/rules")));
    if (!QFileInfo(rules).isDir())
        return false;
    const QString full = clean(path);
    return full.compare(rules, kPathCase) == 0 || full.startsWith(rules + QLatin1Char('/'), kPathCase);
}

QString hashFile(const QString &path, TrustHashCache *cache)
{
    const QFileInfo info(path);
    if (!info.isFile())
        return {};
    const qint64 size = info.size();
    const qint64 mtime = info.lastModified().toMSecsSinceEpoch();
    if (cache != nullptr) {
        const auto found = cache->files.constFind(path);
        if (found != cache->files.cend() && found->size == size && found->mtime == mtime)
            return found->hash;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    const QString hex = QString::fromLatin1(hash.result().toHex());
    if (cache != nullptr)
        cache->files.insert(path, {size, mtime, hex});
    return hex;
}

// A dependency or checkout directory. Walking one of these from a script
// that lives in the project root reads the whole tree, and over a WSL
// share that stalls startup.
bool skippedDirectory(const QString &name)
{
    return name.compare(QLatin1String("node_modules"), kPathCase) == 0
           || name.compare(QLatin1String(".pnpm-store"), kPathCase) == 0
           || name.compare(QLatin1String(".git"), kPathCase) == 0
           || name.compare(QLatin1String("dist"), kPathCase) == 0
           || name.compare(QLatin1String("bin"), kPathCase) == 0
           || name.compare(QLatin1String("obj"), kPathCase) == 0
           || name.compare(QLatin1String("build"), kPathCase) == 0
           || name.compare(QLatin1String("target"), kPathCase) == 0
           || name.compare(QLatin1String("__pycache__"), kPathCase) == 0;
}

// `recursive` is false for the project root. A script that lives there pins
// the scripts beside it, not every subdirectory.
void walkScripts(const QString &directory, const QString &root, bool recursive, QSet<QString> *found)
{
    const QFileInfoList entries =
        QDir(directory).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &info : entries) {
        if (info.isDir()) {
            if (!recursive || skippedDirectory(info.fileName()))
                continue;
        }
        const QString path = clean(info.absoluteFilePath());
        const QString relative = relativeTo(root, path);
        if (!insideRoot(root, path) || excludedRelative(relative))
            continue;
        if (info.isDir()) {
            walkScripts(path, root, true, found);
            continue;
        }
        if (isScript(info.fileName()))
            found->insert(path);
    }
}

struct Collected
{
    QStringList relative;
    bool readsRules = false;
};

Collected collectScripts(const QString &root, const QString &command, const QString &directory)
{
    QSet<QString> absolute;
    bool readsRules = false;
    for (const QString &token : commandTokens(command)) {
        const QString path = resolveToken(directory, token);
        if (!insideRoot(root, path))
            continue;
        if (isRulesPath(root, path))
            readsRules = true;
        const QFileInfo info(path);
        if (!info.isFile() || !isScript(info.fileName()))
            continue;
        const QString full = clean(info.absoluteFilePath());
        if (excludedRelative(relativeTo(root, full)))
            continue;
        const QString parent = clean(info.absolutePath());
        // The project root is not a script tree. Descending from it reads
        // dependency folders such as .pnpm-store on the UI thread.
        walkScripts(parent, root, relativeTo(root, parent) != QLatin1String("."), &absolute);
        absolute.insert(full);
    }

    QStringList relative;
    for (const QString &path : absolute)
        relative.append(relativeTo(root, path));
    relative.sort(kPathCase);
    return {relative, readsRules};
}

QString cwdText(const QString &root, const QString &directory)
{
    if (root.isEmpty())
        return clean(directory);
    const QString relative = relativeTo(root, directory);
    if (!insideRoot(root, directory))
        return clean(directory);
    return relative;
}

QString commandTemplate(const ProjectCommand &command, const QString &root)
{
    return command.command + QLatin1Char('\n') + QStringLiteral("cwd=") + cwdText(root, command.workingDirectory)
           + QLatin1Char('\n') + QStringLiteral("key=") + command.key.toString() + QLatin1Char('\n')
           + QStringLiteral("output=") + (command.outputToEditor ? QStringLiteral("editor") : QString());
}

QString serviceTemplate(const ServiceSettings &service, const QString &root)
{
    return service.command + QLatin1Char('\n') + QStringLiteral("cwd=") + cwdText(root, service.workingDirectory);
}

QString agentTemplate(const AgentSettings &agent, const QString &root)
{
    const AgentPermissions &permissions = agent.permissions;
    return serviceTemplate(agent.process, root) + QLatin1Char('\n') + QStringLiteral("read=")
           + accessText(permissions.read) + QLatin1Char('\n') + QStringLiteral("write=")
           + accessText(permissions.write) + QLatin1Char('\n') + QStringLiteral("terminal=")
           + accessText(permissions.terminal);
}

QString displayId(const QString &id)
{
    const int colon = id.indexOf(QLatin1Char(':'));
    if (colon >= 0)
        return id.mid(colon + 1);
    if (id.isEmpty())
        return id;
    return id.at(0).toUpper() + id.mid(1);
}

QJsonObject trustDocument(const QSettings &settings)
{
    return QJsonDocument::fromJson(settings.value(QLatin1String(kTrustSetting)).toString().toUtf8()).object();
}

void writeDocument(QSettings &settings, const QJsonObject &document)
{
    settings.setValue(QLatin1String(kTrustSetting),
                      QString::fromUtf8(QJsonDocument(document).toJson(QJsonDocument::Compact)));
}

} // namespace

bool ProjectPin::gates(const QString &id) const
{
    for (const TrustEntry &entry : entries) {
        if (entry.id == id)
            return true;
    }
    return false;
}

ProjectPin pinProject(const Project &project, TrustHashCache *cache)
{
    ProjectPin pin;
    if (project.directory.isEmpty())
        return pin;
    const QString root = clean(project.directory);

    const auto add = [&](TrustEntry entry, const QString &command, const QString &directory, bool workspaceOrigin) {
        if (command.isEmpty())
            return;
        const Collected scripts = collectScripts(root, command, directory);
        entry.files = scripts.relative;
        entry.readsRules = scripts.readsRules;
        entry.gated = workspaceOrigin || scripts.readsRules || !scripts.relative.isEmpty();
        if (!entry.gated)
            return;
        pin.entries.append(entry);
    };

    for (const ProjectCommand &command : project.commands) {
        TrustEntry entry;
        entry.id = QStringLiteral("command:") + command.name;
        entry.name = command.name;
        entry.command = command.command;
        entry.templateText = commandTemplate(command, root);
        add(entry, command.command, command.workingDirectory, command.workspaceOrigin);
    }
    if (project.syntax.enabled && !project.syntax.command.isEmpty()) {
        TrustEntry entry;
        entry.id = QStringLiteral("syntax");
        entry.name = QStringLiteral("Syntax");
        entry.command = project.syntax.command;
        entry.templateText = serviceTemplate(project.syntax, root);
        add(entry, project.syntax.command, project.syntax.workingDirectory, project.syntax.workspaceOrigin);
    }
    if (project.autocomplete.enabled && !project.autocomplete.command.isEmpty()) {
        TrustEntry entry;
        entry.id = QStringLiteral("autocomplete");
        entry.name = QStringLiteral("Autocomplete");
        entry.command = project.autocomplete.command;
        entry.templateText = serviceTemplate(project.autocomplete, root);
        add(entry, project.autocomplete.command, project.autocomplete.workingDirectory,
            project.autocomplete.workspaceOrigin);
    }
    for (const AgentSettings &agent : project.agents) {
        TrustEntry entry;
        entry.id = QStringLiteral("agent:") + agent.name;
        entry.name = agent.name;
        entry.command = agent.process.command;
        entry.templateText = agentTemplate(agent, root);
        add(entry, agent.process.command, agent.process.workingDirectory, agent.workspaceOrigin);
    }

    QHash<QString, QString> hashes;
    for (const TrustEntry &entry : pin.entries) {
        for (const QString &relative : entry.files) {
            if (hashes.contains(relative))
                continue;
            hashes.insert(relative, hashFile(clean(root + QLatin1Char('/') + relative), cache));
        }
    }
    QStringList paths = hashes.keys();
    paths.sort(kPathCase);
    for (const QString &relative : paths)
        pin.files.append({relative, hashes.value(relative)});
    return pin;
}

TrustDecision decideTrust(const ProjectPin &pin, const TrustRecord &record, TrustDifference *changes)
{
    if (changes != nullptr)
        *changes = {};
    if (pin.entries.isEmpty())
        return TrustDecision::Allow;
    if (record.state == TrustState::Restricted)
        return TrustDecision::Restricted;
    if (record.state != TrustState::Trusted)
        return TrustDecision::Untrusted;

    QHash<QString, QString> stored;
    for (const TrustTemplate &templ : record.templates)
        stored.insert(templ.id, templ.text);
    QHash<QString, QString> current;
    QHash<QString, QString> names;
    for (const TrustEntry &entry : pin.entries) {
        current.insert(entry.id, entry.templateText);
        names.insert(entry.id, entry.name);
    }

    QStringList commands;
    QSet<QString> ids;
    for (auto it = stored.cbegin(); it != stored.cend(); ++it)
        ids.insert(it.key());
    for (auto it = current.cbegin(); it != current.cend(); ++it)
        ids.insert(it.key());
    for (const QString &id : ids) {
        if (stored.value(id) != current.value(id))
            commands.append(names.value(id, displayId(id)));
    }
    commands.sort(Qt::CaseInsensitive);

    QHash<QString, QString> storedFiles;
    for (const PinnedFile &file : record.files)
        storedFiles.insert(file.relative, file.hash);
    QHash<QString, QString> currentFiles;
    for (const PinnedFile &file : pin.files)
        currentFiles.insert(file.relative, file.hash);
    QStringList paths;
    QSet<QString> fileIds;
    for (auto it = storedFiles.cbegin(); it != storedFiles.cend(); ++it)
        fileIds.insert(it.key());
    for (auto it = currentFiles.cbegin(); it != currentFiles.cend(); ++it)
        fileIds.insert(it.key());
    for (const QString &path : fileIds) {
        if (storedFiles.value(path) != currentFiles.value(path))
            paths.append(path);
    }
    paths.sort(kPathCase);

    if (commands.isEmpty() && paths.isEmpty())
        return TrustDecision::Allow;
    if (changes != nullptr) {
        changes->commands = commands;
        changes->paths = paths;
    }
    return TrustDecision::Mismatch;
}

QString trustReviewText(const ProjectPin &pin, const TrustDifference &changes)
{
    QString text;
    if (!changes.commands.isEmpty() || !changes.paths.isEmpty()) {
        text += QStringLiteral("Changed since this folder was trusted:\n");
        for (const QString &command : changes.commands)
            text += QStringLiteral("  %1\n").arg(command);
        for (const QString &path : changes.paths)
            text += QStringLiteral("  %1\n").arg(path);
        text += QLatin1Char('\n');
    }
    if (pin.entries.isEmpty()) {
        text += QStringLiteral("This folder does not define scripts that run on open.\n");
        return text;
    }
    for (const TrustEntry &entry : pin.entries) {
        text += entry.name + QLatin1Char('\n');
        text += QStringLiteral("  %1\n").arg(entry.command);
        if (entry.readsRules)
            text += QStringLiteral("  Reads .crlf/rules. Those files are not pinned.\n");
        if (entry.files.isEmpty() && !entry.readsRules)
            text += QStringLiteral("  No project script is named.\n");
        for (const QString &file : entry.files)
            text += QStringLiteral("  %1\n").arg(file);
        text += QLatin1Char('\n');
    }
    return text;
}

TrustRecord loadTrust(QSettings &settings, const QString &root)
{
    TrustRecord record;
    const QString key = trustKey(root);
    if (key.isEmpty())
        return record;
    const QJsonObject stored = trustDocument(settings).value(key).toObject();
    const QString state = stored.value(QLatin1String("state")).toString();
    if (state == QLatin1String("trusted"))
        record.state = TrustState::Trusted;
    else if (state == QLatin1String("restricted"))
        record.state = TrustState::Restricted;
    for (const QJsonValue &value : stored.value(QLatin1String("templates")).toArray()) {
        const QJsonObject templ = value.toObject();
        record.templates.append({templ.value(QLatin1String("id")).toString(),
                                 templ.value(QLatin1String("text")).toString()});
    }
    for (const QJsonValue &value : stored.value(QLatin1String("files")).toArray()) {
        const QJsonObject file = value.toObject();
        record.files.append(
            {file.value(QLatin1String("path")).toString(), file.value(QLatin1String("hash")).toString()});
    }
    return record;
}

void saveTrust(QSettings &settings, const QString &root, const TrustRecord &record)
{
    const QString key = trustKey(root);
    if (key.isEmpty())
        return;
    QJsonObject document = trustDocument(settings);
    QJsonObject stored;
    if (record.state == TrustState::Trusted)
        stored.insert(QLatin1String("state"), QStringLiteral("trusted"));
    else if (record.state == TrustState::Restricted)
        stored.insert(QLatin1String("state"), QStringLiteral("restricted"));
    else
        stored.insert(QLatin1String("state"), QStringLiteral("unknown"));
    QJsonArray templates;
    for (const TrustTemplate &templ : record.templates) {
        templates.append(QJsonObject{{QStringLiteral("id"), templ.id}, {QStringLiteral("text"), templ.text}});
    }
    stored.insert(QLatin1String("templates"), templates);
    QJsonArray files;
    for (const PinnedFile &file : record.files) {
        files.append(QJsonObject{{QStringLiteral("path"), file.relative}, {QStringLiteral("hash"), file.hash}});
    }
    stored.insert(QLatin1String("files"), files);
    document.insert(key, stored);
    writeDocument(settings, document);
}
