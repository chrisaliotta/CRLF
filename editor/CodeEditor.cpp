#include "editor/CodeEditor.h"

#include "editor/BlockData.h"
#include "editor/FoldController.h"
#include "editor/FoldingLayout.h"
#include "editor/Gutter.h"
#include "editor/SymbolIndex.h"
#include "editor/TailwindNames.h"
#include "syntax/Highlighter.h"
#include "syntax/LanguageDefinition.h"
#include "syntax/Theme.h"

#include <QCursor>
#include <QEvent>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStyleFactory>
#include <QStyleOptionViewItem>
#include <QTextBlock>
#include <QToolTip>

namespace {

// The platform Vista style's branch indicators cannot be recolored, so fold
// markers are always drawn through Fusion.
QStyle *foldMarkerStyle()
{
    static QStyle *style = QStyleFactory::create(QStringLiteral("Fusion"));
    return style;
}

// Starts with an uppercase letter and has a lowercase one, so `Promise`
// qualifies and a constant such as `MAX_SIZE` does not.
bool looksLikeTypeName(const QString &name)
{
    if (name.isEmpty() || !name.at(0).isUpper())
        return false;
    for (const QChar c : name) {
        if (c.isLower())
            return true;
    }
    return false;
}

enum class Attribute { None, Class, Id };

// What the quoted string holding `column` names, on this line: a `class` or
// `className` value, a string passed to a class helper such as clsx or cn
// inside one, or an `id` value.
Attribute attributeAt(const QString &text, int column)
{
    static const QRegularExpression classHead(QStringLiteral(
        "(?:\\bclass(?:Name)?\\s*=\\s*(?:\\{[^}]*)?|\\b(?:clsx|cn|cx|cva|tv|twMerge|twJoin|classNames|classnames)\\s*\\([^)]*|\\btw)$"));
    static const QRegularExpression idHead(QStringLiteral("\\bid\\s*=\\s*\\{?\\s*$"));
    QChar quote;
    int open = -1;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (open >= 0) {
            if (c == QLatin1Char('\\')) {
                ++i;
                continue;
            }
            if (c != quote)
                continue;
            if (column > open && column < i) {
                const QString head = text.left(open);
                if (idHead.match(head).hasMatch())
                    return Attribute::Id;
                return classHead.match(head).hasMatch() ? Attribute::Class : Attribute::None;
            }
            open = -1;
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'') || c == QLatin1Char('`')) {
            quote = c;
            open = i;
        }
    }
    if (open >= 0 && column > open) {
        const QString head = text.left(open);
        if (idHead.match(head).hasMatch())
            return Attribute::Id;
        return classHead.match(head).hasMatch() ? Attribute::Class : Attribute::None;
    }
    return Attribute::None;
}

// The index key for the name at [start, end). `column` is the key's 1-based
// position in the line, and `markStart` is where the underline begins.
// `external` names a stylesheet reference that may live in a package.
struct LinkKey {
    QString name;
    int column = 0;
    int markStart = 0;
    bool external = false;
};

