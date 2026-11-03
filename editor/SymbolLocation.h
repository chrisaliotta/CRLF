#pragma once

#include <QString>

// Package: `name` exported by the `specifier` import. An empty name with
// `defaultExport` is the default export; `wholeModule` is the module itself.
// Stylesheet: `name` is a key such as `.btn` or `$gap`, defined by a package
// the document imports. Global: `name` is an ambient declaration such as
// `HTMLDivElement`, inside namespace `container` when that is set.
enum class ExternalKind { Package, Stylesheet, Global };

// Where a defined name lives. line and column are 1-based. path is empty for
// an untitled buffer, and editorId is zero when the hit came from a file that
// is not open. line is zero when a lookup found nothing.
//
// An external location has no line yet. It is resolved under node_modules
// when the link is clicked. path is the file the name was used in.
struct SymbolLocation {
    QString path;
    quintptr editorId = 0;
    int line = 0;
    int column = 0;
    bool external = false;
    ExternalKind kind = ExternalKind::Package;
    QString specifier;
    QString name;
    QString container;
    bool defaultExport = false;
    bool wholeModule = false;
};
