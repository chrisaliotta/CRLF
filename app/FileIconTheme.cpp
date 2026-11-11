#include "app/FileIconTheme.h"

#include "io/NamePattern.h"
#include "syntax/LanguageRegistry.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIconEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRegularExpression>
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

bool wildcard(const QString &pattern)
{
    return pattern.contains(QLatin1Char('*')) || pattern.contains(QLatin1Char('?'));
}

bool extensionMatches(const QString &pattern, const QString &fileName)
{
    if (namePatternMatches(pattern, fileName))
        return true;
    int dot = fileName.indexOf(QLatin1Char('.'));
    while (dot >= 0 && dot + 1 < fileName.size()) {
        if (namePatternMatches(pattern, fileName.mid(dot + 1)))
            return true;
        dot = fileName.indexOf(QLatin1Char('.'), dot + 1);
    }
    return false;
}

// The item view asks for whatever rectangle it has reserved. QIcon's default
// engine then scales the pixmap to that rectangle and ignores the aspect
// ratio. Material artwork fills its view box, so that scale reads as a stretch.
class AspectIconEngine : public QIconEngine
{
public:
    explicit AspectIconEngine(const QPixmap &pixmap)
        : m_pixmap(pixmap)
    {
    }

    QIconEngine *clone() const override { return new AspectIconEngine(*this); }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode, QIcon::State, qreal scale) override
    {
        const QSize logical = logicalSize();
        const QSize fitted = size.isEmpty() ? logical : logical.scaled(size, Qt::KeepAspectRatio);
        if (fitted == logical && qFuzzyCompare(scale, m_pixmap.devicePixelRatio()))
            return m_pixmap;
        return m_pixmap.scaled((QSizeF(fitted) * scale).toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        const QPixmap pixmap = scaledPixmap(rect.size(), mode, state, painter->device()->devicePixelRatio());
        const QSize drawn = logicalSizeOf(pixmap);
        const QRect dest(rect.x() + (rect.width() - drawn.width()) / 2,
                         rect.y() + (rect.height() - drawn.height()) / 2,
                         drawn.width(), drawn.height());
        painter->drawPixmap(dest, pixmap);
    }

private:
    QSize logicalSize() const { return logicalSizeOf(m_pixmap); }

    static QSize logicalSizeOf(const QPixmap &pixmap)
    {
        const qreal ratio = pixmap.devicePixelRatio();
        return QSize(qRound(pixmap.width() / ratio), qRound(pixmap.height() / ratio));
    }

    QPixmap m_pixmap;
};

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
        for (const PatternRule &rule : m_patterns) {
            if (rule.kind != QLatin1String("folder") || !namePatternMatches(rule.pattern, lower))
                continue;
            if (expanded && !rule.openId.isEmpty())
                return rule.openId;
            return rule.closedId;
        }
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
    for (const PatternRule &rule : m_patterns) {
        if (rule.kind == QLatin1String("file") && namePatternMatches(rule.pattern, lower))
            return rule.closedId;
    }
    const QString named = m_fileNames.value(lower);
    if (!named.isEmpty())
        return named;
    for (const PatternRule &rule : m_patterns) {
        if (rule.kind == QLatin1String("extension") && extensionMatches(rule.pattern, lower))
            return rule.closedId;
    }
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

void FileIconTheme::appendIcons(QList<IconOption> &rows, const QString &kind,
                                const QHash<QString, QString> &map) const
{
    QStringList keys = map.keys();
    keys.sort(Qt::CaseInsensitive);
    for (const QString &key : keys)
        rows.append({key, kind, iconForId(map.value(key))});
}

QList<IconOption> FileIconTheme::icons() const
{
    QList<IconOption> rows;
    appendIcons(rows, QStringLiteral("folder"), m_folderNames);
    appendIcons(rows, QStringLiteral("folder open"), m_folderNamesExpanded);
    appendIcons(rows, QStringLiteral("file name"), m_fileNames);
    appendIcons(rows, QStringLiteral("extension"), m_fileExtensions);
    appendIcons(rows, QStringLiteral("language"), m_languageIds);
    if (!m_folder.isEmpty())
        rows.append({QStringLiteral("folder"), QStringLiteral("default"), iconForId(m_folder)});
    if (!m_folderExpanded.isEmpty())
        rows.append({QStringLiteral("folderExpanded"), QStringLiteral("default"), iconForId(m_folderExpanded)});
    if (!m_file.isEmpty())
        rows.append({QStringLiteral("file"), QStringLiteral("default"), iconForId(m_file)});
    return rows;
}

