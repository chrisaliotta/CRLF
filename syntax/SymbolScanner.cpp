#include "syntax/SymbolScanner.h"

#include "syntax/LanguageDefinition.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringView>
#include <QVector>

namespace {

bool startsAt(const QString &text, int pos, const QString &token)
{
    return !token.isEmpty() && QStringView(text).mid(pos).startsWith(token);
}

void blankRange(QString &text, int from, int to)
{
    for (int i = from; i < to; ++i) {
        if (text.at(i) != QLatin1Char('\n'))
            text[i] = QLatin1Char(' ');
    }
}

// Spaces out comments, and strings when `blankStrings` is set, without moving
// any other character, so a match offset is still a position in the original
// text. Module specifiers live inside quotes, so that pass leaves strings
// intact and only clears comments.
QString blankNonCode(const QString &text, const LanguageDefinition *def, bool blankStrings)
{
    QString out = text;
    const int length = text.size();
    int pos = 0;
    bool inBlock = false;
    int stringRule = -1;

    while (pos < length) {
        if (inBlock) {
            const int end = text.indexOf(def->blockCommentEnd, pos);
            if (end < 0) {
                blankRange(out, pos, length);
                return out;
            }
            const int close = end + def->blockCommentEnd.size();
            blankRange(out, pos, close);
            pos = close;
            inBlock = false;
            continue;
        }

        if (stringRule >= 0) {
            const StringRule &rule = def->strings.at(stringRule);
            int scan = pos;
            int close = -1;
            while (scan < length) {
                if (startsAt(text, scan, rule.escape)) {
                    scan += rule.escape.size() + 1;
                    continue;
                }
                if (startsAt(text, scan, rule.end)) {
                    close = scan + rule.end.size();
                    break;
                }
                if (!rule.multiline && text.at(scan) == QLatin1Char('\n'))
                    break;
                ++scan;
            }
            if (close < 0) {
                if (blankStrings)
                    blankRange(out, pos, rule.multiline ? length : scan);
                if (rule.multiline)
                    return out;
                pos = scan;
                stringRule = -1;
                continue;
            }
            if (blankStrings)
                blankRange(out, pos, close);
            pos = close;
            stringRule = -1;
            continue;
        }

        if (startsAt(text, pos, def->lineComment)) {
            int end = text.indexOf(QLatin1Char('\n'), pos);
            if (end < 0)
                end = length;
            blankRange(out, pos, end);
            pos = end;
            continue;
        }

        if (startsAt(text, pos, def->blockCommentStart)) {
            inBlock = true;
            continue;
        }

        bool consumed = false;
        for (int i = 0; i < def->strings.size(); ++i) {
            const StringRule &rule = def->strings.at(i);
            if (!startsAt(text, pos, rule.start))
                continue;
            int scan = pos + rule.start.size();
            int close = -1;
            while (scan < length) {
                if (startsAt(text, scan, rule.escape)) {
                    scan += rule.escape.size() + 1;
                    continue;
                }
                if (startsAt(text, scan, rule.end)) {
                    close = scan + rule.end.size();
                    break;
                }
                if (!rule.multiline && text.at(scan) == QLatin1Char('\n'))
                    break;
                ++scan;
            }
            if (close < 0) {
                if (rule.multiline) {
                    if (blankStrings)
                        blankRange(out, pos, length);
                    return out;
                }
                if (blankStrings)
                    blankRange(out, pos, scan);
                pos = scan;
            } else {
                if (blankStrings)
                    blankRange(out, pos, close);
                pos = close;
            }
            consumed = true;
            break;
        }
        if (consumed)
            continue;
        ++pos;
    }
    return out;
}

bool isIdentifier(const QString &name)
{
    if (name.isEmpty())
        return false;
    const QChar first = name.at(0);
    if (!first.isLetter() && first != QLatin1Char('_') && first != QLatin1Char('$'))
        return false;
    for (const QChar c : name) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('_') && c != QLatin1Char('$'))
            return false;
    }
    return true;
}

int lineIndexAt(const QVector<int> &lineStarts, int offset)
{
    int low = 0;
    int high = lineStarts.size() - 1;
    while (low < high) {
        const int mid = (low + high + 1) / 2;
        if (lineStarts.at(mid) <= offset)
            low = mid;
        else
            high = mid - 1;
    }
    return low;
}

void place(const QVector<int> &lineStarts, int offset, int &line, int &column)
{
    const int index = lineIndexAt(lineStarts, offset);
    line = index + 1;
    column = offset - lineStarts.at(index) + 1;
}

