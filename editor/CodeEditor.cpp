#include "editor/CodeEditor.h"

#include "editor/BlockData.h"
#include "editor/FoldController.h"
#include "editor/FoldingLayout.h"
#include "editor/Gutter.h"
#include "syntax/Highlighter.h"
#include "syntax/Theme.h"

#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyleFactory>
#include <QStyleOptionViewItem>
#include <QTextBlock>

namespace {

// The platform Vista style's branch indicators cannot be recolored, so fold
// markers are always drawn through Fusion.
QStyle *foldMarkerStyle()
{
    static QStyle *style = QStyleFactory::create(QStringLiteral("Fusion"));
    return style;
}

} // namespace

CodeEditor::CodeEditor(const LanguageRegistry *registry, QWidget *parent)
    : QPlainTextEdit(parent)
    , m_registry(registry)
{
    // The document must carry a FoldingLayout before setDocument() so the
    // editor wires its scrollbar updates to this layout instance.
    auto *doc = new QTextDocument(this);
    m_layout = new FoldingLayout(doc);
    doc->setDocumentLayout(m_layout);
    setDocument(doc);

    QFont editorFont(QStringLiteral("Ubuntu Mono"));
    editorFont.setStyleHint(QFont::Monospace);
    editorFont.setPointSize(12);
    setFont(editorFont);
    m_basePointSize = editorFont.pointSize();
    applyTabStop();

    setLineWrapMode(QPlainTextEdit::NoWrap);

    // Hover grows the handle. The track itself stays unpainted.
    const auto watchScrollBar = [this](QScrollBar *bar) {
        bar->setAttribute(Qt::WA_Hover, true);
        bar->installEventFilter(this);
    };
    watchScrollBar(verticalScrollBar());
    watchScrollBar(horizontalScrollBar());

    m_gutter = new Gutter(this);
    m_foldController = new FoldController(this, m_layout);

    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateGutterWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateGutterArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::scheduleOverlayRefresh);
    connect(document(), &QTextDocument::contentsChange, this,
            [this](int, int, int) { scheduleOverlayRefresh(); });

    updateGutterWidth();
}

CodeEditor::~CodeEditor()
{
    delete m_foldController;
}

void CodeEditor::setTheme(const Theme *theme)
{
    m_theme = theme;
    QPalette pal = palette();
    pal.setColor(QPalette::Base, theme->background);
    pal.setColor(QPalette::Text, theme->foreground);
    pal.setColor(QPalette::Highlight, theme->selection);
    pal.setColor(QPalette::HighlightedText, theme->foreground);
    setPalette(pal);
    if (m_definition)
        rebuildHighlighter();
    highlightCurrentLine();
    m_gutter->update();
}

void CodeEditor::setLanguage(const LanguageDefinition *definition)
{
    m_definition = definition;
    rebuildHighlighter();
}

void CodeEditor::rebuildHighlighter()
{
    delete m_highlighter;
    m_highlighter = nullptr;
    if (m_definition == nullptr || m_theme == nullptr)
        return;
    m_highlighter = new Highlighter(document(), m_definition, m_registry, m_theme);
    if (blockCount() > 200)
        m_highlighter->highlightGradually();
    else {
        m_highlighter->setAutomatic(true);
        m_highlighter->rehighlight();
    }
}

void CodeEditor::replaceText(const QString &text)
{
    if (m_highlighter)
        m_highlighter->setAutomatic(false);
    setPlainText(text);
    document()->setModified(false);
    if (m_highlighter == nullptr)
        return;
    if (blockCount() > 200)
        m_highlighter->highlightGradually();
    else {
        m_highlighter->setAutomatic(true);
        m_highlighter->rehighlight();
    }
}

void CodeEditor::moveCursorOutOfHiddenBlock()
{
    QTextCursor cursor = textCursor();
    if (!cursor.block().isVisible()) {
        cursor.setVisualNavigation(true);
        cursor.movePosition(QTextCursor::Up);
        setTextCursor(cursor);
    }
}