LinkKey linkKeyFor(const QString &text, int start, int end, Attribute attribute, bool stylesheet)
{
    const QString word = text.mid(start, end - start);
    LinkKey key{word, start + 1, start, false};
    static const QRegularExpression applyBefore(QStringLiteral("@apply\\b[^;{}]*$"));
    const bool variant = end < text.size() && text.at(end) == QLatin1Char(':');
    if (attribute == Attribute::Class || (stylesheet && applyBefore.match(text.left(start)).hasMatch())) {
        key.name = (variant ? QStringLiteral("--breakpoint-") : QStringLiteral(".")) + word;
        key.external = true;
        return key;
    }
    if (attribute == Attribute::Id) {
        key.name = QLatin1Char('#') + word;
        return key;
    }
    if (!stylesheet)
        return key;

    static const QRegularExpression extendBefore(QStringLiteral("@extend\\s+\\.$"));
    static const QRegularExpression varBefore(QStringLiteral("var\\(\\s*$"));
    static const QRegularExpression includeBefore(QStringLiteral("(?:@include\\s+|^\\s*\\+)$"));
    static const QRegularExpression colonAfter(QStringLiteral("^\\s*:"));
    const QString head = text.left(start);
    const QChar before = start > 0 ? text.at(start - 1) : QChar();
    if (before == QLatin1Char('@'))
        return {};
    if (before == QLatin1Char('.') || before == QLatin1Char('#')) {
        key.name = before + word;
        key.column = start;
        key.markStart = start - 1;
        key.external = before == QLatin1Char('.') && extendBefore.match(head).hasMatch();
        return key;
    }
    if (word.startsWith(QLatin1Char('$'))) {
        key.external = !colonAfter.match(text.mid(end)).hasMatch();
        return key;
    }
    if (word.startsWith(QLatin1String("--"))) {
        key.external = varBefore.match(head).hasMatch();
        return key;
    }
    key.name = QLatin1Char('@') + word;
    key.external = includeBefore.match(head).hasMatch();
    return key;
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
    viewport()->setMouseTracking(true);

    m_symbolTimer = new QTimer(this);
    m_symbolTimer->setSingleShot(true);
    m_symbolTimer->setInterval(200);
    connect(m_symbolTimer, &QTimer::timeout, this, &CodeEditor::submitSymbols);

    m_checkTimer = new QTimer(this);
    m_checkTimer->setSingleShot(true);
    m_checkTimer->setInterval(500);
    connect(m_checkTimer, &QTimer::timeout, this, &CodeEditor::checkRequested);

    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateGutterWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateGutterArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::scheduleOverlayRefresh);
    connect(document(), &QTextDocument::contentsChange, this, [this](int, int, int) {
        // The link cursor would be applied from inside the edit. Drop the
        // flag here and let the deferred overlay refresh paint without it.
        m_linkActive = false;
        m_linkWord.clear();
        viewport()->setCursor(Qt::IBeamCursor);
        if (!m_diagnostics.isEmpty()) {
            m_diagnostics.clear();
            m_diagnosticLines.clear();
            m_diagnosticTip.clear();
        }
        scheduleOverlayRefresh();
        scheduleSymbolScan();
        if (m_checking) {
            emit checkInvalidated();
            scheduleCheck();
        }
    });

    updateGutterWidth();
}

CodeEditor::~CodeEditor()
{
    m_symbolTimer->stop();
    m_checkTimer->stop();
    // Drop queued overlay and symbol callbacks before the document goes away.
    disconnect(this, nullptr, this, nullptr);
    if (m_symbolIndex != nullptr) {
        disconnect(m_symbolIndex, nullptr, this, nullptr);
        m_symbolIndex->removeBuffer(quintptr(this));
        m_symbolIndex = nullptr;
    }
    delete m_foldController;
    m_foldController = nullptr;
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
    scheduleSymbolScan();
}

void CodeEditor::setSymbolIndex(SymbolIndex *index)
{
    if (m_symbolIndex != nullptr)
        disconnect(m_symbolIndex, nullptr, this, nullptr);
    m_symbolIndex = index;
    if (m_symbolIndex != nullptr) {
        connect(m_symbolIndex, &SymbolIndex::updated, this, [this] {
            m_linkWord.clear();
            if (hasFocus() && QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier))
                updateSymbolLink(viewport()->mapFromGlobal(QCursor::pos()));
        });
    }
    scheduleSymbolScan();
}

void CodeEditor::setFilePath(const QString &path)
{
    const bool changed = m_filePath != path;
    m_filePath = path;
    scheduleSymbolScan();
    if (changed)
        emit filePathChanged();
}

void CodeEditor::setCheckingEnabled(bool enabled)
{
    m_checking = enabled;
    if (enabled) {
        m_checkTimer->start();
        return;
    }
    m_checkTimer->stop();
    if (m_diagnostics.isEmpty())
        return;
    m_diagnostics.clear();
    m_diagnosticLines.clear();
    m_diagnosticTip.clear();
    scheduleOverlayRefresh();
}

void CodeEditor::setDiagnostics(const QList<Diagnostic> &spans)
{
    m_diagnostics = spans;
    m_diagnosticLines.clear();
    for (const Diagnostic &span : spans) {
        const int last = span.endLine > 0 ? span.endLine : span.line;
        for (int line = span.line; line <= last; ++line) {
            const int index = line - 1;
            if (!m_diagnosticLines.contains(index) || !span.warning())
                m_diagnosticLines.insert(index, !span.warning());
        }
    }
    highlightCurrentLine();
    if (m_gutter)
        m_gutter->update();
}

