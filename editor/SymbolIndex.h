#pragma once

#include "editor/ImportAliases.h"
#include "syntax/SymbolScanner.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class LanguageDefinition;
class LanguageRegistry;

// Where a defined name lives. line and column are 1-based. path is empty for
// an untitled buffer, and editorId is zero when the hit came from a file that
// is not open. line is zero when a lookup found nothing.
struct SymbolLocation {
    QString path;
    quintptr editorId = 0;
    int line = 0;
    int column = 0;
};

// Names defined in open buffers and, when a folder is scoped, in that folder.
// The map is read on the UI thread. Scans run on the thread pool.
class SymbolIndex : public QObject
{
    Q_OBJECT

public:
    explicit SymbolIndex(const LanguageRegistry *registry, QObject *parent = nullptr);

    // Replaces the on-disk index. An empty root drops every file that is not
    // an open buffer. A repeat of the same root and hide list does nothing.
    // While disabled, the root is remembered and nothing is scanned.
    void setProject(const QString &root, const QStringList &hide);
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    // Rescans one editor. An open buffer replaces the disk slice of its path.
    void updateBuffer(quintptr editorId, const QString &path, const QString &text,
                      const LanguageDefinition *language);
    // Drops the buffer. A saved path inside the project is read from disk.
    void removeBuffer(quintptr editorId);

    // An import in the current file wins. A relative specifier, a tsconfig or
    // Vite directory alias, or a `/@dir` path is resolved to that module's
    // export. Otherwise prefers a hit in the current file,
    // then any other hit. The token at line/column is skipped, so the
    // definition under the pointer is not a link to itself when it is the
    // only hit.
    SymbolLocation find(const QString &name, bool ignoreCase, const QString &currentPath,
                        quintptr editorId, int line, int column) const;

signals:
    void updated();

private:
    struct Hit {
        QString name;
        int line = 1;
        int column = 1;
        bool local = false;
    };

    struct Slice {
        QString path;
        quintptr editorId = 0;
        bool ignoreCase = false;
        QList<Hit> hits;
        QList<ModuleBinding> modules;
        QString defaultName;
        int defaultLine = 0;
        int defaultColumn = 0;
    };

    struct DiskFile {
        QString path;
        bool ignoreCase = false;
        QList<Hit> hits;
        QList<ModuleBinding> modules;
        QString defaultName;
        int defaultLine = 0;
        int defaultColumn = 0;
    };

    QString sliceKey(quintptr editorId, const QString &path) const;
    bool bufferOwns(const QString &path) const;
    bool insideProject(const QString &path) const;
    const Slice *sliceForEditor(quintptr editorId) const;
    const Slice *sliceForPath(const QString &path) const;
    // `matched` is set when the name is an import in this file, including a
    // package import that has no project file to open.
    SymbolLocation resolveImport(const Slice &slice, const QString &name, bool &matched) const;
    QString resolveSpecifier(const QString &fromFile, const QString &specifier) const;
    QString indexedModule(const QString &base) const;
    const ImportAliases &aliasesFor(const QString &fromFile) const;
    // skipLine/skipColumn ignore the token under the pointer. Zero skips nothing.
    SymbolLocation hitLocation(const Slice &slice, const QString &name, int skipLine = 0,
                              int skipColumn = 0) const;
    SymbolLocation lookupExport(const QString &path, const QString &imported, int depth) const;
    static void fillSlice(Slice &slice, const QList<Hit> &hits, const QList<ModuleBinding> &modules,
                          const QString &defaultName, int defaultLine, int defaultColumn);
    static bool namesEqual(const QString &left, const QString &right, bool ignoreCase);
    void releaseKey(const QString &key);
    void scheduleDiskFile(const QString &path);
    void rebuild();
    void applyDisk(int generation, const QList<DiskFile> &files);

    const LanguageRegistry *m_registry;
    bool m_enabled = true;
    QString m_root;
    QStringList m_hide;
    int m_walkGeneration = 0;
    QHash<QString, int> m_fileGeneration;
    QHash<quintptr, int> m_bufferGeneration;
    // Editor to the slice key currently published for it.
    QHash<quintptr, QString> m_editors;
    QHash<QString, Slice> m_slices;
    QHash<QString, QVector<SymbolLocation>> m_byName;
    mutable QString m_aliasKey;
    mutable ImportAliases m_importAliases;
};
