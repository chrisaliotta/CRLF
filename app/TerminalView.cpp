#include "app/TerminalView.h"

#include <utility>

#include <QClipboard>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>

namespace {

constexpr int kScrollback = 5000;

VTermModifier modifiers(Qt::KeyboardModifiers mods)
{
    int value = VTERM_MOD_NONE;
    if (mods & Qt::ShiftModifier)
        value |= VTERM_MOD_SHIFT;
    if (mods & Qt::AltModifier)
        value |= VTERM_MOD_ALT;
    if (mods & Qt::ControlModifier)
        value |= VTERM_MOD_CTRL;
    return VTermModifier(value);
}

VTermKey specialKey(int key)
{
    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return VTERM_KEY_ENTER;
    case Qt::Key_Tab:
        return VTERM_KEY_TAB;
    case Qt::Key_Backspace:
        return VTERM_KEY_BACKSPACE;
    case Qt::Key_Escape:
        return VTERM_KEY_ESCAPE;
    case Qt::Key_Up:
        return VTERM_KEY_UP;
    case Qt::Key_Down:
        return VTERM_KEY_DOWN;
    case Qt::Key_Left:
        return VTERM_KEY_LEFT;
    case Qt::Key_Right:
        return VTERM_KEY_RIGHT;
    case Qt::Key_Insert:
        return VTERM_KEY_INS;
    case Qt::Key_Delete:
        return VTERM_KEY_DEL;
    case Qt::Key_Home:
        return VTERM_KEY_HOME;
    case Qt::Key_End:
        return VTERM_KEY_END;
    case Qt::Key_PageUp:
        return VTERM_KEY_PAGEUP;
    case Qt::Key_PageDown:
        return VTERM_KEY_PAGEDOWN;
    default:
        return VTERM_KEY_NONE;
    }
}

bool shellOwns(int key)
{
    if (key == Qt::Key_QuoteLeft || key == Qt::Key_0 || key == Qt::Key_Plus || key == Qt::Key_Minus
        || key == Qt::Key_Equal)
        return false;
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return true;
    switch (key) {
    case Qt::Key_BracketLeft:
    case Qt::Key_BracketRight:
    case Qt::Key_Backslash:
    case Qt::Key_Backspace:
        return true;
    default:
        return false;
    }
}

} // namespace

int TerminalView::damageThunk(VTermRect rect, void *user)
{
    return static_cast<TerminalView *>(user)->onDamage(rect);
}

int TerminalView::moveCursorThunk(VTermPos pos, VTermPos, int visible, void *user)
{
    return static_cast<TerminalView *>(user)->onMoveCursor(pos, visible);
}

int TerminalView::setTermPropThunk(VTermProp prop, VTermValue *val, void *user)
{
    return static_cast<TerminalView *>(user)->onSetTermProp(prop, val);
}

int TerminalView::bellThunk(void *)
{
    return 1;
}

int TerminalView::resizeThunk(int rows, int cols, void *user)
{
    return static_cast<TerminalView *>(user)->onResize(rows, cols);
}

int TerminalView::pushLineThunk(int cols, const VTermScreenCell *cells, void *user)
{
    return static_cast<TerminalView *>(user)->onPushLine(cols, cells);
}

int TerminalView::popLineThunk(int cols, VTermScreenCell *cells, void *user)
{
    return static_cast<TerminalView *>(user)->onPopLine(cols, cells);
}

int TerminalView::clearThunk(void *user)
{
    return static_cast<TerminalView *>(user)->onClearHistory();
}

void TerminalView::outputThunk(const char *bytes, size_t length, void *user)
{
    static_cast<TerminalView *>(user)->inputGenerated(QByteArray(bytes, int(length)));
}

TerminalView::TerminalView(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_InputMethodEnabled, true);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    QFont font(QStringLiteral("Ubuntu Mono"));
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(11);
    setFont(font);
    m_boldFont = font;
    m_boldFont.setBold(true);
    const QFontMetrics metrics(font);
    m_cellWidth = qMax(1, metrics.horizontalAdvance(QLatin1Char('M')));
    m_cellHeight = qMax(1, metrics.height());

    m_vt = vterm_new(m_rows, m_cols);
    vterm_set_utf8(m_vt, 1);
    vterm_output_set_callback(m_vt, outputThunk, this);
    m_screen = vterm_obtain_screen(m_vt);
    vterm_screen_set_callbacks(m_screen, callbacks(), this);
    vterm_screen_enable_altscreen(m_screen, 1);
    vterm_screen_reset(m_screen, 1);
    setColors(m_foreground, m_background, m_selection);

    m_blink = new QTimer(this);
    m_blink->setInterval(530);
    connect(m_blink, &QTimer::timeout, this, [this] {
        if (!hasFocus())
            return;
        m_cursorPhase = !m_cursorPhase;
        update();
    });
    m_blink->start();
}

