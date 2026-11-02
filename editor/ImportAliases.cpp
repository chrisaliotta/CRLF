#include "editor/ImportAliases.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace {

QString absolutePath(const QString &path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool insideRoot(const QString &root, const QString &path)
{
    if (root.isEmpty() || path.isEmpty())
        return false;
    const QString normal = absolutePath(path);
    if (normal.compare(root, Qt::CaseInsensitive) == 0)
        return true;
    const QString slash = root + QLatin1Char('/');
    const QString back = root + QLatin1Char('\\');
    return normal.startsWith(slash, Qt::CaseInsensitive) || normal.startsWith(back, Qt::CaseInsensitive);
}

QByteArray stripJsonComments(const QByteArray &input)
{
    QByteArray out;
    out.reserve(input.size());
    bool string = false;
    bool escape = false;
    for (int i = 0; i < input.size(); ++i) {
        const char c = input.at(i);
        if (string) {
            out.append(c);
            if (escape)
                escape = false;
            else if (c == '\\')
                escape = true;
            else if (c == '"')
                string = false;
            continue;
        }
        if (c == '"') {
            string = true;
            out.append(c);
            continue;
        }
        if (c == '/' && i + 1 < input.size() && input.at(i + 1) == '/') {
            while (i < input.size() && input.at(i) != '\n')
                ++i;
            out.append('\n');
            continue;
        }
        if (c == '/' && i + 1 < input.size() && input.at(i + 1) == '*') {
            i += 2;
            while (i + 1 < input.size() && !(input.at(i) == '*' && input.at(i + 1) == '/'))
                ++i;
            if (i + 1 < input.size())
                ++i;
            continue;
        }
        out.append(c);
    }
    return QString::fromUtf8(out)
        .replace(QRegularExpression(QStringLiteral(",(\\s*[}\\]])")), QStringLiteral("\\1"))
        .toUtf8();
}

QString joinRest(const QString &directory, const QString &rest, const QString &suffix)
{
    QString path = directory;
    if (!rest.isEmpty())
        path = QDir(directory).filePath(rest);
    if (!suffix.isEmpty())
        path += suffix;
    return absolutePath(path);
}

bool matchAlias(const ImportAliases::Map &map, const QString &specifier, QString &rest)
{
    if (!map.wildcard)
        return specifier == map.prefix;
    if (map.prefix.isEmpty())
        return false;
    if (map.boundary) {
        const QString head = map.prefix + QLatin1Char('/');
        if (!specifier.startsWith(head))
            return false;
        rest = specifier.mid(head.size());
        return true;
    }
    if (!specifier.startsWith(map.prefix))
        return false;
    rest = specifier.mid(map.prefix.size());
    if (rest.startsWith(QLatin1Char('/')))
        rest = rest.mid(1);
    return true;
}

void addMap(QList<ImportAliases::Map> &maps, const QString &pattern, const QString &replacement,
            const QString &configDir, const QString &baseUrl, bool boundary, int rank)
{
    if (pattern.isEmpty() || replacement.isEmpty())
        return;
    const int star = pattern.indexOf(QLatin1Char('*'));
    ImportAliases::Map map;
    map.prefix = star < 0 ? pattern : pattern.left(star);
    while (map.prefix.endsWith(QLatin1Char('/')))
        map.prefix.chop(1);
    if (map.prefix.isEmpty())
        return;
    map.wildcard = star >= 0;
    map.boundary = boundary && map.wildcard;
    map.rank = rank;

    const int replStar = replacement.indexOf(QLatin1Char('*'));
    QString head = replStar < 0 ? replacement : replacement.left(replStar);
    map.suffix = replStar < 0 ? QString() : replacement.mid(replStar + 1);
    while (head.endsWith(QLatin1Char('/')) || head.endsWith(QLatin1Char('\\')))
        head.chop(1);
    const QString base = baseUrl.isEmpty() ? configDir : QDir(configDir).filePath(baseUrl);
    map.directory = absolutePath(QDir::isAbsolutePath(head) ? head : QDir(base).filePath(head));
    maps.append(map);
}

void readTsConfig(const QString &path, int depth, int rank, QSet<QString> &seen,
                  QList<ImportAliases::Map> &maps, QString &baseUrl)
{
    const QString normal = absolutePath(path);
    if (depth > 5 || normal.isEmpty() || seen.contains(normal))
        return;
    QFile file(normal);
    if (!file.open(QIODevice::ReadOnly))
        return;
    seen.insert(normal);
    const QJsonDocument doc = QJsonDocument::fromJson(stripJsonComments(file.readAll()));
    if (!doc.isObject())
        return;
    const QJsonObject root = doc.object();
    const QString configDir = QFileInfo(normal).absolutePath();
    const QString extends = root.value(QLatin1String("extends")).toString();
    if (extends.startsWith(QLatin1Char('.')))
        readTsConfig(QDir(configDir).filePath(extends), depth + 1, rank - 1, seen, maps, baseUrl);

    const QJsonObject options = root.value(QLatin1String("compilerOptions")).toObject();
    const QString localBase = options.value(QLatin1String("baseUrl")).toString();
    if (!localBase.isEmpty())
        baseUrl = absolutePath(QDir(configDir).filePath(localBase));
    const QString pathBase = baseUrl.isEmpty() ? configDir : baseUrl;
    const QJsonObject paths = options.value(QLatin1String("paths")).toObject();
    for (auto it = paths.constBegin(); it != paths.constEnd(); ++it) {
        for (const QJsonValue &value : it.value().toArray())
            addMap(maps, it.key(), value.toString(), configDir, pathBase, true, rank);
    }
}

void readViteConfig(const QString &path, int rank, QList<ImportAliases::Map> &maps)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QString text = QString::fromUtf8(file.readAll());
    const QString configDir = QFileInfo(path).absolutePath();
    const QRegularExpression pair(
        QStringLiteral("(['\"`])([^'\"`]+)\\1\\s*:\\s*(?:[A-Za-z0-9_.]+\\s*\\([^)]{0,240}?['\"`]([^'\"`]+)['\"`]|['\"`](\\.[^'\"`]+)['\"`])"));
    const QRegularExpression found(QStringLiteral(
        "find\\s*:\\s*(['\"`])([^'\"`]+)\\1[\\s\\S]{0,300}?replacement\\s*:\\s*(?:[A-Za-z0-9_.]+\\s*\\([^)]{0,240}?['\"`]([^'\"`]+)['\"`]|['\"`]([^'\"`]+)['\"`])"));

    const auto take = [&](const QRegularExpression &expression) {
        QRegularExpressionMatchIterator it = expression.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const QString key = match.captured(2);
            if (!key.startsWith(QLatin1Char('@')) && !key.startsWith(QLatin1Char('/')))
                continue;
            const QString replacement = match.captured(3).isEmpty() ? match.captured(4) : match.captured(3);
            if (replacement.isEmpty() || replacement.contains(QLatin1String("node_modules")))
                continue;
            QString pattern = key;
            if (!pattern.endsWith(QLatin1Char('*')))
                pattern += QLatin1Char('*');
            addMap(maps, pattern, replacement, configDir, QString(), false, rank);
        }
    };
    take(pair);
    take(found);
}

