#include "syntax/LanguageRegistry.h"

#include <QDir>
#include <QFileInfo>

LanguageRegistry::LanguageRegistry()
{
    const QDir dir(QStringLiteral(":/languages"));
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &file : files) {
        const LanguageDefinition def = LanguageDefinition::fromJsonFile(dir.filePath(file));
        const int index = m_definitions.size();
        m_definitions.append(def);
        for (const QString &extension : def.extensions)
            m_byExtension.insert(extension, index);
        if (def.name == QLatin1String("Plain Text"))
            m_plainTextIndex = index;
    }
}

const LanguageDefinition *LanguageRegistry::definitionForPath(const QString &path) const
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return &m_definitions.at(m_byExtension.value(suffix, m_plainTextIndex));
}

const LanguageDefinition *LanguageRegistry::definitionForName(const QString &name) const
{
    for (const LanguageDefinition &def : m_definitions) {
        if (def.name == name)
            return &def;
    }
    return &m_definitions.at(m_plainTextIndex);
}

const LanguageDefinition *LanguageRegistry::definitionForFenceInfo(const QString &info) const
{
    static const QHash<QString, QString> aliases = {
        {QStringLiteral("c++"), QStringLiteral("cpp")},
        {QStringLiteral("c"), QStringLiteral("cpp")},
        {QStringLiteral("c#"), QStringLiteral("cs")},
        {QStringLiteral("csharp"), QStringLiteral("cs")},
        {QStringLiteral("python3"), QStringLiteral("py")},
        {QStringLiteral("js"), QStringLiteral("json")},
        {QStringLiteral("jsonc"), QStringLiteral("json")},
        {QStringLiteral("text"), QStringLiteral("txt")},
        {QStringLiteral("plaintext"), QStringLiteral("txt")},
    };
    const QString key = info.trimmed().toLower();
    if (m_byExtension.contains(key))
        return &m_definitions.at(m_byExtension.value(key));
    for (const LanguageDefinition &def : m_definitions) {
        if (def.name.toLower() == key)
            return &def;
    }
    return &m_definitions.at(m_byExtension.value(aliases.value(key), m_plainTextIndex));
}

int LanguageRegistry::indexOf(const LanguageDefinition *definition) const
{
    return int(definition - m_definitions.constData());
}