TerminalView::~TerminalView()
{
    m_blink->stop();
    vterm_output_set_callback(m_vt, nullptr, nullptr);
    vterm_free(m_vt);
}

const VTermScreenCallbacks *TerminalView::callbacks()
{
    static VTermScreenCallbacks callbacks = {};
    static bool ready = false;
    if (!ready) {
        callbacks.damage = damageThunk;
        callbacks.movecursor = moveCursorThunk;
        callbacks.settermprop = setTermPropThunk;
        callbacks.bell = bellThunk;
        callbacks.resize = resizeThunk;
        callbacks.sb_pushline = pushLineThunk;
        callbacks.sb_popline = popLineThunk;
        callbacks.sb_clear = clearThunk;
        ready = true;
    }
    return &callbacks;
}

void TerminalView::pushBytes(const QByteArray &data)
{
    if (data.isEmpty())
        return;
    vterm_input_write(m_vt, data.constData(), size_t(data.size()));
    vterm_screen_flush_damage(m_screen);
    update();
}

void TerminalView::setColors(const QColor &foreground, const QColor &background, const QColor &selection)
{
    m_foreground = foreground;
    m_background = background;
    m_selection = selection;
    if (!m_screen)
        return;
    VTermColor fg;
    VTermColor bg;
    vterm_color_rgb(&fg, uint8_t(foreground.red()), uint8_t(foreground.green()), uint8_t(foreground.blue()));
    vterm_color_rgb(&bg, uint8_t(background.red()), uint8_t(background.green()), uint8_t(background.blue()));
    vterm_screen_set_default_colors(m_screen, &fg, &bg);
    update();
}

void TerminalView::reset()
{
    m_scrollback.clear();
    m_scrollOffset = 0;
    m_selecting = false;
    m_cursorRow = 0;
    m_cursorCol = 0;
    vterm_screen_reset(m_screen, 1);
    update();
}

void TerminalView::syncSize()
{
    if (width() < m_cellWidth || height() < m_cellHeight)
        return;
    const int cols = qMax(1, width() / m_cellWidth);
    const int rows = qMax(1, height() / m_cellHeight);
    if (rows == m_rows && cols == m_cols)
        return;
    m_rows = rows;
    m_cols = cols;
    vterm_set_size(m_vt, rows, cols);
    emit resized(rows, cols);
    update();
}

void TerminalView::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), m_background);
    const int first = firstLine();
    const int cursorLine = m_scrollback.size() + m_cursorRow;
    for (int row = 0; row < m_rows; ++row) {
        const int line = first + row;
        for (int col = 0; col < m_cols;) {
            VTermScreenCell cell = {};
            const bool have = cellAt(line, col, &cell);
            if (have && cell.chars[0] == uint32_t(-1)) {
                ++col;
                continue;
            }
            const int span = have && cell.width > 1 ? int(cell.width) : 1;
            const QRect area(col * m_cellWidth, row * m_cellHeight, span * m_cellWidth, m_cellHeight);
            const bool selected = cellSelected(line, col);
            const bool cursor = m_cursorVisible && line == cursorLine && col == m_cursorCol && m_scrollOffset == 0;
            const bool fillCursor = cursor && m_cursorShape == VTERM_PROP_CURSORSHAPE_BLOCK && hasFocus() && m_cursorPhase
                                     && !selected;

            QColor background = have ? colorFor(cell.bg, false) : m_background;
            QColor foreground = have ? colorFor(cell.fg, true) : m_foreground;
            if (have && cell.attrs.reverse)
                std::swap(foreground, background);
            if (selected) {
                background = m_selection;
                foreground = m_foreground;
            }
            if (fillCursor)
                std::swap(foreground, background);
            painter.fillRect(area, background);

            const QString text = have && !cell.attrs.conceal ? textFor(cell) : QString();
            if (!text.isEmpty()) {
                painter.setFont(have && cell.attrs.bold ? m_boldFont : font());
                painter.setPen(foreground);
                painter.drawText(area, Qt::AlignLeft | Qt::AlignVCenter, text);
                if (have && cell.attrs.underline) {
                    const int y = area.bottom() - 1;
                    painter.drawLine(area.left(), y, area.right(), y);
                }
            }

            if (cursor && !fillCursor && (m_cursorPhase || !hasFocus())) {
                painter.setPen(foreground);
                if (m_cursorShape == VTERM_PROP_CURSORSHAPE_UNDERLINE)
                    painter.drawLine(area.left(), area.bottom() - 1, area.right(), area.bottom() - 1);
                else if (m_cursorShape == VTERM_PROP_CURSORSHAPE_BAR_LEFT)
                    painter.drawLine(area.left(), area.top(), area.left(), area.bottom());
                else if (!hasFocus())
                    painter.drawRect(area.adjusted(0, 0, -1, -1));
            }
            col += span;
        }
    }
}

