#pragma once

#include <QList>
#include <QString>

class LanguageDefinition;

// One defined name. line and column are 1-based and point at the first
// character of the name.
struct SymbolHit {
    QString name;
    int line = 1;
    int column = 1;
    // True for a variable that belongs to this document, not the project index.
    bool local = false;
};

// A name tied to a module specifier. `imported` is empty for a default
// import or export. `reexport` is an `export ... from`. `star` is `export *`.
// `whole` is `import * as local`, a name for the entire module.
struct ModuleBinding {
    QString local;
    QString imported;
    QString specifier;
    bool reexport = false;
    bool star = false;
    bool whole = false;
};

// Declarations, plus import and export bindings when the language tracks
// modules. defaultLine is zero when the file has no default export.
struct SymbolScan {
    QList<SymbolHit> hits;
    QList<ModuleBinding> modules;
    QString defaultName;
    int defaultLine = 0;
    int defaultColumn = 0;
};

// The text is a copy; this does not touch a document. Comments and strings
// are not declarations. Import specifiers are read from string literals.
// `includeLocals` is set for the document being edited. A project walk leaves
// it clear so local variables stay out of the project index.
SymbolScan scanSource(const QString &text, const LanguageDefinition *definition,
                      bool includeLocals = true);
