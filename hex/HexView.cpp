#include "hex/HexView.h"

#include "syntax/Theme.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>

namespace {

constexpr int kBytesPerRow = 16;
constexpr int kOffsetDigits = 8;
// Blank columns between the offset, hex, and ASCII panes.
constexpr int kPaneGap = 2;

int hexDigitValue(QChar c)
{
    if (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
        return c.unicode() - '0';
    const QChar lower = c.toLower();
    if (lower >= QLatin1Char('a') && lower <= QLatin1Char('f'))
        return lower.unicode() - 'a' + 10;
    return -1;
}

QString hexByte(unsigned char value)
{
    return QString::number(value, 16).rightJustified(2, QLatin1Char('0')).toUpper();
}

bool isPrintableAscii(unsigned char value)
{
    return value >= 0x20 && value < 0x7F;
}

} // namespace

HexView::HexView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    QFont hexFont(QStringLiteral("Ubuntu Mono"));
    hexFont.setStyleHint(QFont::Monospace);
    hexFont.setPointSize(12);
    setFont(hexFont);

    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    updateScrollBars();
}

void HexView::setData(const QByteArray &data)
{
    m_data = data;
    m_cursor = 0;
    m_highNibble = true;
    m_modified = false;
    updateScrollBars();
    viewport()->update();
}

void HexView::setModified(bool modified)
{
    if (m_modified == modified)
        return;
    m_modified = modified;
    emit modificationChanged(modified);
}

void HexView::setCursorOffset(qint64 offset)
{
    m_cursor = qBound<qint64>(0, offset, m_data.size());
    m_highNibble = true;
    ensureCursorVisible();
    viewport()->update();
    emit cursorChanged();
}

void HexView::setTheme(const Theme *theme)
{
    m_theme = theme;
    viewport()->update();
}

HexView::Metrics HexView::metrics() const
{
    const QFontMetrics fm(font());
    Metrics m;
    m.charWidth = fm.horizontalAdvance(QLatin1Char('0'));
    m.rowHeight = fm.lineSpacing();
    m.hexX = (1 + kOffsetDigits + kPaneGap) * m.charWidth;
    // "XX " per byte plus one extra column splitting the row in halves.
    const int hexWidth = (kBytesPerRow * 3 + 1) * m.charWidth;
    m.asciiX = m.hexX + hexWidth + kPaneGap * m.charWidth;
    m.totalWidth = m.asciiX + (kBytesPerRow + 1) * m.charWidth;
    return m;
}

int HexView::hexByteX(const Metrics &m, int column) const
{
    const int halfGap = column >= kBytesPerRow / 2 ? m.charWidth : 0;
    return m.hexX + column * 3 * m.charWidth + halfGap;
}

int HexView::rowCount() const
{
    // One extra row hosts the append position when the size is a multiple
    // of the row length.
    return int(m_data.size() / kBytesPerRow) + 1;
}

int HexView::visibleRows() const
{
    return qMax(1, viewport()->height() / metrics().rowHeight);
}

void HexView::updateScrollBars()
{
    const Metrics m = metrics();
    verticalScrollBar()->setRange(0, qMax(0, rowCount() - visibleRows()));
    verticalScrollBar()->setPageStep(visibleRows());
    horizontalScrollBar()->setRange(0, qMax(0, m.totalWidth - viewport()->width()));
    horizontalScrollBar()->setPageStep(viewport()->width());
}

void HexView::ensureCursorVisible()
{
    const int row = int(m_cursor / kBytesPerRow);
    const int first = verticalScrollBar()->value();
    if (row < first)
        verticalScrollBar()->setValue(row);
    else if (row >= first + visibleRows())
        verticalScrollBar()->setValue(row - visibleRows() + 1);
}

void HexView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

bool HexView::focusNextPrevChild(bool)
{
    return false;
}

void HexView::paintEvent(QPaintEvent *)
{
    QPainter painter(viewport());
    painter.setFont(font());
    const Metrics m = metrics();
    const int x0 = -horizontalScrollBar()->value();
    const int firstRow = verticalScrollBar()->value();
    const int cursorRow = int(m_cursor / kBytesPerRow);
    const int cursorColumn = int(m_cursor % kBytesPerRow);

    painter.fillRect(viewport()->rect(), m_theme->background);
    painter.fillRect(QRect(x0, 0, (1 + kOffsetDigits + 1) * m.charWidth, viewport()->height()),
                     m_theme->gutterBackground);

    for (int row = firstRow; row < firstRow + visibleRows() + 1 && row < rowCount(); ++row) {
        const int y = (row - firstRow) * m.rowHeight;

        if (row == cursorRow) {
            painter.fillRect(QRect(x0 + m.hexX - m.charWidth, y,
                                   m.totalWidth - m.hexX + m.charWidth, m.rowHeight),
                             m_theme->currentLine);
        }

        painter.setPen(m_theme->gutterForeground);
        painter.drawText(QRect(x0 + m.charWidth, y, kOffsetDigits * m.charWidth, m.rowHeight),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QString::number(qint64(row) * kBytesPerRow, 16)
                             .rightJustified(kOffsetDigits, QLatin1Char('0'))
                             .toUpper());

        for (int column = 0; column < kBytesPerRow; ++column) {
            const qint64 offset = qint64(row) * kBytesPerRow + column;
            const QRect hexRect(x0 + hexByteX(m, column), y, 2 * m.charWidth, m.rowHeight);
            const QRect asciiRect(x0 + m.asciiX + column * m.charWidth, y, m.charWidth, m.rowHeight);

            if (offset == m_cursor) {
                // Solid highlight in the pane being edited, outline in the other.
                const QRect &active = m_pane == Pane::Hex ? hexRect : asciiRect;
                const QRect &passive = m_pane == Pane::Hex ? asciiRect : hexRect;
                painter.fillRect(active, m_theme->selection);
                painter.setPen(m_theme->selection);
                painter.drawRect(passive.adjusted(0, 0, -1, -1));
                if (m_pane == Pane::Hex && offset < m_data.size()) {
                    const int nibbleX = hexRect.x() + (m_highNibble ? 0 : m.charWidth);
                    painter.fillRect(QRect(nibbleX, y + m.rowHeight - 2, m.charWidth, 2),
                                     m_theme->foreground);
                }
            }
            if (offset >= m_data.size())
                break;

            const auto value = static_cast<unsigned char>(m_data.at(offset));
            painter.setPen(m_theme->foreground);
            painter.drawText(hexRect, Qt::AlignCenter, hexByte(value));
            if (isPrintableAscii(value)) {
                painter.drawText(asciiRect, Qt::AlignCenter, QChar(value));
            } else {
                painter.setPen(m_theme->gutterForeground);
                painter.drawText(asciiRect, Qt::AlignCenter, QStringLiteral("."));
            }
        }
    }
}

