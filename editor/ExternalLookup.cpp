#include "editor/ExternalLookup.h"

#include "editor/TailwindNames.h"
#include "io/DocumentFile.h"
#include "syntax/LanguageDefinition.h"
#include "syntax/LanguageRegistry.h"
#include "syntax/SymbolScanner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace {

constexpr qint64 kMaxFileBytes = 1024 * 1024;
// TypeScript's lib.dom.d.ts is larger than a source file, and read one at a time.
constexpr qint64 kMaxDeclarationBytes = 8 * 1024 * 1024;
// Files read for one click, across every package that is tried.
constexpr int kMaxFiles = 24;
// A global name may live in any @types package or TypeScript library file.
constexpr int kMaxGlobalFiles = 120;
constexpr int kMaxPackages = 8;
constexpr int kMaxDepth = 4;

struct Budget {
    std::function<bool()> cancelled;
    int files = kMaxFiles;
    QSet<QString> visited;

    // Claims one read. False when the file was already read, the budget is
    // spent, or a newer click replaced this one.
    bool take(const QString &path)
    {
        const QString key = path.toLower();
        if (files <= 0 || visited.contains(key) || cancelled())
            return false;
        visited.insert(key);
        --files;
        return true;
    }
};

bool isFile(const QString &path, qint64 limit = kMaxFileBytes)
{
    const QFileInfo info(path);
    return info.isFile() && info.size() <= limit;
}

QString cleanPath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

void splitPackage(QString specifier, QString &package, QString &subpath)
{
    if (specifier.startsWith(QLatin1Char('~')))
        specifier = specifier.mid(1);
    const QStringList parts = specifier.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const int nameParts = specifier.startsWith(QLatin1Char('@')) ? 2 : 1;
    package = parts.mid(0, nameParts).join(QLatin1Char('/'));
    subpath = parts.mid(nameParts).join(QLatin1Char('/'));
}

// The nearest node_modules/<package> from `start` upward. pnpm links direct
// dependencies there and keeps the rest under .pnpm/node_modules.
QString packageDirectory(const QString &start, const QString &package)
{
    if (start.isEmpty() || package.isEmpty())
        return {};
    QDir dir(start);
    while (true) {
        for (const char *folder : {"node_modules/", "node_modules/.pnpm/node_modules/"}) {
            const QString candidate = dir.filePath(QLatin1String(folder) + package);
            if (QFileInfo(candidate + QStringLiteral("/package.json")).isFile())
                return cleanPath(candidate);
        }
        if (!dir.cdUp())
            return {};
    }
}

