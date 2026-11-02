#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Directory aliases for a module specifier such as `/@utils/myutil` or
// `@utils/myutil`. Maps come from tsconfig/jsconfig `paths` and from a Vite
// config's `resolve.alias`. `root` is the project directory those files were
// read from.
struct ImportAliases {
    struct Map {
        QString prefix;
        QString directory;
        QString suffix;
        bool wildcard = false;
        // A tsconfig `*` pattern only matches at a `/` boundary. A Vite alias
        // matches any specifier that begins with the prefix.
        bool boundary = false;
        int rank = 0;
    };

    QString root;
    QString baseUrl;
    QList<Map> maps;
};

// `projectRoot` is the scoped folder when there is one. Config files are read
// from `fromFile` upward, and stop at that folder.
ImportAliases importAliasesFor(const QString &projectRoot, const QString &fromFile);

// Absolute files or directories the specifier can name, before an extension
// is added. Empty for a relative specifier or a package that is not an alias.
QStringList aliasTargets(const ImportAliases &aliases, const QString &specifier);
