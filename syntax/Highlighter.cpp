#include "syntax/Highlighter.h"

#include "editor/BlockData.h"
#include "syntax/LanguageDefinition.h"
#include "syntax/LanguageRegistry.h"
#include "syntax/Theme.h"

#include <QTimer>

namespace {

constexpr int kLexerNormal = 0;
constexpr int kLexerInBlockComment = 1;
// States >= kLexerInString mean "inside multiline string rule (state - kLexerInString)".
constexpr int kLexerInString = 2;

bool matchesAt(const QString &text, int pos, const QString &token)
{
    return !token.isEmpty() && QStringView(text).mid(pos).startsWith(token);
}

// ATX heading level (1-6) of a Markdown line, or 0 when it is not a heading.
int headingLevel(const QString &text)
{
    int level = 0;
    while (level < text.length() && text.at(level) == QLatin1Char('#'))
        ++level;
    const bool followedBySpace = level < text.length()
                                 && (text.at(level) == QLatin1Char(' ')
                                     || text.at(level) == QLatin1Char('\t'));
    return level >= 1 && level <= 6 && followedBySpace ? level : 0;
}

} // namespace

Highlighter::Highlighter(QTextDocument *document, const LanguageDefinition *definition,
                         const LanguageRegistry *registry, const Theme *theme)
    : QSyntaxHighlighter(static_cast<QObject *>(nullptr))
    , m_definition(definition)
    , m_registry(registry)
    , m_theme(theme)
{
    // Attaching with automatic highlighting off skips the full-document pass
    // QSyntaxHighlighter would otherwise run here.
    setDocument(document);
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &Highlighter::highlightNextSlice);
}

void Highlighter::setAutomatic(bool automatic)
{
    m_automatic = automatic;
    if (automatic)
        return;
    m_timer->stop();
    m_limit = -1;
    m_resume = QTextBlock();
}

void Highlighter::highlightGradually()
{
    m_timer->stop();
    m_resume = QTextBlock();
    highlightNextSlice();
}

void Highlighter::highlightNextSlice()
{
    if (document() == nullptr)
        return;
    m_automatic = true;
    const QTextBlock start = m_resume.isValid() ? m_resume : document()->firstBlock();
    m_resume = QTextBlock();
    m_limit = 200;
    if (start.isValid())
        rehighlightBlock(start);
    m_limit = -1;
    if (m_resume.isValid())
        m_timer->start(0);
}

void Highlighter::highlightBlock(const QString &text)
{
    if (!m_automatic)
        return;
    if (m_limit == 0) {
        if (!m_resume.isValid())
            m_resume = currentBlock();
        setCurrentBlockState(currentBlockState());
        return;
    }
    if (m_limit > 0)
        --m_limit;

    const int packedIn = previousBlockState();
    const int lexerIn = packedIn < 0 ? kLexerNormal : (packedIn & 0xFF);
    const int embeddedIn = packedIn < 0 ? 0 : ((packedIn >> 8) & 0xFF);
    const int foldIn = packedIn < 0 ? 0 : (packedIn >> 16);

    int depth = foldIn;
    int minDepth = foldIn;
    int lexerOut = kLexerNormal;
    int embeddedOut = 0;
    Region region = Region::Outside;

    if (embeddedIn > 0) {
        // A closing fence ends the region even mid-comment in the inner
        // language, matching how Markdown renderers treat it.
        if (matchEmbedEnd(text)) {
            region = Region::Closing;
            formatFenceLine(text);
        } else {
            region = Region::Inside;
            const LanguageDefinition *inner = m_registry->definitionAt(embeddedIn - 1);
            applyPatternRules(text, inner);
            // The inner language's fold delimiters must not disturb the
            // outer fold state, so they count into throwaway counters.
            int innerDepth = 0;
            int innerMinDepth = 0;
            lexerOut = scanLexicalRegions(text, inner, lexerIn, innerDepth, innerMinDepth);
            applyInStringRules(text, inner);
            embeddedOut = embeddedIn;
            applyBackground(text.length(), m_theme->codeBlockBackground);
        }
    } else {
        const int opened = lexerIn == kLexerNormal ? matchEmbedStart(text) : -1;
        if (opened >= 0) {
            region = Region::Opening;
            embeddedOut = opened + 1;
            formatFenceLine(text);
        } else {
            applyPatternRules(text, m_definition);
            lexerOut = scanLexicalRegions(text, m_definition, lexerIn, depth, minDepth);
            applyInStringRules(text, m_definition);
        }
    }

    // Fold level: the base comes from the language's fold mode; lines inside
    // an embedded region sit one deeper so the opening fence folds them.
    int base = qMax(minDepth, 0);
    switch (m_definition->foldMode) {
    case FoldMode::Indent:
        base = indentFoldLevel();
        break;
    case FoldMode::Heading: {
        // The carried state is a bitmask of open heading levels (bit L-1).
        // A heading closes every level >= its own and sits at the depth of
        // its remaining ancestors, so siblings align even when levels are
        // skipped (e.g. "##" with no "#"); its body sits one deeper. Lines
        // inside a fence are never headings.
        int open = foldIn;
        const int level = region == Region::Outside ? headingLevel(text) : 0;
        if (level > 0) {
            open &= (1 << (level - 1)) - 1;
            base = qPopulationCount(quint32(open));
            open |= 1 << (level - 1);
        } else {
            base = qPopulationCount(quint32(open));
        }
        depth = open;
        break;
    }
    default:
        break;
    }
    BlockData::get(currentBlock())->foldingIndent = region == Region::Inside ? base + 1 : base;

    setCurrentBlockState((qMax(depth, 0) << 16) | (embeddedOut << 8) | lexerOut);
}