QStringList implicitTargets(const QString &root, const QString &specifier)
{
    QStringList tails;
    if (specifier.startsWith(QLatin1String("/@"))) {
        const QString body = specifier.mid(2);
        if (body.startsWith(QLatin1Char('/'))) {
            const QString rest = body.mid(1);
            tails.append(QStringLiteral("src/") + rest);
            tails.append(rest);
        } else {
            tails.append(body);
            tails.append(QStringLiteral("src/") + body);
        }
    } else if (specifier.startsWith(QLatin1String("@/"))) {
        const QString rest = specifier.mid(2);
        tails.append(QStringLiteral("src/") + rest);
        tails.append(rest);
    } else if (specifier.startsWith(QLatin1Char('@'))) {
        const QString body = specifier.mid(1);
        tails.append(body);
        tails.append(QStringLiteral("src/") + body);
    }
    QStringList paths;
    for (const QString &tail : tails) {
        if (!tail.isEmpty())
            paths.append(absolutePath(QDir(root).filePath(tail)));
    }
    return paths;
}

} // namespace

ImportAliases importAliasesFor(const QString &projectRoot, const QString &fromFile)
{
    ImportAliases aliases;
    const QString fileDir = absolutePath(QFileInfo(fromFile).absolutePath());
    QString root = absolutePath(projectRoot);
    if (!root.isEmpty() && !insideRoot(root, fileDir) && root.compare(fileDir, Qt::CaseInsensitive) != 0)
        root.clear();

    if (root.isEmpty()) {
        QString dir = fileDir;
        QString found;
        while (!dir.isEmpty()) {
            const QDir folder(dir);
            if (folder.exists(QStringLiteral("package.json")) || folder.exists(QStringLiteral("tsconfig.json"))
                || folder.exists(QStringLiteral("jsconfig.json")) || folder.exists(QStringLiteral("vite.config.ts"))
                || folder.exists(QStringLiteral("vite.config.js")))
                found = dir;
            const QString parent = absolutePath(QFileInfo(dir).dir().absolutePath());
            if (parent.compare(dir, Qt::CaseInsensitive) == 0)
                break;
            dir = parent;
        }
        root = found.isEmpty() ? fileDir : found;
    }
    aliases.root = root;

    QSet<QString> seen;
    int rank = 1000;
    QString dir = fileDir;
    while (!dir.isEmpty()) {
        const QStringList configs =
            QDir(dir).entryList({QStringLiteral("tsconfig*.json"), QStringLiteral("jsconfig.json")}, QDir::Files);
        for (const QString &name : configs)
            readTsConfig(QDir(dir).filePath(name), 0, rank, seen, aliases.maps, aliases.baseUrl);
        const QStringList viteNames = {QStringLiteral("vite.config.ts"),  QStringLiteral("vite.config.js"),
                                       QStringLiteral("vite.config.mjs"), QStringLiteral("vite.config.mts"),
                                       QStringLiteral("vite.config.cjs")};
        for (const QString &name : viteNames)
            readViteConfig(QDir(dir).filePath(name), rank, aliases.maps);
        if (dir.compare(root, Qt::CaseInsensitive) == 0)
            break;
        const QString parent = absolutePath(QFileInfo(dir).dir().absolutePath());
        if (parent.compare(dir, Qt::CaseInsensitive) == 0)
            break;
        dir = parent;
        rank -= 10;
    }

    std::stable_sort(aliases.maps.begin(), aliases.maps.end(),
                     [](const ImportAliases::Map &left, const ImportAliases::Map &right) {
                         if (left.prefix.size() != right.prefix.size())
                             return left.prefix.size() > right.prefix.size();
                         return left.rank > right.rank;
                     });
    return aliases;
}

QStringList aliasTargets(const ImportAliases &aliases, const QString &specifier)
{
    if (specifier.isEmpty() || specifier.startsWith(QLatin1Char('.')))
        return {};

    QStringList hits;
    int bestLength = -1;
    int bestRank = -1;
    for (const ImportAliases::Map &map : aliases.maps) {
        QString rest;
        if (!matchAlias(map, specifier, rest))
            continue;
        if (bestLength < 0) {
            bestLength = map.prefix.size();
            bestRank = map.rank;
        }
        if (map.prefix.size() != bestLength || map.rank != bestRank)
            break;
        hits.append(joinRest(map.directory, rest, map.suffix));
    }
    if (!hits.isEmpty())
        return hits;

    if (!aliases.root.isEmpty()
        && (specifier.startsWith(QLatin1Char('@')) || specifier.startsWith(QLatin1Char('/')))) {
        const QStringList guessed = implicitTargets(aliases.root, specifier);
        if (!guessed.isEmpty())
            return guessed;
    }

    if (!aliases.baseUrl.isEmpty() && !specifier.startsWith(QLatin1Char('/'))
        && !specifier.startsWith(QLatin1Char('@')))
        return {absolutePath(QDir(aliases.baseUrl).filePath(specifier))};
    return {};
}