bool covered(const QString &original, const QString &blanked, int pos)
{
    return pos >= 0 && pos < blanked.size() && blanked.at(pos) == QLatin1Char(' ')
           && original.at(pos) != QLatin1Char(' ');
}

void addClause(const QString &clause, const QString &specifier, bool reexport, QList<ModuleBinding> &out)
{
    QString body = clause.trimmed();
    if (body.startsWith(QLatin1String("type ")))
        body = body.mid(5).trimmed();

    // The default import and a `* as` import come before any braces:
    // `Default`, `* as ns`, or `Default, * as ns`.
    QString braces;
    QString head = body;
    const int open = body.indexOf(QLatin1Char('{'));
    if (open >= 0) {
        const int close = body.lastIndexOf(QLatin1Char('}'));
        if (close > open)
            braces = body.mid(open + 1, close - open - 1);
        head = body.left(open);
    }
    for (QString part : head.split(QLatin1Char(','))) {
        part = part.trimmed();
        if (part.startsWith(QLatin1String("* as "))) {
            const QString local = part.mid(5).trimmed();
            if (isIdentifier(local))
                out.append({local, QString(), specifier, reexport, false, true});
        } else if (isIdentifier(part)) {
            out.append({part, QString(), specifier, reexport, false});
        }
    }

    const QStringList parts = braces.split(QLatin1Char(','));
    for (QString part : parts) {
        part = part.trimmed();
        if (part.startsWith(QLatin1String("type ")))
            part = part.mid(5).trimmed();
        if (part.isEmpty())
            continue;
        QString imported;
        QString local;
        const int asAt = part.indexOf(QLatin1String(" as "));
        if (asAt >= 0) {
            imported = part.left(asAt).trimmed();
            local = part.mid(asAt + 4).trimmed();
        } else {
            imported = part;
            local = part;
        }
        if (imported == QLatin1String("default"))
            imported.clear();
        if (!isIdentifier(local) || (!imported.isEmpty() && !isIdentifier(imported)))
            continue;
        out.append({local, imported, specifier, reexport, false});
    }
}

void scanModules(const QString &text, const QString &code, const QVector<int> &lineStarts, SymbolScan &scan)
{
    const QRegularExpression importRe(
        QStringLiteral("(?m)^[ \\t]*import\\s+(?:type\\s+)?(?!['\"])([\\s\\S]*?)\\sfrom\\s+['\"]([^'\"]+)['\"]"));
    QRegularExpressionMatchIterator imports = importRe.globalMatch(text);
    while (imports.hasNext()) {
        const QRegularExpressionMatch match = imports.next();
        if (covered(text, code, int(match.capturedStart())))
            continue;
        addClause(match.captured(1), match.captured(2), false, scan.modules);
    }

    const QRegularExpression exportFrom(
        QStringLiteral("(?m)^[ \\t]*export\\s+(?:type\\s+)?\\{([\\s\\S]*?)\\}\\s*from\\s+['\"]([^'\"]+)['\"]"));
    QRegularExpressionMatchIterator exported = exportFrom.globalMatch(text);
    while (exported.hasNext()) {
        const QRegularExpressionMatch match = exported.next();
        if (covered(text, code, int(match.capturedStart())))
            continue;
        addClause(QStringLiteral("{%1}").arg(match.captured(1)), match.captured(2), true, scan.modules);
    }

    const QRegularExpression exportNamespace(QStringLiteral(
        "(?m)^[ \\t]*export\\s+(?:type\\s+)?\\*\\s+as\\s+([A-Za-z_$][\\w$]*)\\s+from\\s+['\"]([^'\"]+)['\"]"));
    QRegularExpressionMatchIterator namespaces = exportNamespace.globalMatch(text);
    while (namespaces.hasNext()) {
        const QRegularExpressionMatch match = namespaces.next();
        if (covered(text, code, int(match.capturedStart())))
            continue;
        scan.modules.append({match.captured(1), QString(), match.captured(2), true, false, true});
    }

    const QRegularExpression exportStar(
        QStringLiteral("(?m)^[ \\t]*export\\s+(?:type\\s+)?\\*\\s+from\\s+['\"]([^'\"]+)['\"]"));
    QRegularExpressionMatchIterator stars = exportStar.globalMatch(text);
    while (stars.hasNext()) {
        const QRegularExpressionMatch match = stars.next();
        if (covered(text, code, int(match.capturedStart())))
            continue;
        scan.modules.append({QString(), QString(), match.captured(1), true, true});
    }

    const QRegularExpression defaultNamed(QStringLiteral(
        "(?m)^[ \\t]*export\\s+default\\s+(?:async\\s+)?(?:function\\s+([A-Za-z_$][\\w$]*)|class\\s+([A-Za-z_$][\\w$]*)|(?!function\\b|class\\b|async\\b)([A-Za-z_$][\\w$]*))"));
    QRegularExpressionMatchIterator defaults = defaultNamed.globalMatch(text);
    while (defaults.hasNext()) {
        const QRegularExpressionMatch match = defaults.next();
        if (covered(text, code, int(match.capturedStart())))
            continue;
        int group = 1;
        if (match.capturedStart(1) < 0)
            group = match.capturedStart(2) >= 0 ? 2 : 3;
        const int start = int(match.capturedStart(group));
        if (start < 0 || !isIdentifier(match.captured(group)))
            continue;
        place(lineStarts, start, scan.defaultLine, scan.defaultColumn);
        scan.defaultName = match.captured(group);
        break;
    }
    if (scan.defaultLine == 0) {
        const QRegularExpression anonymous(QStringLiteral("(?m)^[ \\t]*export\\s+default\\b"));
        QRegularExpressionMatchIterator rest = anonymous.globalMatch(text);
        while (rest.hasNext()) {
            const QRegularExpressionMatch match = rest.next();
            if (covered(text, code, int(match.capturedStart())))
                continue;
            const int exportAt = text.indexOf(QLatin1String("export"), int(match.capturedStart()));
            place(lineStarts, exportAt, scan.defaultLine, scan.defaultColumn);
            break;
        }
    }
}

} // namespace