QList<IconOption> FileIconTheme::glyphs() const
{
    QHash<QString, QString> keyForId;
    QHash<QString, QString> kindForId;
    const auto consider = [&](const QHash<QString, QString> &map, const QString &kind) {
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            const QString existing = keyForId.value(it.value());
            if (existing.isEmpty() || it.key().size() < existing.size()) {
                keyForId.insert(it.value(), it.key());
                kindForId.insert(it.value(), kind);
            }
        }
    };
    consider(m_fileExtensions, QStringLiteral("extension"));
    consider(m_languageIds, QStringLiteral("language"));
    consider(m_fileNames, QStringLiteral("file name"));
    consider(m_folderNames, QStringLiteral("folder"));
    consider(m_folderNamesExpanded, QStringLiteral("folder open"));
    if (!m_file.isEmpty() && !keyForId.contains(m_file)) {
        keyForId.insert(m_file, QStringLiteral("file"));
        kindForId.insert(m_file, QStringLiteral("default"));
    }
    if (!m_folder.isEmpty() && !keyForId.contains(m_folder)) {
        keyForId.insert(m_folder, QStringLiteral("folder"));
        kindForId.insert(m_folder, QStringLiteral("default"));
    }
    if (!m_folderExpanded.isEmpty() && !keyForId.contains(m_folderExpanded)) {
        keyForId.insert(m_folderExpanded, QStringLiteral("folderExpanded"));
        kindForId.insert(m_folderExpanded, QStringLiteral("default"));
    }

    QStringList ids = keyForId.keys();
    ids.sort(Qt::CaseInsensitive);
    QList<IconOption> rows;
    for (const QString &id : ids)
        rows.append({keyForId.value(id), kindForId.value(id), iconForId(id)});
    return rows;
}

QString FileIconTheme::catalogId(const QString &key) const
{
    const QString lower = key.toLower();
    const QHash<QString, QString> *maps[] = {
        &m_fileNames, &m_fileExtensions, &m_languageIds, &m_folderNames, &m_folderNamesExpanded,
    };
    for (const QHash<QString, QString> *map : maps) {
        const auto it = map->constFind(lower);
        if (it != map->constEnd())
            return it.value();
    }
    if (lower == QLatin1String("folder"))
        return m_folder;
    if (lower == QLatin1String("folderexpanded"))
        return m_folderExpanded;
    if (lower == QLatin1String("file"))
        return m_file;
    return {};
}

QString FileIconTheme::copyGlyph(FileIconTheme *theme, const FileIconTheme &source, const QString &key, int *index) const
{
    const QString sourceId = source.catalogId(key);
    const auto definition = source.m_definitions.constFind(sourceId);
    if (sourceId.isEmpty() || definition == source.m_definitions.constEnd())
        return {};
    const QString id = QStringLiteral("_project_%1").arg((*index)++);
    theme->m_definitions.insert(id, definition.value());
    return id;
}

FileIconTheme FileIconTheme::compose(const FileIconTheme &base, const QList<IconRule> &rules,
                                     const QList<FileIconTheme> &installed)
{
    FileIconTheme theme = base;
    theme.m_cache.clear();
    theme.m_patterns.clear();
    int index = 0;
    for (const IconRule &rule : rules) {
        const FileIconTheme *source = nullptr;
        for (const FileIconTheme &candidate : installed) {
            if (candidate.name == rule.from) {
                source = &candidate;
                break;
            }
        }
        if (source == nullptr)
            continue;
        const QString closedId = theme.copyGlyph(&theme, *source, rule.icon, &index);
        if (closedId.isEmpty())
            continue;
        QString openId;
        if (!rule.iconOpen.isEmpty())
            openId = theme.copyGlyph(&theme, *source, rule.iconOpen, &index);

        const QString key = rule.pattern.toLower();
        if (wildcard(rule.pattern)) {
            theme.m_patterns.append({rule.kind, rule.pattern, closedId, openId});
            continue;
        }
        if (rule.kind == QLatin1String("extension"))
            theme.m_fileExtensions.insert(key, closedId);
        else if (rule.kind == QLatin1String("file"))
            theme.m_fileNames.insert(key, closedId);
        else if (rule.kind == QLatin1String("folder")) {
            theme.m_folderNames.insert(key, closedId);
            if (!openId.isEmpty())
                theme.m_folderNamesExpanded.insert(key, openId);
        } else if (rule.kind == QLatin1String("language"))
            theme.m_languageIds.insert(key, closedId);
        else if (rule.kind == QLatin1String("default") && key == QLatin1String("file"))
            theme.m_file = closedId;
        else if (rule.kind == QLatin1String("default") && key == QLatin1String("folder"))
            theme.m_folder = closedId;
        else if (rule.kind == QLatin1String("default") && key == QLatin1String("folderexpanded"))
            theme.m_folderExpanded = closedId;
    }
    return theme;
}

QIcon FileIconTheme::iconFor(const QString &fileName, bool isDir, bool expanded) const
{
    return iconForId(definitionId(fileName, isDir, expanded));
}

QIcon FileIconTheme::iconForId(const QString &id) const
{
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
    return QIcon(new AspectIconEngine(pixmap));
}