void CodeEditor::zoom(int steps)
{
    if (steps > 0)
        zoomIn(steps);
    else
        zoomOut(-steps);
    applyTabStop();
    updateGutterWidth();
}

void CodeEditor::resetZoom()
{
    QFont editorFont = font();
    editorFont.setPointSize(m_basePointSize);
    setFont(editorFont);
    applyTabStop();
    updateGutterWidth();
}

void CodeEditor::applyTabStop()
{
    setTabStopDistance(m_indentSize * fontMetrics().horizontalAdvance(QLatin1Char(' ')));
}

void CodeEditor::setIndent(bool spaces, int size)
{
    m_indentSpaces = spaces;
    m_indentSize = size;
    applyTabStop();
}

QString CodeEditor::indentUnit() const
{
    return m_indentSpaces ? QString(m_indentSize, QLatin1Char(' ')) : QStringLiteral("\t");
}

void CodeEditor::indentLines(int direction)
{
    QTextCursor cursor = textCursor();
    const int anchor = cursor.selectionStart();
    const int position = cursor.selectionEnd();
    cursor.setPosition(anchor);
    const int first = cursor.blockNumber();
    cursor.setPosition(position);
    int last = cursor.blockNumber();
    if (position > anchor && cursor.positionInBlock() == 0)
        --last;

    cursor.beginEditBlock();
    for (int number = first; number <= last; ++number) {
        const QTextBlock block = document()->findBlockByNumber(number);
        if (direction > 0) {
            QTextCursor line(block);
            line.insertText(indentUnit());
            continue;
        }
        const QString text = block.text();
        int remove = 0;
        if (text.startsWith(QLatin1Char('\t')))
            remove = 1;
        else {
            while (remove < m_indentSize && remove < text.size() && text.at(remove) == QLatin1Char(' '))
                ++remove;
        }
        if (remove == 0)
            continue;
        QTextCursor line(block);
        line.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, remove);
        line.removeSelectedText();
    }
    const QTextBlock startBlock = document()->findBlockByNumber(first);
    const QTextBlock endBlock = document()->findBlockByNumber(last);
    QTextCursor selected(document());
    selected.setPosition(startBlock.position());
    selected.setPosition(endBlock.position() + endBlock.length() - 1, QTextCursor::KeepAnchor);
    setTextCursor(selected);
    cursor.endEditBlock();
}

void CodeEditor::keyPressEvent(QKeyEvent *event)
{
    const bool tab = event->key() == Qt::Key_Tab && event->modifiers() == Qt::NoModifier;
    const bool backtab = event->key() == Qt::Key_Backtab
                         || (event->key() == Qt::Key_Tab && event->modifiers() == Qt::ShiftModifier);
    if (tab && !textCursor().hasSelection()) {
        insertPlainText(indentUnit());
        return;
    }
    if ((tab && textCursor().hasSelection()) || backtab) {
        indentLines(tab ? 1 : -1);
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

int CodeEditor::foldMarkerWidth() const
{
    const int lineSpacing = fontMetrics().lineSpacing();
    return lineSpacing + lineSpacing % 2 + 1;
}

void CodeEditor::setLineNumbersVisible(bool visible)
{
    m_lineNumbers = visible;
    updateGutterWidth();
    m_gutter->update();
}

int CodeEditor::gutterWidth() const
{
    if (!m_lineNumbers)
        return 4 + foldMarkerWidth();
    int digits = 1;
    for (int max = qMax(1, blockCount()); max >= 10; max /= 10)
        ++digits;
    return 8 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits + 4 + foldMarkerWidth();
}

void CodeEditor::updateGutterWidth()
{
    setViewportMargins(gutterWidth(), 0, 0, 0);
    const QRect cr = contentsRect();
    m_gutter->setGeometry(QRect(cr.left(), cr.top(), gutterWidth(), cr.height()));
}

void CodeEditor::updateGutterArea(const QRect &rect, int dy)
{
    if (dy)
        m_gutter->scroll(0, dy);
    else
        m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
    if (rect.contains(viewport()->rect()))
        updateGutterWidth();
}

bool CodeEditor::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == verticalScrollBar() || watched == horizontalScrollBar())
        && (event->type() == QEvent::Enter || event->type() == QEvent::Leave)) {
        auto *bar = static_cast<QWidget *>(watched);
        bar->setProperty("expanded", event->type() == QEvent::Enter);
        bar->style()->unpolish(bar);
        bar->style()->polish(bar);
        bar->update();
    }
    return QPlainTextEdit::eventFilter(watched, event);
}

void CodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    m_gutter->setGeometry(QRect(cr.left(), cr.top(), gutterWidth(), cr.height()));
}

void CodeEditor::setFindTerm(const QString &term)
{
    m_findTerm = term;
    refreshFindSelections();
    highlightCurrentLine();
}

void CodeEditor::refreshFindSelections()
{
    m_findSelections.clear();
    if (m_findTerm.isEmpty() || m_theme == nullptr)
        return;
    QTextCharFormat format;
    const bool dark = m_theme->background.lightness() < 128;
    format.setBackground(dark ? QColor(255, 220, 0, 90) : QColor(255, 200, 0, 140));
    QTextCursor match = document()->find(m_findTerm, 0);
    while (!match.isNull()) {
        QTextEdit::ExtraSelection selection;
        selection.cursor = match;
        selection.format = format;
        m_findSelections.append(selection);
        match = document()->find(m_findTerm, match);
    }
}

void CodeEditor::scheduleOverlayRefresh()
{
    if (m_overlayRefreshQueued)
        return;
    m_overlayRefreshQueued = true;
    QMetaObject::invokeMethod(this, [this] {
        m_overlayRefreshQueued = false;
        refreshFindSelections();
        highlightCurrentLine();
    }, Qt::QueuedConnection);
}

void CodeEditor::highlightCurrentLine()
{
    if (m_theme == nullptr)
        return;
    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(m_theme->currentLine);
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = textCursor();
    selection.cursor.clearSelection();
    QList<QTextEdit::ExtraSelection> selections{selection};
    selections.append(m_findSelections);
    setExtraSelections(selections);
}

void CodeEditor::paintGutter(QPaintEvent *event)
{
    QPainter painter(m_gutter);
    painter.fillRect(event->rect(), m_theme->gutterBackground);
    painter.setFont(font());
    painter.setPen(m_theme->gutterForeground);

    const int numberWidth = m_lineNumbers ? m_gutter->width() - foldMarkerWidth() - 4 : 0;
    const int markerX = m_lineNumbers ? numberWidth + 4 : 0;
    const int lineHeight = fontMetrics().height();

    QTextBlock block = firstVisibleBlock();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            if (m_lineNumbers) {
                painter.drawText(0, top, numberWidth, lineHeight, Qt::AlignRight,
                                 QString::number(block.blockNumber() + 1));
            }
            if (FoldController::isFoldHeader(block)) {
                const QRect markerRect(markerX, top, foldMarkerWidth(), lineHeight);
                drawFoldMarker(painter, markerRect, block.next().isVisible());
            }
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
    }
}

void CodeEditor::drawFoldMarker(QPainter &painter, const QRect &rect, bool expanded) const
{
    QStyleOptionViewItem option;
    option.rect = rect;
    option.state = QStyle::State_Active | QStyle::State_Item | QStyle::State_Children
                   | QStyle::State_Enabled;
    if (expanded)
        option.state |= QStyle::State_Open;
    option.palette = palette();
    option.palette.setColor(QPalette::Text, m_theme->gutterForeground);
    option.palette.setColor(QPalette::WindowText, m_theme->gutterForeground);
    foldMarkerStyle()->drawPrimitive(QStyle::PE_IndicatorBranch, &option, &painter, m_gutter);
}

void CodeEditor::gutterMousePress(QMouseEvent *event)
{
    if (m_lineNumbers && event->pos().x() < m_gutter->width() - foldMarkerWidth())
        return;
    // cursorForPosition with x forced to 0 maps a y-coordinate to its block.
    const QTextBlock block = cursorForPosition(QPoint(0, event->pos().y())).block();
    if (FoldController::isFoldHeader(block))
        m_foldController->toggle(block);
}
