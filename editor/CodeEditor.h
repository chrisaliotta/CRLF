#pragma once

#include "editor/Diagnostic.h"
#include "editor/SymbolLocation.h"

#include <QHash>
#include <QList>
#include <QPlainTextEdit>

class QKeyEvent;

class FoldController;
class FoldingLayout;
class Gutter;
class Highlighter;
class QEvent;
class QFocusEvent;
class QMouseEvent;
class LanguageDefinition;
class LanguageRegistry;
class SymbolIndex;
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
    void setSymbolIndex(SymbolIndex *index);
    // Reads the open document into the symbol index.
    void refreshSymbols();
    void setFilePath(const QString &path);
    const QString &filePath() const { return m_filePath; }
    // When false, edits do not schedule a check and any spans are cleared.
    void setCheckingEnabled(bool enabled);
    void setDiagnostics(const QList<Diagnostic> &spans);
    int documentRevision() const;
    // 1-based. Unfolds a region that hides the line, then centers it.
    void goTo(int line, int column);
    // Inserts text without coloring every line before the window can paint.
    void replaceText(const QString &text);
    // One undo step. Leaves the document modified. Does not write the file.
    void applyCommandOutput(const QString &text);
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

signals:
    // Ctrl+click on a name whose definition is in the index.
    void definitionActivated(const QString &path, quintptr editorId, int line, int column);
    // Ctrl+click on a package import or an unresolved stylesheet name.
    void externalActivated(const SymbolLocation &target);
    // The buffer has been still for 500ms and checking is enabled.
    void checkRequested();
    // An edit invalidated the check that was in flight.
    void checkInvalidated();
    void filePathChanged();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
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
    void scheduleSymbolScan();
    void scheduleCheck();
    bool diagnosticRange(const Diagnostic &span, int *start, int *end) const;
    void updateDiagnosticTip(const QPoint &pos);
    void submitSymbols();
    void updateSymbolLink(const QPoint &pos);
    void clearSymbolLink();
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
    SymbolIndex *m_symbolIndex = nullptr;
    class QTimer *m_symbolTimer = nullptr;
    class QTimer *m_checkTimer = nullptr;
    QString m_filePath;
    bool m_checking = false;
    QList<Diagnostic> m_diagnostics;
    // Block number to true when that line's strongest span is an error.
    QHash<int, bool> m_diagnosticLines;
    QString m_diagnosticTip;
    // The identifier currently under Ctrl, and the definition it points at.
    QString m_linkWord;
    int m_linkWordLine = 0;
    int m_linkWordColumn = 0;
    bool m_linkActive = false;
    SymbolLocation m_link;
    QTextEdit::ExtraSelection m_linkSelection;
    int m_basePointSize = 0;
    bool m_lineNumbers = true;
    bool m_indentSpaces = false;
    int m_indentSize = 4;
    QString m_findTerm;
    QList<QTextEdit::ExtraSelection> m_findSelections;
    bool m_overlayRefreshQueued = false;
};
