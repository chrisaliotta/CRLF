#include "app/FileIconTheme.h"

#include "syntax/LanguageRegistry.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QScreen>
#include <QSvgRenderer>

namespace {

QString glyphFrom(const QString &text)
{
    if (text.size() == 1)
        return text;
    int hexStart = -1;
    if (text.startsWith(QLatin1String("\\u")))
        hexStart = 2;
    else if (text.startsWith(QLatin1Char('\\')))
        hexStart = 1;
    if (hexStart > 0) {
        bool ok = false;
        const uint code = text.mid(hexStart).toUInt(&ok, 16);
        if (ok && code > 0)
            return QString(QChar(code));
    }
    return text;
}

QHash<QString, QString> associationMap(const QJsonObject &object)
{
    QHash<QString, QString> map;
    for (auto it = object.begin(); it != object.end(); ++it)
        map.insert(it.key().toLower(), it.value().toString());
    return map;
}

// VS Code language ids, shared by every icon theme. Themes map these ids in
// languageIds; the extension and file name lists are the host's language
// contributions, which an icon theme does not carry.
struct LanguageAssociations {
    QHash<QString, QString> fileNames;
    QHash<QString, QString> fileExtensions;
    QHash<QString, QString> fileNamePrefixes;
};

const LanguageAssociations &languageAssociations()
{
    static const LanguageAssociations associations = [] {
        LanguageAssociations loaded;
        QFile file(QDir(programDirectory()).filePath(QStringLiteral("icons/languages.json")));
        (void)file.open(QIODevice::ReadOnly);
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        loaded.fileNames = associationMap(root.value(QLatin1String("fileNames")).toObject());
        loaded.fileExtensions = associationMap(root.value(QLatin1String("fileExtensions")).toObject());
        loaded.fileNamePrefixes = associationMap(root.value(QLatin1String("fileNamePrefixes")).toObject());
        return loaded;
    }();
    return associations;
}

QString languageIdFor(const QString &fileName)
{
    const LanguageAssociations &languages = languageAssociations();
    const QString named = languages.fileNames.value(fileName);
    if (!named.isEmpty())
        return named;

    QString prefixId;
    int prefixLength = -1;
    for (auto it = languages.fileNamePrefixes.cbegin(); it != languages.fileNamePrefixes.cend(); ++it) {
        if (fileName.startsWith(it.key()) && it.key().size() > prefixLength) {
            prefixLength = it.key().size();
            prefixId = it.value();
        }
    }
    if (!prefixId.isEmpty())
        return prefixId;

    int dot = fileName.indexOf(QLatin1Char('.'));
    while (dot >= 0 && dot + 1 < fileName.size()) {
        const QString id = languages.fileExtensions.value(fileName.mid(dot + 1));
        if (!id.isEmpty())
            return id;
        dot = fileName.indexOf(QLatin1Char('.'), dot + 1);
    }
    return {};
}

QString loadFont(const QString &path)
{
    const int id = QFontDatabase::addApplicationFont(path);
    if (id < 0)
        return {};
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    return families.isEmpty() ? QString() : families.first();
}

} // namespace

QList<FileIconTheme> FileIconTheme::loadInstalled()
{
    QList<FileIconTheme> themes;
    const auto add = [&themes](FileIconTheme theme) {
        if (theme.name.isEmpty() || theme.m_definitions.isEmpty())
            return;
        for (int i = 0; i < themes.size(); ++i) {
            if (themes.at(i).name == theme.name) {
                themes[i] = theme;
                return;
            }
        }
        themes.append(theme);
    };

    const QDir root(QDir(programDirectory()).filePath(QStringLiteral("icons")));
    const QStringList folders = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &folder : folders) {
        const QDir dir(root.filePath(folder));
        const QStringList jsons = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
        for (const QString &json : jsons) {
            FileIconTheme theme = fromJsonFile(dir.filePath(json));
            if (theme.name.isEmpty())
                theme.name = jsons.size() == 1 ? folder : QFileInfo(json).completeBaseName();
            add(theme);
        }
    }
    return themes;
}

