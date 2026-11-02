#include "editor/SymbolIndex.h"

#include "editor/ImportAliases.h"

#include "io/DocumentFile.h"
#include "syntax/LanguageDefinition.h"
#include "syntax/LanguageRegistry.h"
#include "syntax/SymbolScanner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QRegularExpression>
#include <QPointer>
#include <QSet>
#include <QThreadPool>

namespace {

constexpr qint64 kMaxFileBytes = 1024 * 1024;

struct ScannedFile {
    QString path;
    bool ignoreCase = false;
    SymbolScan scan;
};

bool scanUseful(const SymbolScan &scan)
{
    return !scan.hits.isEmpty() || !scan.modules.isEmpty() || scan.defaultLine > 0;
}

QString normalizedPath(const QString &path)
{
    return path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
}

bool nameHidden(const QStringList &hide, const QString &name)
{
    for (const QString &pattern : hide) {
        if (!pattern.contains(QLatin1Char('*')) && !pattern.contains(QLatin1Char('?'))) {
            if (name.compare(pattern, Qt::CaseInsensitive) == 0)
                return true;
        } else {
            const QRegularExpression expression(
                QRegularExpression::wildcardToRegularExpression(pattern),
                QRegularExpression::CaseInsensitiveOption);
            if (expression.match(name).hasMatch())
                return true;
        }
    }
    return false;
}

QList<ScannedFile> collectProject(const QString &root, const QStringList &hide,
                                  const LanguageRegistry *registry)
{
    QList<ScannedFile> found;
    QSet<QString> seenDirs;
    QStringList pending{root};
    while (!pending.isEmpty()) {
        const QString dir = pending.takeLast();
        const QString dirKey = normalizedPath(dir).toLower();
        if (!seenDirs.contains(dirKey))
            seenDirs.insert(dirKey);
        else
            continue;

        const QFileInfoList entries =
            QDir(dir).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const QFileInfo &info : entries) {
            if (nameHidden(hide, info.fileName()))
                continue;
            if (info.isDir()) {
                if (!info.fileName().startsWith(QLatin1Char('.')))
                    pending.append(info.absoluteFilePath());
                continue;
            }
            if (!info.isFile() || info.size() > kMaxFileBytes)
                continue;
            const LanguageDefinition *def = registry->definitionForPath(info.filePath());
            if (def->symbols.isEmpty() && !def->modules)
                continue;
            ScannedFile file;
            file.path = normalizedPath(info.filePath());
            file.ignoreCase = def->ignoreCase;
            file.scan = scanSource(DocumentFile(file.path).load(), def, true);
            if (scanUseful(file.scan))
                found.append(file);
        }
    }
    return found;
}

ScannedFile scanDiskFile(const QString &path, const LanguageRegistry *registry)
{
    ScannedFile file;
    file.path = path;
    const QFileInfo info(path);
    if (!info.isFile() || info.size() > kMaxFileBytes)
        return file;
    const LanguageDefinition *def = registry->definitionForPath(path);
    if (def->symbols.isEmpty() && !def->modules)
        return file;
    file.ignoreCase = def->ignoreCase;
    file.scan = scanSource(DocumentFile(path).load(), def, true);
    return file;
}

} // namespace

SymbolIndex::SymbolIndex(const LanguageRegistry *registry, QObject *parent)
    : QObject(parent)
    , m_registry(registry)
{
}

void SymbolIndex::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    ++m_walkGeneration;
    m_bufferGeneration.clear();
    m_fileGeneration.clear();
    m_slices.clear();
    m_byName.clear();
    m_editors.clear();
    m_aliasKey.clear();
    if (!enabled) {
        emit updated();
        return;
    }
    const QString root = m_root;
    m_root.clear();
    setProject(root, m_hide);
}

