#include "app/DocumentTabBar.h"

#include <QCursor>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>

DocumentTabBar::DocumentTabBar(QWidget *parent)
    : QTabBar(parent)
{
    setMouseTracking(true);
}

void DocumentTabBar::setSeparatorColor(const QColor &color)
{
    m_separatorColor = color;
    update();
}

void DocumentTabBar::setScrollButtonColor(const QColor &color)
{
    m_scrollButtonColor = color;
    applyScrollButtonColor();
}

void DocumentTabBar::applyScrollButtonColor()
{
    if (!m_scrollButtonColor.isValid())
        return;
    // Scroll arrows are direct children. Close buttons live inside the
    // right-side wrapper and are skipped by the object name.
    const auto buttons = findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly);
    for (QToolButton *button : buttons) {
        if (button->objectName() == QLatin1String("tabCloseButton"))
            continue;
        if (button->autoFillBackground()
            && button->palette().color(QPalette::Window) == m_scrollButtonColor)
            continue;
        QPalette palette = button->palette();
        palette.setColor(QPalette::Window, m_scrollButtonColor);
        button->setPalette(palette);
        button->setAutoFillBackground(true);
    }
}

void DocumentTabBar::tabLayoutChange()
{
    QTabBar::tabLayoutChange();
    // The arrows are created the first time the tabs overflow.
    applyScrollButtonColor();
}

void DocumentTabBar::paintEvent(QPaintEvent *event)
{
    QTabBar::paintEvent(event);
    if (!m_separatorColor.isValid() || count() < 2)
        return;
    QPainter painter(this);
    painter.setPen(m_separatorColor);
    // The gap between tabs, stopping before a line after the + button.
    for (int i = 0; i < count() - 1; ++i) {
        const QRect left = tabRect(i);
        const QRect right = tabRect(i + 1);
        const int x = (left.right() + right.left()) / 2;
        painter.drawLine(x, left.top() + 8, x, left.bottom() - 6);
    }
}

void DocumentTabBar::hoverAt(const QPoint &position)
{
    const int index = tabAt(position);
    if (index == m_hovered)
        return;
    m_hovered = index;
    emit tabHovered(index);
}

void DocumentTabBar::mousePressEvent(QMouseEvent *event)
{
    const int index = tabAt(event->position().toPoint());
    // QTabBar resolves the pressed tab after its click signal, so letting the
    // + press through would turn the freshly inserted document into a drag.
    if (index == count() - 1) {
        if (event->button() == Qt::LeftButton)
            emit newTabRequested();
        return;
    }
    // The context menu handles the right button. A drag starts on the left.
    if (event->button() != Qt::LeftButton)
        return;
    const QRect pressed = tabRect(index);
    m_pressedIndex = index;
    m_grabOffset = event->position().x() - pressed.left();
    m_dragWidth = pressed.width();
    hoverAt(event->position().toPoint());
    QTabBar::mousePressEvent(event);
}

void DocumentTabBar::mouseMoveEvent(QMouseEvent *event)
{
    hoverAt(event->position().toPoint());
    if (!(event->buttons() & Qt::LeftButton) || m_pressedIndex < 0) {
        QTabBar::mouseMoveEvent(event);
        return;
    }
    // Keep the dragged tab out of the + slot, and keep pinned tabs on the
    // left of every unpinned tab.
    qreal maxX = tabRect(count() - 1).left() - m_dragWidth + m_grabOffset;
    qreal minX = m_grabOffset;
    const bool pinned = m_pressedIndex < m_pinnedCount;
    if (pinned && m_pinnedCount > 0 && m_pinnedCount < count() - 1)
        maxX = qMin(maxX, tabRect(m_pinnedCount).left() - m_dragWidth + m_grabOffset);
    if (!pinned && m_pinnedCount > 0)
        minX = tabRect(m_pinnedCount).left() + m_grabOffset;
    const qreal x = qBound(minX, event->position().x(), maxX);
    if (x == event->position().x()) {
        QTabBar::mouseMoveEvent(event);
        return;
    }
    QMouseEvent clamped(event->type(), QPointF(x, event->position().y()),
                        event->globalPosition(), event->button(), event->buttons(),
                        event->modifiers(), event->pointingDevice());
    QTabBar::mouseMoveEvent(&clamped);
}

void DocumentTabBar::mouseReleaseEvent(QMouseEvent *event)
{
    m_pressedIndex = -1;
    QTabBar::mouseReleaseEvent(event);
}

void DocumentTabBar::leaveEvent(QEvent *event)
{
    // Moving onto the close button or title label stays inside this bar.
    if (!rect().contains(mapFromGlobal(QCursor::pos())) && m_hovered != -1) {
        m_hovered = -1;
        emit tabHovered(-1);
    }
    QTabBar::leaveEvent(event);
}