QJsonObject readManifest(const QString &packageDir)
{
    QFile file(QDir(packageDir).filePath(QStringLiteral("package.json")));
    if (file.size() > kMaxFileBytes || !file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

QString exportTarget(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isArray())
        return value.toArray().isEmpty() ? QString() : exportTarget(value.toArray().first());
    if (!value.isObject())
        return {};
    const QJsonObject conditions = value.toObject();
    for (const char *key : {"types", "import", "module", "default", "require", "node", "browser"}) {
        const QString found = exportTarget(conditions.value(QLatin1String(key)));
        if (!found.isEmpty())
            return found;
    }
    return {};
}

// A module path as written, with the extension or index file a bundler adds.
QString resolveModuleFile(const QString &base)
{
    static const QStringList extensions = {
        QStringLiteral(".d.ts"), QStringLiteral(".ts"),  QStringLiteral(".tsx"), QStringLiteral(".d.mts"),
        QStringLiteral(".mjs"),  QStringLiteral(".js"),  QStringLiteral(".cjs"), QStringLiteral(".jsx")};
    QStringList candidates;
    const QList<QPair<QString, QString>> declarationFor = {
        {QStringLiteral(".mjs"), QStringLiteral(".d.mts")},
        {QStringLiteral(".cjs"), QStringLiteral(".d.cts")},
        {QStringLiteral(".js"), QStringLiteral(".d.ts")},
    };
    for (const auto &[script, declaration] : declarationFor) {
        if (base.endsWith(script)) {
            candidates.append(base.left(base.size() - script.size()) + declaration);
            break;
        }
    }
    candidates.append(base);
    for (const QString &ext : extensions)
        candidates.append(base + ext);
    for (const QString &ext : extensions)
        candidates.append(QDir(base).filePath(QStringLiteral("index") + ext));
    for (const QString &candidate : candidates) {
        if (isFile(candidate))
            return cleanPath(candidate);
    }
    return {};
}

void appendEntry(QStringList &out, const QString &packageDir, const QString &relative)
{
    if (relative.isEmpty())
        return;
    const QString file = resolveModuleFile(QDir(packageDir).filePath(relative));
    if (!file.isEmpty() && !out.contains(file))
        out.append(file);
}

// Entry files for `subpath`, declaration files first.
QStringList entryFiles(const QString &packageDir, const QString &subpath)
{
    const QJsonObject manifest = readManifest(packageDir);
    QStringList out;
    const QJsonValue exports = manifest.value(QLatin1String("exports"));
    const QString key = subpath.isEmpty() ? QStringLiteral(".") : QStringLiteral("./") + subpath;
    if (exports.isObject() && exports.toObject().contains(key))
        appendEntry(out, packageDir, exportTarget(exports.toObject().value(key)));
    else if (subpath.isEmpty() && (exports.isString() || exports.isObject()))
        appendEntry(out, packageDir, exportTarget(exports));
    if (subpath.isEmpty()) {
        for (const char *field : {"types", "typings", "module", "main"})
            appendEntry(out, packageDir, manifest.value(QLatin1String(field)).toString());
        appendEntry(out, packageDir, QStringLiteral("index"));
    } else {
        appendEntry(out, packageDir, subpath);
    }
    return out;
}

void place(const QString &text, int offset, SymbolLocation &loc)
{
    const int lineStart = offset == 0 ? 0 : text.lastIndexOf(QLatin1Char('\n'), offset - 1) + 1;
    loc.line = int(text.left(offset).count(QLatin1Char('\n'))) + 1;
    loc.column = offset - lineStart + 1;
}

SymbolLocation searchModule(const QString &path, QString name, bool defaultExport,
                            const LanguageRegistry *registry, int depth, Budget &budget)
{
    if (path.isEmpty() || depth > kMaxDepth || !budget.take(path))
        return {};
    const QString text = DocumentFile(path).load();
    const LanguageDefinition *def = registry->definitionForPath(path);
    const SymbolScan scan = scanSource(text, def, true);
    SymbolLocation loc;
    loc.path = path;

    if (defaultExport) {
        static const QRegularExpression exportAssign(QStringLiteral("\\bexport\\s*=\\s*([A-Za-z_$][\\w$]*)"));
        if (!scan.defaultName.isEmpty()) {
            name = scan.defaultName;
        } else if (scan.defaultLine > 0) {
            loc.line = scan.defaultLine;
            loc.column = scan.defaultColumn;
            return loc;
        } else if (const QRegularExpressionMatch match = exportAssign.match(text); match.hasMatch()) {
            name = match.captured(1);
        } else {
            return {};
        }
    }

    const SymbolHit *localHit = nullptr;
    for (const SymbolHit &hit : scan.hits) {
        if (hit.name != name)
            continue;
        if (!hit.local) {
            loc.line = hit.line;
            loc.column = hit.column;
            return loc;
        }
        if (localHit == nullptr)
            localHit = &hit;
    }
    if (localHit != nullptr) {
        loc.line = localHit->line;
        loc.column = localHit->column;
        return loc;
    }
    const QRegularExpression declared(
        QStringLiteral("\\b(?:function|class|interface|type|enum|namespace|module|const|let|var)\\s+(%1)\\b")
            .arg(QRegularExpression::escape(name)));
    if (const QRegularExpressionMatch match = declared.match(text); match.hasMatch()) {
        place(text, int(match.capturedStart(1)), loc);
        return loc;
    }

    const QString dir = QFileInfo(path).absolutePath();
    for (const ModuleBinding &binding : scan.modules) {
        if (!binding.reexport || binding.star || binding.local != name
            || !binding.specifier.startsWith(QLatin1Char('.')))
            continue;
        const QString target = resolveModuleFile(QDir(dir).filePath(binding.specifier));
        if (binding.whole) {
            SymbolLocation module;
            module.path = target;
            module.line = target.isEmpty() ? 0 : 1;
            module.column = 1;
            return module;
        }
        return searchModule(target, binding.imported, binding.imported.isEmpty(), registry, depth + 1, budget);
    }
    for (const ModuleBinding &binding : scan.modules) {
        if (!binding.star || !binding.specifier.startsWith(QLatin1Char('.')))
            continue;
        const QString target = resolveModuleFile(QDir(dir).filePath(binding.specifier));
        const SymbolLocation found = searchModule(target, name, false, registry, depth + 1, budget);
        if (found.line != 0)
            return found;
    }
    return {};
}

// The module `path` re-exports as `export * as name from`, or empty.
QString namespaceTarget(const QString &path, const QString &name, const LanguageRegistry *registry)
{
    const SymbolScan scan = scanSource(DocumentFile(path).load(), registry->definitionForPath(path), false);
    for (const ModuleBinding &binding : scan.modules) {
        if (binding.reexport && binding.whole && binding.local == name && binding.specifier.startsWith(QLatin1Char('.')))
            return resolveModuleFile(QDir(QFileInfo(path).absolutePath()).filePath(binding.specifier));
    }
    return {};
}

SymbolLocation findPackageExport(const ExternalQuery &query, const QString &start,
                                 const LanguageRegistry *registry)
{
    const SymbolLocation &target = query.target;
    QString package;
    QString subpath;
    splitPackage(target.specifier, package, subpath);
    QStringList files;
    const QString packageDir = packageDirectory(start, package);
    if (!packageDir.isEmpty())
        files = entryFiles(packageDir, subpath);
    const QString typesName = package.startsWith(QLatin1Char('@'))
                                  ? package.mid(1).replace(QLatin1Char('/'), QStringLiteral("__"))
                                  : package;
    const QString typesDir = packageDirectory(start, QStringLiteral("@types/") + typesName);
    if (!typesDir.isEmpty()) {
        for (const QString &file : entryFiles(typesDir, subpath)) {
            if (!files.contains(file))
                files.append(file);
        }
    }

    if (target.wholeModule && !files.isEmpty()) {
        SymbolLocation loc;
        loc.path = files.first();
        loc.line = 1;
        loc.column = 1;
        return loc;
    }
    Budget budget{query.cancelled};
    if (!target.container.isEmpty()) {
        // `Imported.member` where the entry has `export * as Imported from './parts'`:
        // search the member in that module.
        for (const QString &file : files) {
            const QString parts = namespaceTarget(file, target.container, registry);
            const SymbolLocation loc =
                parts.isEmpty() ? SymbolLocation() : searchModule(parts, target.name, false, registry, 0, budget);
            if (loc.line != 0)
                return loc;
        }
    }
    for (const QString &file : files) {
        const SymbolLocation loc = searchModule(file, target.name, target.defaultExport, registry, 0, budget);
        if (loc.line != 0)
            return loc;
    }
    return {};
}

// The folders that hold @types packages, nearest node_modules first.
QStringList typesRoots(const QString &start)
{
    QStringList roots;
    QDir dir(start);
    while (roots.isEmpty()) {
        for (const char *folder : {"node_modules/@types", "node_modules/.pnpm/node_modules/@types"}) {
            const QString candidate = dir.filePath(QLatin1String(folder));
            if (QFileInfo(candidate).isDir())
                roots.append(cleanPath(candidate));
        }
        if (!dir.cdUp())
            break;
    }
    return roots;
}

// Declaration files that may hold an ambient name, most likely first: React's
// types, TypeScript's DOM and ES5 libraries, Node's types, the remaining
// TypeScript libraries, then every other @types package.
QStringList globalDeclarationFiles(const QString &start)
{
    QStringList files;
    const auto add = [&files](const QString &file) {
        if (!file.isEmpty() && !files.contains(file))
            files.append(file);
    };
    const QStringList roots = typesRoots(start);
    const auto addTypes = [&](const QString &name) {
        for (const QString &root : roots) {
            const QString packageDir = QDir(root).filePath(name);
            if (!QFileInfo(packageDir).isDir())
                continue;
            for (const QString &entry : entryFiles(packageDir, QString()))
                add(entry);
            const QString global = QDir(packageDir).filePath(QStringLiteral("global.d.ts"));
            if (isFile(global))
                add(cleanPath(global));
        }
    };
    const QString typescript = packageDirectory(start, QStringLiteral("typescript"));
    const QDir lib(QDir(typescript).filePath(QStringLiteral("lib")));

    addTypes(QStringLiteral("react"));
    addTypes(QStringLiteral("react-dom"));
    if (!typescript.isEmpty()) {
        add(cleanPath(lib.filePath(QStringLiteral("lib.dom.d.ts"))));
        add(cleanPath(lib.filePath(QStringLiteral("lib.es5.d.ts"))));
    }
    addTypes(QStringLiteral("node"));
    if (!typescript.isEmpty()) {
        for (const QString &name : lib.entryList({QStringLiteral("lib.*.d.ts")}, QDir::Files, QDir::Name))
            add(cleanPath(lib.filePath(name)));
    }
    for (const QString &root : roots) {
        for (const QString &name : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            addTypes(name);
    }
    return files;
}

// One declaration file, then the files its `/// <reference path>` lines name.
SymbolLocation searchDeclarations(const QString &path, const QList<QRegularExpression> &definitions,
                                  const QRegularExpression &container, const QString &name, int depth,
                                  Budget &budget)
{
    if (depth > 2 || !isFile(path, kMaxDeclarationBytes) || !budget.take(path))
        return {};
    const QString text = DocumentFile(path).load();
    if (text.contains(name)) {
        int from = 0;
        if (!container.pattern().isEmpty()) {
            const QRegularExpressionMatch scope = container.match(text);
            from = scope.hasMatch() ? int(scope.capturedEnd()) : -1;
        }
        for (const QRegularExpression &definition : definitions) {
            if (from < 0)
                break;
            const QRegularExpressionMatch match = definition.match(text, from);
            if (!match.hasMatch())
                continue;
            SymbolLocation loc;
            loc.path = path;
            place(text, int(match.capturedStart(1)), loc);
            return loc;
        }
    }
    static const QRegularExpression reference(QStringLiteral("^///\\s*<reference\\s+path=[\"']([^\"']+)[\"']"),
                                              QRegularExpression::MultilineOption);
    const QDir dir = QFileInfo(path).absoluteDir();
    QRegularExpressionMatchIterator refs = reference.globalMatch(text);
    while (refs.hasNext()) {
        const SymbolLocation loc = searchDeclarations(cleanPath(dir.filePath(refs.next().captured(1))), definitions,
                                                      container, name, depth + 1, budget);
        if (loc.line != 0)
            return loc;
    }
    return {};
}

SymbolLocation findGlobal(const ExternalQuery &query, const QString &start)
{
    const SymbolLocation &target = query.target;
    const QString name = QRegularExpression::escape(target.name);
    const QList<QRegularExpression> definitions{
        QRegularExpression(QStringLiteral("\\b(?:interface|class|type|enum|namespace|module)\\s+(%1)\\b").arg(name)),
        QRegularExpression(QStringLiteral("\\b(?:function|var|const|let)\\s+(%1)\\b").arg(name)),
    };
    const QRegularExpression container =
        target.container.isEmpty()
            ? QRegularExpression()
            : QRegularExpression(QStringLiteral("\\b(?:namespace|module)\\s+%1\\b")
                                     .arg(QRegularExpression::escape(target.container)));
    Budget budget{query.cancelled, kMaxGlobalFiles};
    for (const QString &file : globalDeclarationFiles(start)) {
        const SymbolLocation loc = searchDeclarations(file, definitions, container, target.name, 0, budget);
        if (loc.line != 0 || budget.files <= 0 || query.cancelled())
            return loc;
    }
    return {};
}

QString resolveStyleFile(const QString &base)
{
    const QFileInfo info(base);
    const QString dir = info.path();
    const QString name = info.fileName();
    const QStringList candidates = {
        base,
        base + QStringLiteral(".scss"),
        base + QStringLiteral(".sass"),
        base + QStringLiteral(".css"),
        QDir(dir).filePath(QLatin1Char('_') + name + QStringLiteral(".scss")),
        QDir(dir).filePath(QLatin1Char('_') + name + QStringLiteral(".sass")),
        QDir(base).filePath(QStringLiteral("_index.scss")),
        QDir(base).filePath(QStringLiteral("index.scss")),
        QDir(base).filePath(QStringLiteral("index.css")),
    };
    for (const QString &candidate : candidates) {
        if (isFile(candidate))
            return cleanPath(candidate);
    }
    return {};
}

// The definition pattern for a stylesheet key from CodeEditor.
QRegularExpression styleDefinition(const QString &key)
{
    const QString name = QRegularExpression::escape(key.mid(1));
    if (key.startsWith(QLatin1String("--")))
        return QRegularExpression(QStringLiteral("(?<![\\w-])(%1)\\s*:").arg(QRegularExpression::escape(key)));
    if (key.startsWith(QLatin1Char('$')))
        return QRegularExpression(QStringLiteral("(?m)^[ \\t]*(\\$%1)\\s*:").arg(name));
    if (key.startsWith(QLatin1Char('@')))
        return QRegularExpression(
            QStringLiteral("(?m)(?:@(?:mixin|function|(?:-webkit-)?keyframes)\\s+|^[ \\t]*=\\s*)(%1)(?![\\w-])")
                .arg(name));
    return QRegularExpression(QStringLiteral("(?<![\\w-])(%1)(?![\\w-])").arg(QRegularExpression::escape(key)));
}

// The first definition in `text`, trying `definitions` in order.
SymbolLocation matchFirst(const QString &path, const QString &text, const QList<QRegularExpression> &definitions)
{
    for (const QRegularExpression &definition : definitions) {
        if (const QRegularExpressionMatch match = definition.match(text); match.hasMatch()) {
            SymbolLocation loc;
            loc.path = path;
            place(text, int(match.capturedStart(1)), loc);
            return loc;
        }
    }
    return {};
}

// Searches one stylesheet, then the relative partials it imports.
SymbolLocation searchStyle(const QString &path, const QList<QRegularExpression> &definitions, int depth,
                           Budget &budget)
{
    if (path.isEmpty() || depth > kMaxDepth || !budget.take(path))
        return {};
    const QString text = DocumentFile(path).load();
    if (const SymbolLocation loc = matchFirst(path, text, definitions); loc.line != 0)
        return loc;
    static const QRegularExpression importRule(QStringLiteral("@(?:import|use|forward)\\s+([^;\\n]+)"));
    static const QRegularExpression quoted(QStringLiteral("['\"]([^'\"]+)['\"]"));
    const QDir dir = QFileInfo(path).absoluteDir();
    QRegularExpressionMatchIterator imports = importRule.globalMatch(text);
    while (imports.hasNext()) {
        QRegularExpressionMatchIterator names = quoted.globalMatch(imports.next().captured(1));
        while (names.hasNext()) {
            const QString spec = names.next().captured(1);
            if (spec.contains(QLatin1Char(':')) || spec.startsWith(QLatin1Char('~')))
                continue;
            const SymbolLocation loc = searchStyle(resolveStyleFile(dir.filePath(spec)), definitions, depth + 1, budget);
            if (loc.line != 0)
                return loc;
        }
    }
    return {};
}

QStringList importedPackages(const QString &text)
{
    static const QRegularExpression specifier(QStringLiteral(
        "(?:\\bfrom\\s+|\\bimport\\s+|\\brequire\\(\\s*|@import\\s+(?:url\\(\\s*)?|@use\\s+|@forward\\s+)['\"]([^'\"]+)['\"]"));
    QStringList packages;
    QRegularExpressionMatchIterator it = specifier.globalMatch(text);
    while (it.hasNext() && packages.size() < kMaxPackages) {
        QString spec = it.next().captured(1);
        if (spec.startsWith(QLatin1Char('~')))
            spec = spec.mid(1);
        if (spec.startsWith(QLatin1Char('.')) || spec.startsWith(QLatin1Char('/')) || spec.contains(QLatin1Char(':')))
            continue;
        if (!packages.contains(spec))
            packages.append(spec);
    }
    return packages;
}

// A Tailwind v3 theme key in the project's tailwind.config, then in the
// tailwindcss package's default colors and full config.
SymbolLocation findTailwindKey(const TailwindClass &tailwind, const QString &start, Budget &budget)
{
    static const QRegularExpression numeric(QStringLiteral("^[\\d.]+$"));
    QStringList keys;
    if (!tailwind.value.isEmpty() && !numeric.match(tailwind.value).hasMatch()) {
        keys.append(tailwind.value);
        const QString first = tailwind.value.section(QLatin1Char('-'), 0, 0);
        if (first != tailwind.value && !numeric.match(first).hasMatch())
            keys.append(first);
    }
    if (keys.isEmpty())
        return {};
    QList<QRegularExpression> definitions;
    for (const QString &key : keys)
        definitions.append(QRegularExpression(
            QStringLiteral("(?m)(?:^|[\\s{,])(?:['\"](%1)['\"]|(%1))\\s*:").arg(QRegularExpression::escape(key))));
    const auto search = [&](const QString &path) -> SymbolLocation {
        if (!isFile(path) || !budget.take(path))
            return {};
        const QString text = DocumentFile(path).load();
        for (const QRegularExpression &definition : definitions) {
            const QRegularExpressionMatch match = definition.match(text);
            if (!match.hasMatch())
                continue;
            SymbolLocation loc;
            loc.path = cleanPath(path);
            place(text, int(match.capturedStart(1) >= 0 ? match.capturedStart(1) : match.capturedStart(2)), loc);
            return loc;
        }
        return {};
    };

    QDir dir(start);
    while (true) {
        for (const char *name : {"tailwind.config.ts", "tailwind.config.js", "tailwind.config.mjs", "tailwind.config.cjs"}) {
            if (const SymbolLocation loc = search(dir.filePath(QLatin1String(name))); loc.line != 0)
                return loc;
        }
        if (dir.exists(QStringLiteral("package.json")) || !dir.cdUp())
            break;
    }
    const QString package = packageDirectory(start, QStringLiteral("tailwindcss"));
    if (package.isEmpty())
        return {};
    for (const char *relative : {"lib/public/colors.js", "src/public/colors.js", "stubs/config.full.js"}) {
        if (const SymbolLocation loc = search(QDir(package).filePath(QLatin1String(relative))); loc.line != 0)
            return loc;
    }
    return {};
}

SymbolLocation findStyleName(const ExternalQuery &query, const QString &start)
{
    const QString &key = query.target.name;
    const bool className = key.startsWith(QLatin1Char('.'));
    const TailwindClass tailwind = className ? parseTailwindClass(key.mid(1)) : TailwindClass();
    QList<QRegularExpression> definitions{styleDefinition(key)};
    for (const QString &variable : tailwind.variables)
        definitions.append(styleDefinition(variable));
    QStringList packages = importedPackages(query.text);
    if (!packages.contains(QStringLiteral("tailwindcss")))
        packages.append(QStringLiteral("tailwindcss"));

    Budget budget{query.cancelled};
    for (const QString &spec : packages) {
        QString package;
        QString subpath;
        splitPackage(spec, package, subpath);
        const QString packageDir = packageDirectory(start, package);
        if (packageDir.isEmpty())
            continue;
        QStringList files;
        if (!subpath.isEmpty()) {
            files.append(resolveStyleFile(QDir(packageDir).filePath(subpath)));
        } else {
            const QJsonObject manifest = readManifest(packageDir);
            for (const char *field : {"style", "sass"}) {
                const QString relative = manifest.value(QLatin1String(field)).toString();
                if (!relative.isEmpty())
                    files.append(resolveStyleFile(QDir(packageDir).filePath(relative)));
            }
        }
        for (const QString &file : files) {
            const SymbolLocation loc = searchStyle(file, definitions, 0, budget);
            if (loc.line != 0)
                return loc;
        }
        if (budget.files <= 0)
            break;
    }
    return className ? findTailwindKey(tailwind, start, budget) : SymbolLocation();
}

} // namespace

SymbolLocation findExternal(const ExternalQuery &query, const LanguageRegistry *registry)
{
    const QString start = query.target.path.isEmpty() ? query.root : QFileInfo(query.target.path).absolutePath();
    switch (query.target.kind) {
    case ExternalKind::Package:
        return findPackageExport(query, start, registry);
    case ExternalKind::Stylesheet:
        return findStyleName(query, start);
    case ExternalKind::Global:
        return findGlobal(query, start);
    }
    return {};
}