void SymbolIndex::setProject(const QString &root, const QStringList &hide)
{
    const QString normal = normalizedPath(root);
    if (!m_enabled) {
        m_root = normal;
        m_hide = hide;
        return;
    }
    if (normal == m_root && hide == m_hide)
        return;
    m_root = normal;
    m_hide = hide;
    const int generation = ++m_walkGeneration;
    if (normal.isEmpty()) {
        QStringList drop;
        for (auto it = m_slices.cbegin(); it != m_slices.cend(); ++it) {
            if (it.value().editorId == 0)
                drop.append(it.key());
        }
        for (const QString &key : drop)
            m_slices.remove(key);
        rebuild();
        emit updated();
        return;
    }

    const LanguageRegistry *registry = m_registry;
    QPointer<SymbolIndex> alive(this);
    QThreadPool::globalInstance()->start([alive, normal, hide, registry, generation] {
        const QList<ScannedFile> scanned = collectProject(normal, hide, registry);
        QCoreApplication *app = QCoreApplication::instance();
        if (app == nullptr)
            return;
        QMetaObject::invokeMethod(app, [alive, scanned, generation] {
            if (alive.isNull())
                return;
            QList<DiskFile> files;
            for (const ScannedFile &file : scanned) {
                DiskFile disk;
                disk.path = file.path;
                disk.ignoreCase = file.ignoreCase;
                for (const SymbolHit &hit : file.scan.hits)
                    disk.hits.append({hit.name, hit.line, hit.column, hit.local});
                disk.modules = file.scan.modules;
                disk.defaultName = file.scan.defaultName;
                disk.defaultLine = file.scan.defaultLine;
                disk.defaultColumn = file.scan.defaultColumn;
                files.append(disk);
            }
            alive->applyDisk(generation, files);
        }, Qt::QueuedConnection);
    });
}

void SymbolIndex::updateBuffer(quintptr editorId, const QString &path, const QString &text,
                               const LanguageDefinition *language)
{
    if (!m_enabled)
        return;
    const QString normal = normalizedPath(path);
    const QString key = sliceKey(editorId, normal);
    const QString previous = m_editors.value(editorId);
    m_editors.insert(editorId, key);
    if (!previous.isEmpty() && previous != key)
        releaseKey(previous);

    const int generation = ++m_bufferGeneration[editorId];
    const bool ignoreCase = language != nullptr && language->ignoreCase;
    QPointer<SymbolIndex> alive(this);
    QThreadPool::globalInstance()->start([alive, editorId, key, normal, text, language, ignoreCase, generation] {
        const SymbolScan scan = language == nullptr ? SymbolScan() : scanSource(text, language, true);
        QCoreApplication *app = QCoreApplication::instance();
        if (app == nullptr)
            return;
        QMetaObject::invokeMethod(app, [alive, editorId, key, normal, scan, ignoreCase, generation] {
            if (alive.isNull() || !alive->m_enabled || alive->m_bufferGeneration.value(editorId) != generation)
                return;
            if (alive->m_editors.value(editorId) != key)
                return;
            Slice slice;
            slice.path = normal;
            slice.editorId = editorId;
            slice.ignoreCase = ignoreCase;
            QList<Hit> hits;
            for (const SymbolHit &hit : scan.hits)
                hits.append({hit.name, hit.line, hit.column, hit.local});
            fillSlice(slice, hits, scan.modules, scan.defaultName, scan.defaultLine, scan.defaultColumn);
            alive->m_slices.insert(key, slice);
            alive->rebuild();
            emit alive->updated();
        }, Qt::QueuedConnection);
    });
}

void SymbolIndex::removeBuffer(quintptr editorId)
{
    const QString key = m_editors.take(editorId);
    m_bufferGeneration.remove(editorId);
    if (!key.isEmpty())
        releaseKey(key);
}

