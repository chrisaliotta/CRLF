#pragma once

#include <QWidget>

class CodeEditor;

// Thin child widget for the left margin. All painting and hit-testing logic
// lives in CodeEditor, which owns the geometry data the gutter needs.
class Gutter : public QWidget
{
public:
    explicit Gutter(CodeEditor *editor);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    CodeEditor *m_editor;
};