void HexView::keyPressEvent(QKeyEvent *event)
{
    const qint64 size = m_data.size();
    const bool control = event->modifiers() & Qt::ControlModifier;
    const qint64 rowStart = m_cursor - m_cursor % kBytesPerRow;

    switch (event->key()) {
    case Qt::Key_Left: setCursorOffset(m_cursor - 1); return;
    case Qt::Key_Right: setCursorOffset(m_cursor + 1); return;
    case Qt::Key_Up: setCursorOffset(m_cursor - kBytesPerRow); return;
    case Qt::Key_Down: setCursorOffset(m_cursor + kBytesPerRow); return;
    case Qt::Key_PageUp: setCursorOffset(m_cursor - qint64(kBytesPerRow) * visibleRows()); return;
    case Qt::Key_PageDown: setCursorOffset(m_cursor + qint64(kBytesPerRow) * visibleRows()); return;
    case Qt::Key_Home: setCursorOffset(control ? 0 : rowStart); return;
    case Qt::Key_End: setCursorOffset(control ? size : rowStart + kBytesPerRow - 1); return;
    case Qt::Key_Tab:
        m_pane = m_pane == Pane::Hex ? Pane::Ascii : Pane::Hex;
        m_highNibble = true;
        viewport()->update();
        return;
    case Qt::Key_Delete:
        if (m_cursor < size) {
            m_data.remove(m_cursor, 1);
            markModified();
        }
        return;
    case Qt::Key_Backspace:
        if (m_cursor > 0) {
            m_data.remove(m_cursor - 1, 1);
            markModified();
            setCursorOffset(m_cursor - 1);
        }
        return;
    default:
        break;
    }

    const QString text = event->text();
    if (text.size() == 1 && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
        const QChar c = text.at(0);
        if (m_pane == Pane::Hex) {
            const int value = hexDigitValue(c);
            if (value >= 0) {
                typeNibble(value);
                return;
            }
        } else if (c.unicode() < 0x7F && isPrintableAscii(static_cast<unsigned char>(c.unicode()))) {
            overwriteByte(m_cursor, char(c.unicode()));
            setCursorOffset(m_cursor + 1);
            return;
        }
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void HexView::typeNibble(int value)
{
    if (m_cursor == m_data.size())
        m_data.append('\0');
    auto byte = static_cast<unsigned char>(m_data.at(m_cursor));
    if (m_highNibble) {
        byte = static_cast<unsigned char>((value << 4) | (byte & 0x0F));
        m_data[int(m_cursor)] = char(byte);
        m_highNibble = false;
        markModified();
    } else {
        byte = static_cast<unsigned char>((byte & 0xF0) | value);
        m_data[int(m_cursor)] = char(byte);
        markModified();
        setCursorOffset(m_cursor + 1);
    }
}

void HexView::overwriteByte(qint64 offset, char value)
{
    if (offset == m_data.size())
        m_data.append(value);
    else
        m_data[int(offset)] = value;
    markModified();
}

void HexView::markModified()
{
    updateScrollBars();
    viewport()->update();
    setModified(true);
}

void HexView::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    const Metrics m = metrics();
    const int x = int(event->position().x()) + horizontalScrollBar()->value();
    const int row = verticalScrollBar()->value() + int(event->position().y()) / m.rowHeight;

    int column = -1;
    if (x >= m.asciiX && x < m.asciiX + kBytesPerRow * m.charWidth) {
        m_pane = Pane::Ascii;
        column = (x - m.asciiX) / m.charWidth;
    } else if (x >= m.hexX && x < m.asciiX) {
        m_pane = Pane::Hex;
        column = kBytesPerRow - 1;
        for (int c = 0; c < kBytesPerRow; ++c) {
            if (x < hexByteX(m, c) + 3 * m.charWidth) {
                column = c;
                break;
            }
        }
    }
    if (column >= 0)
        setCursorOffset(qint64(row) * kBytesPerRow + column);
}