int CodeEditor::documentRevision() const
{
    return document()->revision();
}

void CodeEditor::refreshSymbols()
{
    scheduleSymbolScan();
}

void CodeEditor::goTo(int line, int column)
{
    QTextBlock block = document()->findBlockByNumber(line - 1);
    if (!block.isValid())
        block = document()->lastBlock();
    while (block.isValid() && !block.isVisible()) {
        QTextBlock header = block.previous();
        while (header.isValid() && !(header.isVisible() && FoldController::isFoldHeader(header)))
            header = header.previous();
        if (!header.isValid())
            break;
        m_foldController->setFolded(header, false);
        if (!block.isVisible())
            break;
    }
    const int index = qBound(0, column - 1, qMax(0, block.length() - 1));
    QTextCursor cursor(block);
    cursor.setPosition(block.position() + index);
    setTextCursor(cursor);
    centerCursor();
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

void CodeEditor::applyCommandOutput(const QString &text)
{
    if (toPlainText() == text)
        return;
    const int position = textCursor().position();
    const int scroll = verticalScrollBar()->value();
    if (m_highlighter)
        m_highlighter->setAutomatic(false);
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(text);
    cursor.endEditBlock();
    const int end = qMax(0, int(document()->characterCount()) - 1);
    cursor.setPosition(qMin(position, end));
    setTextCursor(cursor);
    verticalScrollBar()->setValue(scroll);
    if (m_highlighter) {
        if (blockCount() > 200)
            m_highlighter->highlightGradually();
        else {
            m_highlighter->setAutomatic(true);
            m_highlighter->rehighlight();
        }
    }
    // A full-document replace, and the highlight pass after it, can leave
    // the modified flag clear. The text did change, so the tab must show that.
    if (!document()->isModified())
        document()->setModified(true);
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
    if (event->key() == Qt::Key_Control && !event->isAutoRepeat())
        updateSymbolLink(viewport()->mapFromGlobal(QCursor::pos()));
}

void CodeEditor::keyReleaseEvent(QKeyEvent *event)
{
    QPlainTextEdit::keyReleaseEvent(event);
    if (event->key() == Qt::Key_Control && !event->isAutoRepeat())
        clearSymbolLink();
}

void CodeEditor::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && event->modifiers().testFlag(Qt::ControlModifier)) {
        updateSymbolLink(event->position().toPoint());
        if (m_linkActive) {
            if (m_link.external)
                emit externalActivated(m_link);
            else
                emit definitionActivated(m_link.path, m_link.editorId, m_link.line, m_link.column);
            return;
        }
    }
    QPlainTextEdit::mousePressEvent(event);
}

void CodeEditor::mouseMoveEvent(QMouseEvent *event)
{
    QPlainTextEdit::mouseMoveEvent(event);
    const QPoint pos = event->position().toPoint();
    updateSymbolLink(pos);
    updateDiagnosticTip(pos);
}

void CodeEditor::leaveEvent(QEvent *event)
{
    QPlainTextEdit::leaveEvent(event);
    clearSymbolLink();
    m_diagnosticTip.clear();
    QToolTip::hideText();
}

void CodeEditor::focusOutEvent(QFocusEvent *event)
{
    QPlainTextEdit::focusOutEvent(event);
    clearSymbolLink();
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
        if (m_gutter)
            m_gutter->update();
    }, Qt::QueuedConnection);
}

void CodeEditor::scheduleCheck()
{
    if (m_checking)
        m_checkTimer->start();
}