int Highlighter::matchEmbedStart(const QString &text) const
{
    for (const EmbedRule &rule : m_definition->embedded) {
        const QRegularExpressionMatch match = rule.start.match(text);
        if (match.hasMatch()) {
            const LanguageDefinition *inner =
                m_registry->definitionForFenceInfo(match.captured(rule.languageCapture));
            return m_registry->indexOf(inner);
        }
    }
    return -1;
}

bool Highlighter::matchEmbedEnd(const QString &text) const
{
    for (const EmbedRule &rule : m_definition->embedded) {
        if (rule.end.match(text).hasMatch())
            return true;
    }
    return false;
}

void Highlighter::formatFenceLine(const QString &text)
{
    QTextCharFormat format = m_theme->tokenFormat(TokenRole::String);
    format.setBackground(m_theme->codeBlockBackground);
    setFormat(0, text.length(), format);
}

void Highlighter::applyBackground(int length, const QColor &color)
{
    int start = 0;
    while (start < length) {
        QTextCharFormat run = format(start);
        int end = start + 1;
        while (end < length && format(end) == run)
            ++end;
        run.setBackground(color);
        setFormat(start, end - start, run);
        start = end;
    }
}

void Highlighter::applyPatternRules(const QString &text, const LanguageDefinition *def)
{
    const auto applyRegex = [this, &text](const QRegularExpression &regex, TokenRole role) {
        if (regex.pattern().isEmpty())
            return;
        QRegularExpressionMatchIterator it = regex.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            setFormat(int(match.capturedStart()), int(match.capturedLength()),
                      m_theme->tokenFormat(role));
        }
    };

    applyRegex(def->keywordRegex, TokenRole::Keyword);
    applyRegex(def->typeRegex, TokenRole::Type);
    for (const PatternRule &rule : def->patterns)
        applyRegex(rule.regex, rule.role);
}

void Highlighter::applyInStringRules(const QString &text, const LanguageDefinition *def)
{
    const QTextCharFormat stringFormat = m_theme->tokenFormat(TokenRole::String);
    for (const PatternRule &rule : def->patterns) {
        if (!rule.insideStrings)
            continue;
        QRegularExpressionMatchIterator it = rule.regex.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const int start = int(match.capturedStart());
            if (format(start) == stringFormat)
                setFormat(start, int(match.capturedLength()), m_theme->tokenFormat(rule.role));
        }
    }
}