SymbolLocation SymbolIndex::find(const QString &name, bool ignoreCase, const QString &currentPath,
                                 quintptr editorId, int line, int column) const
{
    if (!m_enabled)
        return {};
    if (const Slice *current = sliceForEditor(editorId)) {
        bool imported = false;
        const SymbolLocation resolved = resolveImport(*current, name, imported);
        if (imported) {
            const bool sameToken = resolved.line == line && resolved.column == column
                                   && (resolved.editorId == editorId
                                       || (!current->path.isEmpty()
                                           && resolved.path.compare(current->path, Qt::CaseInsensitive) == 0));
            if (sameToken)
                return {};
            return resolved;
        }
        const SymbolLocation local = hitLocation(*current, name, line, column);
        if (local.line != 0)
            return local;
    }

    const QString key = ignoreCase ? name.toLower() : name;
    const QString here = normalizedPath(currentPath);
    SymbolLocation sameFile;
    SymbolLocation other;
    for (const SymbolLocation &loc : m_byName.value(key)) {
        const bool inThis = (editorId != 0 && loc.editorId == editorId)
                            || (!here.isEmpty() && loc.path.compare(here, Qt::CaseInsensitive) == 0);
        if (inThis && loc.line == line && loc.column == column)
            continue;
        if (inThis) {
            if (sameFile.line == 0)
                sameFile = loc;
        } else if (other.line == 0) {
            other = loc;
        }
    }
    return sameFile.line != 0 ? sameFile : other;
}

QString SymbolIndex::sliceKey(quintptr editorId, const QString &path) const
{
    return path.isEmpty() ? QStringLiteral("untitled:%1").arg(editorId) : path;
}