void TerminalView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (isVisible())
        syncSize();
}

void TerminalView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    syncSize();
}

void TerminalView::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    if (mods == Qt::ControlModifier && key == Qt::Key_C) {
        if (hasSelection())
            copySelection();
        else {
            stickToBottom();
            emit inputGenerated(QByteArray(1, '\x03'));
        }
        return;
    }
    if (mods == Qt::ControlModifier && key == Qt::Key_V) {
        pasteClipboard();
        return;
    }

    const VTermModifier mod = modifiers(mods);
    const VTermKey special = specialKey(key);
    if (special != VTERM_KEY_NONE) {
        stickToBottom();
        vterm_keyboard_key(m_vt, special, mod);
        return;
    }
    if ((mods & (Qt::ControlModifier | Qt::AltModifier)) != 0 && key >= Qt::Key_A && key <= Qt::Key_Z) {
        stickToBottom();
        vterm_keyboard_unichar(m_vt, uint32_t(QLatin1Char('a').unicode() + (key - Qt::Key_A)), mod);
        return;
    }
    if (!(mods & Qt::ControlModifier) && !event->text().isEmpty()) {
        stickToBottom();
        sendText(event->text());
    }
}

void TerminalView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    setFocus();
    m_anchor = m_focus = pointAt(event->pos());
    m_dragging = true;
    m_selecting = false;
    grabMouse();
    update();
}

void TerminalView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging)
        return;
    m_focus = pointAt(event->pos());
    m_selecting = m_anchor.line != m_focus.line || m_anchor.col != m_focus.col;
    update();
}

void TerminalView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_dragging)
        return;
    m_dragging = false;
    releaseMouse();
}

void TerminalView::wheelEvent(QWheelEvent *event)
{
    const int steps = event->angleDelta().y() > 0 ? 3 : -3;
    m_scrollOffset = qBound(0, m_scrollOffset + steps, m_scrollback.size());
    update();
    event->accept();
}

void TerminalView::inputMethodEvent(QInputMethodEvent *event)
{
    if (!event->commitString().isEmpty())
        sendText(event->commitString());
    event->accept();
}

QVariant TerminalView::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (query == Qt::ImEnabled)
        return true;
    if (query == Qt::ImCursorRectangle) {
        const int row = m_scrollback.size() + m_cursorRow - firstLine();
        return QRect(m_cursorCol * m_cellWidth, row * m_cellHeight, m_cellWidth, m_cellHeight);
    }
    return QWidget::inputMethodQuery(query);
}

bool TerminalView::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        const auto mods = key->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
        if (mods == Qt::ControlModifier && shellOwns(key->key())) {
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

bool TerminalView::focusNextPrevChild(bool)
{
    return false;
}

int TerminalView::onDamage(VTermRect)
{
    update();
    return 1;
}

int TerminalView::onMoveCursor(VTermPos pos, int visible)
{
    m_cursorRow = pos.row;
    m_cursorCol = pos.col;
    m_cursorVisible = visible;
    m_cursorPhase = true;
    update();
    return 1;
}

int TerminalView::onSetTermProp(VTermProp prop, VTermValue *val)
{
    if (prop == VTERM_PROP_CURSORVISIBLE)
        m_cursorVisible = val->boolean;
    else if (prop == VTERM_PROP_CURSORSHAPE)
        m_cursorShape = val->number;
    update();
    return 1;
}

int TerminalView::onResize(int rows, int cols)
{
    m_rows = rows;
    m_cols = cols;
    return 1;
}

int TerminalView::onPushLine(int cols, const VTermScreenCell *cells)
{
    QVector<VTermScreenCell> line(cols);
    for (int col = 0; col < cols; ++col) {
        line[col] = cells[col];
        if (line[col].width < 1)
            line[col].width = 1;
    }
    m_scrollback.append(line);
    if (m_scrollOffset > 0)
        ++m_scrollOffset;
    if (m_scrollback.size() > kScrollback)
        discardHistory(m_scrollback.size() - kScrollback);
    m_scrollOffset = qMin(m_scrollOffset, m_scrollback.size());
    return 1;
}

int TerminalView::onPopLine(int cols, VTermScreenCell *cells)
{
    if (m_scrollback.isEmpty())
        return 0;
    const QVector<VTermScreenCell> line = m_scrollback.takeLast();
    if (m_scrollOffset > 0)
        --m_scrollOffset;
    for (int col = 0; col < cols; ++col) {
        if (col < line.size())
            cells[col] = line[col];
        else
            cells[col] = {};
        if (cells[col].width < 1)
            cells[col].width = 1;
    }
    return 1;
}

int TerminalView::onClearHistory()
{
    m_scrollback.clear();
    m_scrollOffset = 0;
    m_selecting = false;
    return 1;
}

void TerminalView::sendText(const QString &text)
{
    stickToBottom();
    for (int i = 0; i < text.size();) {
        uint codepoint = text.at(i).unicode();
        if (text.at(i).isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
            codepoint = QChar::surrogateToUcs4(text.at(i), text.at(i + 1));
            i += 2;
        } else {
            ++i;
        }
        if (codepoint == '\r')
            continue;
        if (codepoint == '\n')
            vterm_keyboard_key(m_vt, VTERM_KEY_ENTER, VTERM_MOD_NONE);
        else
            vterm_keyboard_unichar(m_vt, codepoint, VTERM_MOD_NONE);
    }
}

void TerminalView::pasteClipboard()
{
    const QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty())
        return;
    stickToBottom();
    vterm_keyboard_start_paste(m_vt);
    sendText(text);
    vterm_keyboard_end_paste(m_vt);
}

