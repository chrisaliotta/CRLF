#pragma once

#include "syntax/LanguageDefinition.h"

#include <QHash>
#include <QList>

// Loads every language JSON bundled under :/languages and resolves a file
// path to its definition by extension. Unknown extensions map to Plain Text.
class LanguageRegistry
{
public:
    LanguageRegistry();

    const LanguageDefinition *definitionForPath(const QString &path) const;
    // Lookup by display name, used to restore a manually chosen language.
    const LanguageDefinition *definitionForName(const QString &name) const;
    // Every bundled language, for menus that let the user pick one manually.
    const QList<LanguageDefinition> &definitions() const { return m_definitions; }

private:
    QList<LanguageDefinition> m_definitions;
    QHash<QString, int> m_byExtension;
    int m_plainTextIndex = 0;
};
