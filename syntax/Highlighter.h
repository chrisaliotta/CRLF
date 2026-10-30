#pragma once

#include "Types.h"

#include <QSyntaxHighlighter>
#include <QTextBlock>

class LanguageDefinition;
class LanguageRegistry;
class Theme;

// Rule-driven highlighter. Besides coloring, it is the single authority on
// fold levels: only the lexer knows whether a delimiter sits inside a string
// or comment, so foldingIndent is computed here and stored in BlockData.
//
// Embedded regions (Markdown fenced code) hand their lines to another
// language's rules, so code inside a fence is colored exactly as it would be
// in its own file, including that language's multi-line comments/strings.
//
// Block state packing: (foldState << 16) | (embeddedLanguage << 8) | lexerState.
// foldState is the delimiter depth at end of block or, in heading mode, the
// bitmask of open heading levels. embeddedLanguage is 0 outside a region,
// else registry index + 1. Any field change alters the block state and makes
// QSyntaxHighlighter cascade the rehighlight to following blocks, exactly as
// an unterminated block comment would.
class Highlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    Highlighter(QTextDocument *document, const LanguageDefinition *definition,
                const LanguageRegistry *registry, const Theme *theme);

    // While false, document edits do not recolor. Used around a bulk
    // setPlainText so the insert itself does not highlight every line.
    void setAutomatic(bool automatic);
    // Colors the start of the document immediately and the rest in slices
    // so a large file does not freeze the window.
    void highlightGradually();

protected:
    void highlightBlock(const QString &text) override;

private:
    // Where a line sits relative to embedded regions.
    enum class Region { Outside, Opening, Inside, Closing };

    void applyPatternRules(const QString &text, const LanguageDefinition *def);
    // Runs after the lexical scan: re-applies insideStrings rules wherever
    // they match within string-formatted text.
    void applyInStringRules(const QString &text, const LanguageDefinition *def);
    // Formats comments/strings, counts fold delimiters outside them, and
    // returns the lexer state at end of line. depth/minDepth are running
    // delimiter counters seeded with the depth carried in from the previous
    // block.
    int scanLexicalRegions(const QString &text, const LanguageDefinition *def, int lexerIn,
                           int &depth, int &minDepth);
    // Registry index of the language opened by an embed rule on this line,
    // or -1 when the line opens none.
    int matchEmbedStart(const QString &text) const;
    bool matchEmbedEnd(const QString &text) const;
    void formatFenceLine(const QString &text);
    // Adds a background behind every existing format run on the line.
    void applyBackground(int length, const QColor &color);
    int indentFoldLevel() const;
    void highlightNextSlice();

    const LanguageDefinition *m_definition;
    const LanguageRegistry *m_registry;
    const Theme *m_theme;
    class QTimer *m_timer = nullptr;
    QTextBlock m_resume;
    // Below zero, highlightBlock runs without a cap. Zero means this slice
    // is full and further blocks in the same cascade keep their old state.
    int m_limit = -1;
    bool m_automatic = false;
};