bool SymbolIndex::bufferOwns(const QString &path) const
{
    for (auto it = m_editors.cbegin(); it != m_editors.cend(); ++it) {
        if (it.value().compare(path, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

bool SymbolIndex::insideProject(const QString &path) const
{
    if (m_root.isEmpty() || path.isEmpty())
        return false;
    if (path.compare(m_root, Qt::CaseInsensitive) == 0)
        return true;
    const QString slash = m_root + QLatin1Char('/');
    const QString back = m_root + QLatin1Char('\\');
    return path.startsWith(slash, Qt::CaseInsensitive) || path.startsWith(back, Qt::CaseInsensitive);
}

void SymbolIndex::releaseKey(const QString &key)
{
    for (auto it = m_editors.cbegin(); it != m_editors.cend(); ++it) {
        if (it.value() == key)
            return;
    }
    const QString path = m_slices.value(key).path;
    m_slices.remove(key);
    rebuild();
    emit updated();
    if (!path.isEmpty() && insideProject(path))
        scheduleDiskFile(path);
}

void SymbolIndex::scheduleDiskFile(const QString &path)
{
    const int generation = ++m_fileGeneration[path];
    const LanguageRegistry *registry = m_registry;
    QPointer<SymbolIndex> alive(this);
    QThreadPool::globalInstance()->start([alive, path, registry, generation] {
        const ScannedFile scanned = scanDiskFile(path, registry);
        QCoreApplication *app = QCoreApplication::instance();
        if (app == nullptr)
            return;
        QMetaObject::invokeMethod(app, [alive, scanned, generation] {
            if (alive.isNull() || alive->m_fileGeneration.value(scanned.path) != generation)
                return;
            if (alive->bufferOwns(scanned.path))
                return;
            if (!scanUseful(scanned.scan)) {
                const auto existing = alive->m_slices.constFind(scanned.path);
                if (existing != alive->m_slices.cend() && existing->editorId == 0) {
                    alive->m_slices.remove(scanned.path);
                    alive->rebuild();
                    emit alive->updated();
                }
                return;
            }
            Slice slice;
            slice.path = scanned.path;
            slice.ignoreCase = scanned.ignoreCase;
            QList<Hit> hits;
            for (const SymbolHit &hit : scanned.scan.hits)
                hits.append({hit.name, hit.line, hit.column, hit.local});
            fillSlice(slice, hits, scanned.scan.modules, scanned.scan.defaultName, scanned.scan.defaultLine,
                      scanned.scan.defaultColumn);
            alive->m_slices.insert(scanned.path, slice);
            alive->rebuild();
            emit alive->updated();
        }, Qt::QueuedConnection);
    });
}

const SymbolIndex::Slice *SymbolIndex::sliceForEditor(quintptr editorId) const
{
    if (editorId == 0)
        return nullptr;
    for (auto it = m_slices.cbegin(); it != m_slices.cend(); ++it) {
        if (it->editorId == editorId)
            return &it.value();
    }
    return nullptr;
}

const SymbolIndex::Slice *SymbolIndex::sliceForPath(const QString &path) const
{
    if (path.isEmpty())
        return nullptr;
    const QString normal = normalizedPath(path);
    const auto exact = m_slices.constFind(normal);
    if (exact != m_slices.cend())
        return &exact.value();
    for (auto it = m_slices.cbegin(); it != m_slices.cend(); ++it) {
        if (it.key().compare(normal, Qt::CaseInsensitive) == 0)
            return &it.value();
    }
    return nullptr;
}

const ImportAliases &SymbolIndex::aliasesFor(const QString &fromFile) const
{
    const QString key = m_root + QLatin1Char('\n') + QFileInfo(fromFile).absolutePath();
    if (key != m_aliasKey) {
        m_importAliases = importAliasesFor(m_root, fromFile);
        m_aliasKey = key;
    }
    return m_importAliases;
}

QString SymbolIndex::indexedModule(const QString &base) const
{
    if (base.isEmpty())
        return {};
    const QStringList extensions = {QStringLiteral(".tsx"), QStringLiteral(".ts"), QStringLiteral(".jsx"),
                                    QStringLiteral(".js"),  QStringLiteral(".mts"), QStringLiteral(".cts"),
                                    QStringLiteral(".mjs"), QStringLiteral(".cjs")};
    const auto knownExtension = [&extensions](const QString &path) {
        for (const QString &ext : extensions) {
            if (path.endsWith(ext, Qt::CaseInsensitive))
                return true;
        }
        return false;
    };

    QStringList candidates;
    if (knownExtension(base)) {
        candidates.append(base);
        const QString stem = base.left(base.lastIndexOf(QLatin1Char('.')));
        for (const QString &ext : extensions)
            candidates.append(stem + ext);
    } else {
        for (const QString &ext : extensions)
            candidates.append(base + ext);
        for (const QString &ext : extensions)
            candidates.append(QDir(base).filePath(QStringLiteral("index") + ext));
    }
    for (const QString &candidate : candidates) {
        const QString normal = normalizedPath(candidate);
        if (sliceForPath(normal) != nullptr)
            return normal;
    }
    return {};
}

QString SymbolIndex::resolveSpecifier(const QString &fromFile, const QString &specifier) const
{
    if (fromFile.isEmpty() || specifier.isEmpty())
        return {};
    QStringList bases;
    if (specifier.startsWith(QLatin1Char('.')))
        bases.append(normalizedPath(QDir(QFileInfo(fromFile).absolutePath()).filePath(specifier)));
    else
        bases = aliasTargets(aliasesFor(fromFile), specifier);
    for (const QString &base : bases) {
        const QString found = indexedModule(base);
        if (!found.isEmpty())
            return found;
    }
    return {};
}

bool SymbolIndex::namesEqual(const QString &left, const QString &right, bool ignoreCase)
{
    return ignoreCase ? left.compare(right, Qt::CaseInsensitive) == 0 : left == right;
}

void SymbolIndex::fillSlice(Slice &slice, const QList<Hit> &hits, const QList<ModuleBinding> &modules,
                            const QString &defaultName, int defaultLine, int defaultColumn)
{
    slice.hits = hits;
    slice.modules = modules;
    slice.defaultName = defaultName;
    slice.defaultLine = defaultLine;
    slice.defaultColumn = defaultColumn;
}

SymbolLocation SymbolIndex::hitLocation(const Slice &slice, const QString &name, int skipLine,
                                      int skipColumn) const
{
    SymbolLocation localHit;
    for (const Hit &hit : slice.hits) {
        if (!namesEqual(hit.name, name, slice.ignoreCase))
            continue;
        if (skipLine > 0 && hit.line == skipLine && hit.column == skipColumn)
            continue;
        SymbolLocation loc;
        loc.path = slice.path;
        loc.editorId = slice.editorId;
        loc.line = hit.line;
        loc.column = hit.column;
        if (!hit.local)
            return loc;
        if (localHit.line == 0)
            localHit = loc;
    }
    return localHit;
}

SymbolLocation SymbolIndex::lookupExport(const QString &path, const QString &imported, int depth) const
{
    if (path.isEmpty() || depth > 8)
        return {};
    const Slice *slice = sliceForPath(path);
    if (slice == nullptr)
        return {};
    if (imported.isEmpty()) {
        if (!slice->defaultName.isEmpty()) {
            const SymbolLocation declared = hitLocation(*slice, slice->defaultName);
            if (declared.line != 0)
                return declared;
        }
        if (slice->defaultLine != 0) {
            SymbolLocation loc;
            loc.path = slice->path;
            loc.editorId = slice->editorId;
            loc.line = slice->defaultLine;
            loc.column = slice->defaultColumn;
            return loc;
        }
    } else {
        const SymbolLocation declared = hitLocation(*slice, imported);
        if (declared.line != 0)
            return declared;
    }

    for (const ModuleBinding &binding : slice->modules) {
        if (!binding.reexport || binding.star)
            continue;
        const bool match = imported.isEmpty() ? binding.imported.isEmpty()
                                              : namesEqual(binding.local, imported, slice->ignoreCase);
        if (!match)
            continue;
        return lookupExport(resolveSpecifier(slice->path, binding.specifier), binding.imported, depth + 1);
    }
    if (!imported.isEmpty()) {
        for (const ModuleBinding &binding : slice->modules) {
            if (!binding.star)
                continue;
            const SymbolLocation loc =
                lookupExport(resolveSpecifier(slice->path, binding.specifier), imported, depth + 1);
            if (loc.line != 0)
                return loc;
        }
    }
    return {};
}

SymbolLocation SymbolIndex::resolveImport(const Slice &slice, const QString &name, bool &matched) const
{
    matched = false;
    for (const ModuleBinding &binding : slice.modules) {
        if (binding.reexport || binding.star)
            continue;
        if (!namesEqual(binding.local, name, slice.ignoreCase))
            continue;
        matched = true;
        if (slice.path.isEmpty())
            return {};
        return lookupExport(resolveSpecifier(slice.path, binding.specifier), binding.imported, 0);
    }
    return {};
}

void SymbolIndex::rebuild()
{
    m_byName.clear();
    QStringList keys = m_slices.keys();
    keys.sort(Qt::CaseInsensitive);
    for (const QString &key : keys) {
        const auto slice = m_slices.constFind(key);
        for (const Hit &hit : slice->hits) {
            if (hit.local)
                continue;
            SymbolLocation loc;
            loc.path = slice->path;
            loc.editorId = slice->editorId;
            loc.line = hit.line;
            loc.column = hit.column;
            const QString name = slice->ignoreCase ? hit.name.toLower() : hit.name;
            m_byName[name].append(loc);
        }
    }
}

void SymbolIndex::applyDisk(int generation, const QList<DiskFile> &files)
{
    if (generation != m_walkGeneration)
        return;
    QStringList drop;
    for (auto it = m_slices.cbegin(); it != m_slices.cend(); ++it) {
        if (it.value().editorId == 0)
            drop.append(it.key());
    }
    for (const QString &key : drop)
        m_slices.remove(key);
    for (const DiskFile &file : files) {
        if (bufferOwns(file.path))
            continue;
        if (file.hits.isEmpty() && file.modules.isEmpty() && file.defaultLine == 0)
            continue;
        Slice slice;
        slice.path = file.path;
        slice.ignoreCase = file.ignoreCase;
        fillSlice(slice, file.hits, file.modules, file.defaultName, file.defaultLine, file.defaultColumn);
        m_slices.insert(file.path, slice);
    }
    rebuild();
    emit updated();
}