int Highlighter::scanLexicalRegions(const QString &text, const LanguageDefinition *def,
                                    int lexerIn, int &depth, int &minDepth)
{
    const QTextCharFormat commentFormat = m_theme->tokenFormat(TokenRole::Comment);
    const QTextCharFormat stringFormat = m_theme->tokenFormat(TokenRole::String);
    const int length = text.length();
    int pos = 0;

    // Finish a construct carried over from the previous block.
    if (lexerIn == kLexerInBlockComment) {
        const int end = text.indexOf(def->blockCommentEnd);
        if (end < 0) {
            setFormat(0, length, commentFormat);
            return kLexerInBlockComment;
        }
        pos = end + def->blockCommentEnd.length();
        setFormat(0, pos, commentFormat);
    } else if (lexerIn >= kLexerInString) {
        const StringRule &rule = def->strings.at(lexerIn - kLexerInString);
        int scan = 0;
        int close = -1;
        while (scan < length) {
            if (matchesAt(text, scan, rule.escape)) {
                scan += rule.escape.length() + 1;
                continue;
            }
            if (matchesAt(text, scan, rule.end)) {
                close = scan + rule.end.length();
                break;
            }
            ++scan;
        }
        if (close < 0) {
            setFormat(0, length, stringFormat);
            return lexerIn;
        }
        setFormat(0, close, stringFormat);
        pos = close;
    }

    while (pos < length) {
        if (matchesAt(text, pos, def->lineComment)) {
            setFormat(pos, length - pos, commentFormat);
            return kLexerNormal;
        }

        if (matchesAt(text, pos, def->blockCommentStart)) {
            const int end = text.indexOf(def->blockCommentEnd, pos + def->blockCommentStart.length());
            if (end < 0) {
                setFormat(pos, length - pos, commentFormat);
                return kLexerInBlockComment;
            }
            const int close = end + def->blockCommentEnd.length();
            setFormat(pos, close - pos, commentFormat);
            pos = close;
            continue;
        }

        bool consumed = false;
        for (int i = 0; i < def->strings.size(); ++i) {
            const StringRule &rule = def->strings.at(i);
            if (!matchesAt(text, pos, rule.start))
                continue;
            int scan = pos + rule.start.length();
            int close = -1;
            while (scan < length) {
                if (matchesAt(text, scan, rule.escape)) {
                    scan += rule.escape.length() + 1;
                    continue;
                }
                if (matchesAt(text, scan, rule.end)) {
                    close = scan + rule.end.length();
                    break;
                }
                ++scan;
            }
            if (close < 0) {
                setFormat(pos, length - pos, stringFormat);
                if (rule.multiline)
                    return kLexerInString + i;
                pos = length;
            } else {
                setFormat(pos, close - pos, stringFormat);
                pos = close;
            }
            consumed = true;
            break;
        }
        if (consumed)
            continue;

        if (def->foldMode == FoldMode::Delimiter) {
            bool moved = false;
            for (const FoldPair &pair : def->foldPairs) {
                if (matchesAt(text, pos, pair.open)) {
                    ++depth;
                    pos += pair.open.length();
                    moved = true;
                    break;
                }
                if (matchesAt(text, pos, pair.close)) {
                    --depth;
                    minDepth = qMin(minDepth, depth);
                    pos += pair.close.length();
                    moved = true;
                    break;
                }
            }
            if (moved)
                continue;
        }

        ++pos;
    }

    return kLexerNormal;
}

int Highlighter::indentFoldLevel() const
{
    const QTextBlock block = currentBlock();
    QString text = block.text();
    // Blank lines inherit the level of the next non-blank line so they never
    // sever a fold region.
    if (text.trimmed().isEmpty()) {
        for (QTextBlock b = block.next(); b.isValid(); b = b.next()) {
            if (!b.text().trimmed().isEmpty()) {
                text = b.text();
                break;
            }
        }
    }
    int columns = 0;
    for (const QChar c : text) {
        if (c == QLatin1Char(' '))
            ++columns;
        else if (c == QLatin1Char('\t'))
            columns += 4;
        else
            break;
    }
    return columns;
}
