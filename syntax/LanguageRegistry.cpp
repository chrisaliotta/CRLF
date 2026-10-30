#include "syntax/LanguageRegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

QString programDirectory()
{
    QDir dir(QCoreApplication::applicationDirPath());
    if (dir.dirName().compare(QLatin1String("bin"), Qt::CaseInsensitive) == 0)
        dir.cdUp();
    return dir.path();
}

namespace {

void addDefinition(QList<LanguageDefinition> &definitions, QHash<QString, int> &byExtension,
                   int &plainTextIndex, const LanguageDefinition &def)
{
    int index = -1;
    for (int i = 0; i < definitions.size(); ++i) {
        if (definitions.at(i).name == def.name) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        index = definitions.size();
        definitions.append(def);
    } else {
        for (auto it = byExtension.begin(); it != byExtension.end();) {
            if (it.value() == index)
                it = byExtension.erase(it);
            else
                ++it;
        }
        definitions[index] = def;
    }
    for (const QString &extension : def.extensions)
        byExtension.insert(extension, index);
    if (def.name == QLatin1String("Plain Text"))
        plainTextIndex = index;
}

void loadDirectory(QList<LanguageDefinition> &definitions, QHash<QString, int> &byExtension,
                   int &plainTextIndex, const QString &path)
{
    const QDir dir(path);
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &file : files)
        addDefinition(definitions, byExtension, plainTextIndex,
                      LanguageDefinition::fromJsonFile(dir.filePath(file)));
}

} // namespace

LanguageRegistry::LanguageRegistry()
{
    loadDirectory(m_definitions, m_byExtension, m_plainTextIndex,
                  QDir(programDirectory()).filePath(QStringLiteral("styles")));
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
        {QStringLiteral("javascript"), QStringLiteral("ts")},
        {QStringLiteral("jsonc"), QStringLiteral("json")},
        {QStringLiteral("tailwindcss"), QStringLiteral("tailwind")},
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
