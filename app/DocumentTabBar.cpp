#include "app/DocumentTabBar.h"

#include <QMouseEvent>

DocumentTabBar::DocumentTabBar(QWidget *parent)
    : QTabBar(parent)
{
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
    const QRect pressed = tabRect(index);
    m_grabOffset = event->position().x() - pressed.left();
    m_dragWidth = pressed.width();
    QTabBar::mousePressEvent(event);
}

void DocumentTabBar::mouseMoveEvent(QMouseEvent *event)
{
    const qreal maxX = tabRect(count() - 1).left() - m_dragWidth + m_grabOffset;
    if (!(event->buttons() & Qt::LeftButton) || event->position().x() <= maxX) {
        QTabBar::mouseMoveEvent(event);
        return;
    }
    QMouseEvent clamped(event->type(), QPointF(maxX, event->position().y()),
                        event->globalPosition(), event->button(), event->buttons(),
                        event->modifiers(), event->pointingDevice());
    QTabBar::mouseMoveEvent(&clamped);
}
