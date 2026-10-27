#include "editor/Gutter.h"

#include "editor/CodeEditor.h"

#include <QCoreApplication>
#include <QWheelEvent>

Gutter::Gutter(CodeEditor *editor)
    : QWidget(editor)
    , m_editor(editor)
{
}

QSize Gutter::sizeHint() const
{
    return QSize(m_editor->gutterWidth(), 0);
}

void Gutter::paintEvent(QPaintEvent *event)
{
    m_editor->paintGutter(event);
}

void Gutter::mousePressEvent(QMouseEvent *event)
{
    m_editor->gutterMousePress(event);
}

void Gutter::wheelEvent(QWheelEvent *event)
{
    // Scrolling over the gutter should scroll the editor.
    QCoreApplication::sendEvent(m_editor->viewport(), event);
}
