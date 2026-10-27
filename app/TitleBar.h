#pragma once

#include <QWidget>

class QLabel;
class QMenuBar;
class QToolButton;

// Custom window title bar: app icon, the menu bar inlined on the left,
// centered window title, and minimize/maximize/close caption buttons.
// Dragging, snapping, and double-click maximize come from the window's
// native hit testing (see MainWindow::nativeEvent), not from this widget.
class TitleBar : public QWidget
{
    Q_OBJECT

public:
    TitleBar(QMenuBar *menuBar, QWidget *parent = nullptr);

    // True where a press should move the window: empty space, the icon, or
    // the title text. False over the menu bar and the caption buttons.
    bool isDraggableAt(const QPoint &pos) const;

    // Swaps the maximize glyph with the restore glyph.
    void setMaximized(bool maximized);

public slots:
    void setTitle(const QString &title);

private:
    QToolButton *makeCaptionButton(QChar glyph, const QString &objectName,
                                   const QString &toolTip);

    QLabel *m_titleLabel = nullptr;
    QToolButton *m_maximizeButton = nullptr;
};
