#pragma once

#include "Types.h"

#include <QSyntaxHighlighter>

class LanguageDefinition;
class Theme;

// Rule-driven highlighter. Besides coloring, it is the single authority on
// fold levels: only the lexer knows whether a delimiter sits inside a string
// or comment, so foldingIndent is computed here and stored in BlockData.
//
// Block state packing: (foldState << 8) | lexerState, where foldState is the
// delimiter depth at end of block or, in heading mode, the bitmask of open
// heading levels. A fold-state change therefore changes the block state and
// makes QSyntaxHighlighter cascade the rehighlight to following blocks,
// exactly as an unterminated block comment would.
class Highlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    Highlighter(QTextDocument *document, const LanguageDefinition *definition, const Theme *theme);

protected:
    void highlightBlock(const QString &text) override;

private:
    void applyPatternRules(const QString &text);
    // Formats comments/strings, counts fold delimiters outside them, and
    // returns the lexer state at end of line. depth/minDepth are running
    // delimiter counters seeded with the depth carried in from the previous
    // block.
    int scanLexicalRegions(const QString &text, int lexerIn, int &depth, int &minDepth);
    int indentFoldLevel() const;

    const LanguageDefinition *m_definition;
    const Theme *m_theme;
};
