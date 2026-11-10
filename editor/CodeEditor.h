#pragma once

#include "editor/CompletionItem.h"
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
class ProcessOrchestrator;
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
    void setOrchestrator(ProcessOrchestrator *orchestrator) { m_orchestrator = orchestrator; }
    // Tab title and path, shown while this buffer's symbol scan is waiting.
    void setWorkOwner(const QString &owner) { m_workOwner = owner; }
    // Reads the open document into the symbol index.
    void refreshSymbols();
    void setFilePath(const QString &path);
    const QString &filePath() const { return m_filePath; }
    // When false, edits do not schedule a check and any spans are cleared.
    void setSyntaxEnabled(bool enabled);
    void setCompletionEnabled(bool enabled);
    // The characters this file's language server wants a completion asked
    // after, such as `.`. The first letter of a word always asks.
    void setCompletionTriggers(const QStringList &triggers);
    // `incomplete` means the list was cut short for the word typed so far,
    // so it is asked for again when that word changes.
    void showCompletions(const QList<CompletionItem> &items, bool incomplete = false);
    void showResolved(const QString &data, const QString &detail, const QString &documentation);
    void showSignatures(const QList<SignatureHint> &signatures, int active);
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
    // character, drawn `size` columns wide. Those are the fallback when the
    // lines above the cursor show no indentation. `keep` copies the prior
    // line's own leading whitespace onto a new line.
    void setIndent(bool spaces, int size, bool keep);

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
    void syntaxRequested();
    // Ctrl+Space, a word being started, or a trigger character. `trigger` is
    // that character and is empty for the other two.
    void completionRequested(const QString &trigger);
    // A key was pressed while the caret is inside a call's parentheses.
    void signatureRequested();
    // The selected completion has no documentation yet.
    void documentationRequested(const CompletionItem &item);
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
    void scheduleSyntax();
    // Decides what the key just typed asks for. `text` is the key's text;
    // Backspace and Delete pass "\b".
    void scheduleCompletion(const QString &text, bool listOpen);
    // The identifier that ends at the caret.
    QString completionPrefix() const;
    // The caret is still in the word the open list was asked for.
    bool inCompletion() const;
    void narrowCompletions();
    void hideCompletions();
    void hideSignature();
    void drawSignature();
    void cycleSignature(int delta);
    bool insideArguments() const;
    void placeSignature();
    void applyCompletion(const CompletionItem &item);
    void placeCompletionPopup();
    bool diagnosticRange(const Diagnostic &span, int *start, int *end) const;
    void updateDiagnosticTip(const QPoint &pos);
    void updateSymbolLink(const QPoint &pos);
    void clearSymbolLink();
    int foldMarkerWidth() const;
    void drawFoldMarker(QPainter &painter, const QRect &rect, bool expanded) const;
    void applyTabStop();
    // The file rule, used when no line above shows an indentation.
    QString indentUnit() const;
    // One indent level taken from the nearest indented line at or above
    // `origin`. A tab there means a tab; spaces mean the shallowest run.
    QString indentUnitAt(const QTextBlock &origin) const;
    // Leading whitespace to carry onto a new line: the current line, or the
    // prior non-blank line when this one is empty. Empty when that line is
    // not indented.
    QString keptIndent(const QTextBlock &block) const;
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
    ProcessOrchestrator *m_orchestrator = nullptr;
    class QTimer *m_syntaxTimer = nullptr;
    class QTimer *m_autocompleteTimer = nullptr;
    class CompletionPopup *m_completionPopup = nullptr;
    class QLabel *m_signature = nullptr;
    QList<SignatureHint> m_signatures;
    int m_signatureIndex = 0;
    bool m_completion = false;
    // What the completion timer asks for when it fires.
    bool m_wantCompletion = false;
    bool m_wantSignature = false;
    QString m_trigger;
    QString m_triggers;
    // The open list: the line it is on, the column its word starts at, and
    // the caret column when it was shown.
    int m_listBlock = 0;
    int m_listAnchor = 0;
    int m_listCaret = 0;
    bool m_listIncomplete = false;
    QString m_filePath;
    QString m_workOwner;
    bool m_syntaxEnabled = false;
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
    bool m_keepIndent = false;
    QString m_findTerm;
    QList<QTextEdit::ExtraSelection> m_findSelections;
    bool m_overlayRefreshQueued = false;
};
