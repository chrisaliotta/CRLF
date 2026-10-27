#include "syntax/Highlighter.h"

#include "editor/BlockData.h"
#include "syntax/LanguageDefinition.h"
#include "syntax/Theme.h"

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

Highlighter::Highlighter(QTextDocument *document, const LanguageDefinition *definition, const Theme *theme)
    : QSyntaxHighlighter(document)
    , m_definition(definition)
    , m_theme(theme)
{
}

void Highlighter::highlightBlock(const QString &text)
{
    const int packedIn = previousBlockState();
    const int lexerIn = packedIn < 0 ? kLexerNormal : (packedIn & 0xFF);
    const int depthIn = packedIn < 0 ? 0 : (packedIn >> 8);

    applyPatternRules(text);

    int depth = depthIn;
    int minDepth = depthIn;
    const int lexerOut = scanLexicalRegions(text, lexerIn, depth, minDepth);

    int foldingIndent = qMax(minDepth, 0);
    switch (m_definition->foldMode) {
    case FoldMode::Indent:
        foldingIndent = indentFoldLevel();
        break;
    case FoldMode::Heading: {
        // The carried state is a bitmask of open heading levels (bit L-1).
        // A heading closes every level >= its own and sits at the depth of
        // its remaining ancestors, so siblings align even when levels are
        // skipped (e.g. "##" with no "#"); its body sits one deeper. A '#'
        // inside a fenced code block is not a heading.
        int open = depthIn;
        const int level = lexerIn == kLexerNormal ? headingLevel(text) : 0;
        if (level > 0) {
            open &= (1 << (level - 1)) - 1;
            foldingIndent = qPopulationCount(quint32(open));
            open |= 1 << (level - 1);
        } else {
            foldingIndent = qPopulationCount(quint32(open));
        }
        depth = open;
        break;
    }
    default:
        break;
    }
    BlockData::get(currentBlock())->foldingIndent = foldingIndent;

    setCurrentBlockState((qMax(depth, 0) << 8) | lexerOut);
}

void Highlighter::applyPatternRules(const QString &text)
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

    applyRegex(m_definition->keywordRegex, TokenRole::Keyword);
    applyRegex(m_definition->typeRegex, TokenRole::Type);
    for (const PatternRule &rule : m_definition->patterns)
        applyRegex(rule.regex, rule.role);
}

int Highlighter::scanLexicalRegions(const QString &text, int lexerIn, int &depth, int &minDepth)
{
    const QTextCharFormat commentFormat = m_theme->tokenFormat(TokenRole::Comment);
    const QTextCharFormat stringFormat = m_theme->tokenFormat(TokenRole::String);
    const int length = text.length();
    int pos = 0;

    // Finish a construct carried over from the previous block.
    if (lexerIn == kLexerInBlockComment) {
        const int end = text.indexOf(m_definition->blockCommentEnd);
        if (end < 0) {
            setFormat(0, length, commentFormat);
            return kLexerInBlockComment;
        }
        pos = end + m_definition->blockCommentEnd.length();
        setFormat(0, pos, commentFormat);
    } else if (lexerIn >= kLexerInString) {
        const StringRule &rule = m_definition->strings.at(lexerIn - kLexerInString);
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
        if (matchesAt(text, pos, m_definition->lineComment)) {
            setFormat(pos, length - pos, commentFormat);
            return kLexerNormal;
        }

        if (matchesAt(text, pos, m_definition->blockCommentStart)) {
            const int end = text.indexOf(m_definition->blockCommentEnd,
                                         pos + m_definition->blockCommentStart.length());
            if (end < 0) {
                setFormat(pos, length - pos, commentFormat);
                return kLexerInBlockComment;
            }
            const int close = end + m_definition->blockCommentEnd.length();
            setFormat(pos, close - pos, commentFormat);
            pos = close;
            continue;
        }

        bool consumed = false;
        for (int i = 0; i < m_definition->strings.size(); ++i) {
            const StringRule &rule = m_definition->strings.at(i);
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

        if (m_definition->foldMode == FoldMode::Delimiter) {
            bool moved = false;
            for (const FoldPair &pair : m_definition->foldPairs) {
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