SymbolScan scanSource(const QString &text, const LanguageDefinition *definition, bool includeLocals)
{
    SymbolScan scan;
    if (definition == nullptr || (definition->symbols.isEmpty() && !definition->modules))
        return scan;

    const QString code = blankNonCode(text, definition, true);
    QVector<int> lineStarts;
    lineStarts.append(0);
    for (int i = 0; i < code.size(); ++i) {
        if (code.at(i) == QLatin1Char('\n'))
            lineStarts.append(i + 1);
    }

    static const QRegularExpression styleName(QStringLiteral("^(?:[.#$@]|--)?-?[A-Za-z_][\\w-]*$"));
    QSet<QString> seen;
    const auto addHit = [&](const QString &name, int start, bool local) {
        if (definition->stylesheet ? !styleName.match(name).hasMatch() : !isIdentifier(name))
            return;
        if (!definition->keywordRegex.pattern().isEmpty()) {
            const QRegularExpressionMatch word = definition->keywordRegex.match(name);
            if (word.hasMatch() && word.capturedStart() == 0 && word.capturedLength() == name.size())
                return;
        }
        int line = 1;
        int column = 1;
        place(lineStarts, start, line, column);
        const QString id = name + QLatin1Char('\n') + QString::number(line) + QLatin1Char(':')
                           + QString::number(column);
        if (seen.contains(id))
            return;
        seen.insert(id);
        scan.hits.append({name, line, column, local});
    };

    for (const SymbolRule &rule : definition->symbols) {
        if (rule.local && !includeLocals)
            continue;
        QRegularExpressionMatchIterator it = rule.regex.globalMatch(code);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            if (rule.capture > match.lastCapturedIndex())
                continue;
            const int start = int(match.capturedStart(rule.capture));
            if (start < 0)
                continue;
            const QString captured = match.captured(rule.capture);
            if (!rule.params) {
                addHit(rule.prefix + captured, start, rule.local);
                continue;
            }
            int offset = 0;
            const QStringList parts = captured.split(QLatin1Char(','));
            for (QString part : parts) {
                const int partAt = captured.indexOf(part, offset);
                offset = partAt + part.size();
                const int eq = part.indexOf(QLatin1Char('='));
                if (eq >= 0)
                    part = part.left(eq);
                const int colon = part.indexOf(QLatin1Char(':'));
                if (colon >= 0)
                    part = part.left(colon);
                if (part.trimmed().startsWith(QLatin1String("...")))
                    part = part.trimmed().mid(3);
                const QRegularExpression last(QStringLiteral("([A-Za-z_$][\\w$]*)\\s*$"));
                const QRegularExpressionMatch name = last.match(part);
                if (!name.hasMatch())
                    continue;
                const int nameAt = partAt + int(name.capturedStart(1));
                addHit(name.captured(1), start + nameAt, true);
            }
        }
    }

    if (definition->modules) {
        const QString commentsOnly = blankNonCode(text, definition, false);
        scanModules(commentsOnly, code, lineStarts, scan);
    }
    return scan;
}
