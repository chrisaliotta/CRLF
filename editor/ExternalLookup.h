#pragma once

#include "editor/SymbolLocation.h"

#include <QString>

#include <functional>

class LanguageRegistry;

// One external link that was clicked. node_modules is searched from the
// folder of `target.path` upward, or from `root` for an untitled document.
// `text` is the document, read for the packages a stylesheet name may come
// from. `cancelled` is polled between file reads so a newer click stops this one.
struct ExternalQuery {
    SymbolLocation target;
    QString root;
    QString text;
    std::function<bool()> cancelled;
};

// Runs on a worker thread. Reads package.json and a bounded number of entry
// or declaration files, never a whole package. line is zero when the name
// was not found.
SymbolLocation findExternal(const ExternalQuery &query, const LanguageRegistry *registry);