void TerminalView::copySelection()
{
    if (!hasSelection())
        return;
    Point start = m_anchor;
    Point end = m_focus;
    if (start.line > end.line || (start.line == end.line && start.col > end.col))
        std::swap(start, end);
    QString text;
    for (int line = start.line; line <= end.line; ++line) {
        const int from = line == start.line ? start.col : 0;
        const int to = line == end.line ? end.col : m_cols - 1;
        QString row;
        for (int col = from; col <= to; ++col) {
            VTermScreenCell cell;
            if (!cellAt(line, col, &cell) || cell.chars[0] == uint32_t(-1))
                continue;
            row += textFor(cell);
        }
        while (row.endsWith(QLatin1Char(' ')))
            row.chop(1);
        if (line != start.line)
            text += QLatin1Char('\n');
        text += row;
    }
    QGuiApplication::clipboard()->setText(text);
}

void TerminalView::stickToBottom()
{
    if (m_scrollOffset == 0)
        return;
    m_scrollOffset = 0;
    update();
}

void TerminalView::discardHistory(int count)
{
    if (count <= 0)
        return;
    m_scrollback.remove(0, count);
    m_anchor.line -= count;
    m_focus.line -= count;
    if (m_anchor.line < 0 || m_focus.line < 0)
        m_selecting = false;
}

int TerminalView::firstLine() const
{
    return qMax(0, m_scrollback.size() - m_scrollOffset);
}

bool TerminalView::cellAt(int line, int col, VTermScreenCell *cell) const
{
    if (line < 0 || col < 0)
        return false;
    if (line < m_scrollback.size()) {
        const QVector<VTermScreenCell> &row = m_scrollback.at(line);
        if (col >= row.size())
            return false;
        *cell = row.at(col);
        return true;
    }
    const int row = line - m_scrollback.size();
    if (row >= m_rows || col >= m_cols)
        return false;
    const VTermPos pos{row, col};
    return vterm_screen_get_cell(m_screen, pos, cell) != 0;
}

TerminalView::Point TerminalView::pointAt(const QPoint &pos) const
{
    const int line = qBound(0, firstLine() + pos.y() / m_cellHeight, m_scrollback.size() + m_rows - 1);
    const int col = qBound(0, pos.x() / m_cellWidth, m_cols - 1);
    return {line, col};
}

bool TerminalView::hasSelection() const
{
    return m_selecting && (m_anchor.line != m_focus.line || m_anchor.col != m_focus.col);
}

bool TerminalView::cellSelected(int line, int col) const
{
    if (!hasSelection())
        return false;
    Point start = m_anchor;
    Point end = m_focus;
    if (start.line > end.line || (start.line == end.line && start.col > end.col))
        std::swap(start, end);
    if (line < start.line || line > end.line)
        return false;
    if (line == start.line && col < start.col)
        return false;
    if (line == end.line && col > end.col)
        return false;
    return true;
}

QString TerminalView::textFor(const VTermScreenCell &cell) const
{
    if (cell.chars[0] == 0 || cell.chars[0] == uint32_t(-1))
        return QString(QLatin1Char(' '));
    QString text;
    for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i) {
        const char32_t codepoint = cell.chars[i];
        text += QString::fromUcs4(&codepoint, 1);
    }
    return text;
}

QColor TerminalView::colorFor(VTermColor color, bool foreground) const
{
    if (foreground && VTERM_COLOR_IS_DEFAULT_FG(&color))
        return m_foreground;
    if (!foreground && VTERM_COLOR_IS_DEFAULT_BG(&color))
        return m_background;
    vterm_screen_convert_color_to_rgb(m_screen, &color);
    return QColor(color.rgb.red, color.rgb.green, color.rgb.blue);
}