FileIconTheme FileIconTheme::fromJsonFile(const QString &path)
{
    QFile file(path);
    (void)file.open(QIODevice::ReadOnly);
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QDir dir = QFileInfo(path).dir();

    FileIconTheme theme;
    theme.name = root.value(QLatin1String("name")).toString();
    theme.m_file = root.value(QLatin1String("file")).toString();
    theme.m_folder = root.value(QLatin1String("folder")).toString();
    theme.m_folderExpanded = root.value(QLatin1String("folderExpanded")).toString();
    theme.m_fileNames = associationMap(root.value(QLatin1String("fileNames")).toObject());
    theme.m_fileExtensions = associationMap(root.value(QLatin1String("fileExtensions")).toObject());
    theme.m_languageIds = associationMap(root.value(QLatin1String("languageIds")).toObject());
    theme.m_folderNames = associationMap(root.value(QLatin1String("folderNames")).toObject());
    theme.m_folderNamesExpanded = associationMap(root.value(QLatin1String("folderNamesExpanded")).toObject());

    QHash<QString, QString> families;
    QString defaultFamily;
    const QJsonArray fonts = root.value(QLatin1String("fonts")).toArray();
    for (const QJsonValue &value : fonts) {
        const QJsonObject font = value.toObject();
        const QString id = font.value(QLatin1String("id")).toString();
        const QString size = font.value(QLatin1String("size")).toString();
        if (size.endsWith(QLatin1Char('%')))
            theme.m_fontPercent = size.chopped(1).toInt();
        QString family;
        for (const QJsonValue &source : font.value(QLatin1String("src")).toArray()) {
            const QString relative = source.toObject().value(QLatin1String("path")).toString();
            if (relative.isEmpty())
                continue;
            family = loadFont(QDir::cleanPath(dir.filePath(relative)));
            if (!family.isEmpty())
                break;
        }
        if (family.isEmpty())
            continue;
        if (!id.isEmpty())
            families.insert(id, family);
        if (defaultFamily.isEmpty())
            defaultFamily = family;
    }
    if (theme.m_fontPercent <= 0)
        theme.m_fontPercent = 100;

    const QJsonObject definitions = root.value(QLatin1String("iconDefinitions")).toObject();
    for (auto it = definitions.begin(); it != definitions.end(); ++it) {
        const QJsonObject spec = it.value().toObject();
        Definition definition;
        const QString iconPath = spec.value(QLatin1String("iconPath")).toString();
        if (!iconPath.isEmpty())
            definition.iconPath = QDir::cleanPath(dir.filePath(iconPath));
        const QString character = glyphFrom(spec.value(QLatin1String("fontCharacter")).toString());
        if (!character.isEmpty()) {
            const QString fontId = spec.value(QLatin1String("fontId")).toString();
            definition.fontFamily = fontId.isEmpty() ? defaultFamily : families.value(fontId, defaultFamily);
            definition.character = character;
            definition.color = QColor(spec.value(QLatin1String("fontColor")).toString());
            definition.font = !definition.fontFamily.isEmpty();
        }
        if (definition.iconPath.isEmpty() && !definition.font)
            continue;
        theme.m_definitions.insert(it.key(), definition);
    }
    return theme;
}

QString FileIconTheme::definitionId(const QString &fileName, bool isDir, bool expanded) const
{
    const QString lower = fileName.toLower();
    if (isDir) {
        if (expanded) {
            const QString namedOpen = m_folderNamesExpanded.value(lower);
            if (!namedOpen.isEmpty())
                return namedOpen;
            if (!m_folderExpanded.isEmpty() && !m_folderNames.contains(lower))
                return m_folderExpanded;
        }
        const QString named = m_folderNames.value(lower);
        return named.isEmpty() ? m_folder : named;
    }
    const QString named = m_fileNames.value(lower);
    if (!named.isEmpty())
        return named;
    int dot = lower.indexOf(QLatin1Char('.'));
    while (dot >= 0 && dot + 1 < lower.size()) {
        const QString extension = m_fileExtensions.value(lower.mid(dot + 1));
        if (!extension.isEmpty())
            return extension;
        dot = lower.indexOf(QLatin1Char('.'), dot + 1);
    }
    const QString language = m_languageIds.value(languageIdFor(lower));
    return language.isEmpty() ? m_file : language;
}

QIcon FileIconTheme::iconFor(const QString &fileName, bool isDir, bool expanded) const
{
    const QString id = definitionId(fileName, isDir, expanded);
    if (m_cache.contains(id))
        return m_cache.value(id);
    const auto definition = m_definitions.constFind(id);
    if (definition == m_definitions.constEnd())
        return {};
    const QIcon icon = render(definition.value());
    m_cache.insert(id, icon);
    return icon;
}

QIcon FileIconTheme::render(const Definition &definition) const
{
    qreal dpr = 1;
    if (const QScreen *screen = QGuiApplication::primaryScreen())
        dpr = screen->devicePixelRatio();
    constexpr int side = 16;
    QPixmap pixmap(qRound(side * dpr), qRound(side * dpr));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    if (!definition.iconPath.isEmpty()) {
        if (definition.iconPath.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
            QSvgRenderer renderer(definition.iconPath);
            renderer.setAspectRatioMode(Qt::KeepAspectRatio);
            renderer.render(&painter, QRectF(0, 0, side, side));
        } else {
            const QPixmap image(definition.iconPath);
            painter.drawPixmap(QRect(0, 0, side, side), image);
        }
    } else if (definition.font) {
        QFont font(definition.fontFamily);
        font.setPixelSize(side * m_fontPercent / 100);
        font.setStyleStrategy(QFont::NoFontMerging);
        painter.setFont(font);
        painter.setPen(definition.color);
        painter.setClipRect(QRectF(0, 0, side, side));
        painter.drawText(QRectF(0, 0, side, side), Qt::AlignCenter, definition.character);
    }
    return QIcon(pixmap);
}
