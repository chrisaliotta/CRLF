#pragma once

#include <QList>
#include <QPlainTextEdit>

class QKeyEvent;

class FoldController;
class FoldingLayout;
class Gutter;
class Highlighter;
class QEvent;
class LanguageDefinition;
class LanguageRegistry;
class Theme;

// The editing surface for one document: a QPlainTextEdit with a FoldingLayout
// installed on its document, a gutter (line numbers + fold markers), a
// current-line highlight, and a theme-driven highlighter.
class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    // The registry resolves languages embedded in this document (e.g. code
    // fenced inside Markdown).
    explicit CodeEditor(const LanguageRegistry *registry, QWidget *parent = nullptr);
    ~CodeEditor() override;

    void setTheme(const Theme *theme);
    void setLanguage(const LanguageDefinition *definition);
    // Inserts text without coloring every line before the window can paint.
    void replaceText(const QString &text);
    const LanguageDefinition *language() const { return m_definition; }

    FoldController *foldController() { return m_foldController; }
    void moveCursorOutOfHiddenBlock();

    void zoom(int steps);
    void resetZoom();
    // Fold markers stay. Hiding numbers narrows the gutter to just those markers.
    void setLineNumbersVisible(bool visible);
    // `spaces` inserts `size` spaces for Tab. Otherwise Tab inserts one tab
    // character, drawn `size` columns wide.
    void setIndent(bool spaces, int size);

    // Highlights every occurrence. An empty term clears the highlights.
    void setFindTerm(const QString &term);

    // Gutter geometry and event handling, called by the Gutter child widget.
    int gutterWidth() const;
    void paintGutter(QPaintEvent *event);
    void gutterMousePress(QMouseEvent *event);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuildHighlighter();
    void updateGutterWidth();
    void updateGutterArea(const QRect &rect, int dy);
    void highlightCurrentLine();
    void refreshFindSelections();
    // contentsChange and cursor moves are delivered from inside the edit that
    // caused them. Updating extra selections there reenters the document and
    // frees formats the edit is still using, so the refresh is deferred.
    void scheduleOverlayRefresh();
    int foldMarkerWidth() const;
    void drawFoldMarker(QPainter &painter, const QRect &rect, bool expanded) const;
    void applyTabStop();
    QString indentUnit() const;
    // Adds or removes one indent from each line in the selection.
    void indentLines(int direction);

    FoldingLayout *m_layout = nullptr;
    Gutter *m_gutter = nullptr;
    FoldController *m_foldController = nullptr;
    Highlighter *m_highlighter = nullptr;
    const LanguageRegistry *m_registry;
    const Theme *m_theme = nullptr;
    const LanguageDefinition *m_definition = nullptr;
    int m_basePointSize = 0;
    bool m_lineNumbers = true;
    bool m_indentSpaces = false;
    int m_indentSize = 4;
    QString m_findTerm;
    QList<QTextEdit::ExtraSelection> m_findSelections;
    bool m_overlayRefreshQueued = false;
};
