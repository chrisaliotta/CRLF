#pragma once

#include <QColor>
#include <QTabBar>

// Tab bar whose last tab is a "+" new-tab button rather than a document.
// Pressing + requests a new document without starting a tab drag, and a
// dragged document stops at + so the button always stays last.
class DocumentTabBar : public QTabBar
{
    Q_OBJECT

public:
    explicit DocumentTabBar(QWidget *parent = nullptr);

    void setSeparatorColor(const QColor &color);
    // Opaque fill for the scroll arrows so tab titles do not show through them.
    void setScrollButtonColor(const QColor &color);
    // Pinned document tabs occupy the prefix [0, count). The + tab is never pinned.
    void setPinnedCount(int count) { m_pinnedCount = count; }

signals:
    void newTabRequested();
    // Index of the tab under the pointer, or -1 when it leaves the bar.
    void tabHovered(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void tabLayoutChange() override;

private:
    void hoverAt(const QPoint &position);
    void applyScrollButtonColor();

    // Where the press landed inside the pressed tab, and that tab's width;
    // together they bound how far right the drag may carry it.
    qreal m_grabOffset = 0;
    int m_dragWidth = 0;
    int m_hovered = -1;
    int m_pressedIndex = -1;
    int m_pinnedCount = 0;
    QColor m_separatorColor;
    QColor m_scrollButtonColor;
};