bool CodeEditor::diagnosticRange(const Diagnostic &span, int *start, int *end) const
{
    const QTextBlock startBlock = document()->findBlockByNumber(span.line - 1);
    if (!startBlock.isValid())
        return false;
    const int startColumn = qBound(0, span.column - 1, qMax(0, startBlock.text().size()));
    *start = startBlock.position() + startColumn;

    const int endLine = span.endLine > 0 ? span.endLine : span.line;
    QTextBlock endBlock = document()->findBlockByNumber(endLine - 1);
    if (!endBlock.isValid())
        endBlock = startBlock;
    const int endColumn = span.endColumn > 0 ? span.endColumn - 1 : endBlock.text().size();
    *end = endBlock.position() + qBound(0, endColumn, qMax(0, endBlock.text().size()));
    if (*end <= *start)
        *end = qMin(startBlock.position() + startBlock.text().size(), *start + 1);
    return *end > *start;
}

void CodeEditor::updateDiagnosticTip(const QPoint &pos)
{
    if (m_diagnostics.isEmpty()
        || QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier)) {
        if (!m_diagnosticTip.isEmpty()) {
            m_diagnosticTip.clear();
            QToolTip::hideText();
        }
        return;
    }
    const int position = cursorForPosition(pos).position();
    QString message;
    for (const Diagnostic &span : m_diagnostics) {
        int start = 0;
        int end = 0;
        if (diagnosticRange(span, &start, &end) && position >= start && position < end) {
            message = span.message;
            break;
        }
    }
    if (message == m_diagnosticTip)
        return;
    m_diagnosticTip = message;
    if (message.isEmpty())
        QToolTip::hideText();
    else
        QToolTip::showText(viewport()->mapToGlobal(pos), message, viewport());
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
    if (m_theme != nullptr) {
        for (const Diagnostic &span : m_diagnostics) {
            int start = 0;
            int end = 0;
            if (!diagnosticRange(span, &start, &end))
                continue;
            QTextEdit::ExtraSelection mark;
            mark.cursor = QTextCursor(document());
            mark.cursor.setPosition(start);
            mark.cursor.setPosition(end, QTextCursor::KeepAnchor);
            mark.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            mark.format.setUnderlineColor(span.warning() ? m_theme->diagnosticWarning
                                                         : m_theme->diagnosticError);
            selections.append(mark);
        }
    }
    if (m_linkActive)
        selections.append(m_linkSelection);
    setExtraSelections(selections);
}

void CodeEditor::scheduleSymbolScan()
{
    if (m_symbolIndex != nullptr && m_symbolIndex->isEnabled() && m_definition != nullptr)
        m_symbolTimer->start();
}

void CodeEditor::submitSymbols()
{
    if (m_symbolIndex != nullptr && m_definition != nullptr)
        m_symbolIndex->updateBuffer(quintptr(this), m_filePath, toPlainText(), m_definition);
}

void CodeEditor::updateSymbolLink(const QPoint &pos)
{
    if (m_symbolIndex == nullptr || m_definition == nullptr
        || !QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier)
        || !viewport()->rect().contains(pos)) {
        clearSymbolLink();
        return;
    }

    const QTextCursor at = cursorForPosition(pos);
    const QString text = at.block().text();
    int column = at.positionInBlock();
    const Attribute attribute =
        m_definition->classAttributes ? attributeAt(text, column) : Attribute::None;
    const bool dashed = m_definition->stylesheet || attribute != Attribute::None;
    const bool classContext = attribute == Attribute::Class || m_definition->stylesheet;
    const auto isIdent = [dashed](QChar c) {
        return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('$')
               || (dashed && c == QLatin1Char('-'));
    };
    if ((column >= text.size() || !isIdent(text.at(column))) && column > 0 && isIdent(text.at(column - 1)))
        --column;
    if (column >= text.size() || !isIdent(text.at(column))) {
        clearSymbolLink();
        return;
    }
    int start = column;
    while (start > 0 && isIdent(text.at(start - 1)))
        --start;
    int end = column + 1;
    while (end < text.size() && isIdent(text.at(end)))
        ++end;
    const QString word = text.mid(start, end - start);
    static const QRegularExpression nameStart(QStringLiteral("[^-$]"));
    const int firstLetter = word.indexOf(nameStart);
    if (firstLetter < 0 || word.at(firstLetter).isDigit()) {
        clearSymbolLink();
        return;
    }
    const LinkKey key = linkKeyFor(text, start, end, attribute, m_definition->stylesheet);
    if (key.name.isEmpty()) {
        clearSymbolLink();
        return;
    }
    const int line = at.blockNumber() + 1;
    if (key.name == m_linkWord && line == m_linkWordLine && key.column == m_linkWordColumn)
        return;

    m_linkWord = key.name;
    m_linkWordLine = line;
    m_linkWordColumn = key.column;
    QString object;
    if (m_definition->modules && start >= 2 && text.at(start - 1) == QLatin1Char('.')) {
        int objectStart = start - 1;
        while (objectStart > 0 && (text.at(objectStart - 1).isLetterOrNumber() || text.at(objectStart - 1) == QLatin1Char('_')
                                   || text.at(objectStart - 1) == QLatin1Char('$')))
            --objectStart;
        object = text.mid(objectStart, start - 1 - objectStart);
    }
    SymbolLocation loc;
    if (!object.isEmpty())
        loc = m_symbolIndex->findMember(quintptr(this), object, word);
    if (loc.line == 0 && !loc.external)
        loc = m_symbolIndex->find(key.name, m_definition->ignoreCase, m_filePath, quintptr(this), line, key.column);
    if (loc.line == 0 && !loc.external && classContext && key.name.startsWith(QLatin1Char('.'))) {
        for (const QString &variable : parseTailwindClass(key.name.mid(1)).variables) {
            loc = m_symbolIndex->find(variable, false, m_filePath, quintptr(this), line, key.column);
            if (loc.line != 0)
                break;
        }
    }
    if (loc.line == 0 && !loc.external && key.external && m_symbolIndex->externalEnabled()) {
        loc.external = true;
        loc.kind = ExternalKind::Stylesheet;
        loc.path = m_filePath;
        loc.editorId = quintptr(this);
        loc.name = key.name;
    }
    // An undefined PascalCase name in a module file is an ambient declaration,
    // such as HTMLDivElement or JSX.Element, from the TypeScript libraries or @types.
    const auto undefinedName = [this](const QString &name) {
        const SymbolLocation owner = m_symbolIndex->find(name, false, m_filePath, quintptr(this), 0, 0);
        return owner.line == 0 && !owner.external;
    };
    if (loc.line == 0 && !loc.external && m_definition->modules && !classContext
        && m_symbolIndex->externalEnabled() && looksLikeTypeName(word)
        && (object.isEmpty() || (looksLikeTypeName(object) && undefinedName(object)))) {
        loc.external = true;
        loc.kind = ExternalKind::Global;
        loc.path = m_filePath;
        loc.editorId = quintptr(this);
        loc.name = word;
        loc.container = object;
    }
    if (loc.line == 0 && !loc.external) {
        if (m_linkActive) {
            m_linkActive = false;
            viewport()->setCursor(Qt::IBeamCursor);
            highlightCurrentLine();
        }
        return;
    }

    QTextCursor mark(at.block());
    const int from = at.block().position() + key.markStart;
    mark.setPosition(from);
    mark.setPosition(at.block().position() + end, QTextCursor::KeepAnchor);
    QTextCharFormat format;
    format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
    if (m_theme != nullptr) {
        const QColor color = loc.external ? m_theme->tokenFormat(TokenRole::Function).foreground().color()
                                          : m_theme->foreground;
        format.setUnderlineColor(color);
        if (loc.external)
            format.setForeground(color);
    }
    m_linkSelection.cursor = mark;
    m_linkSelection.format = format;
    m_linkActive = true;
    m_link = loc;
    viewport()->setCursor(Qt::PointingHandCursor);
    highlightCurrentLine();
}

void CodeEditor::clearSymbolLink()
{
    if (!m_linkActive && m_linkWord.isEmpty())
        return;
    m_linkActive = false;
    m_linkWord.clear();
    m_link = {};
    viewport()->setCursor(Qt::IBeamCursor);
    highlightCurrentLine();
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
            const auto mark = m_diagnosticLines.constFind(block.blockNumber());
            if (mark != m_diagnosticLines.cend() && m_theme != nullptr) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(mark.value() ? m_theme->diagnosticError : m_theme->diagnosticWarning);
                const int size = 6;
                painter.drawRect(1, top + (lineHeight - size) / 2, size, size);
                painter.setPen(m_theme->gutterForeground);
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
