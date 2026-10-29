#pragma once

#include <QTabBar>

// Tab bar whose last tab is a "+" new-tab button rather than a document.
// Pressing + requests a new document without starting a tab drag, and a
// dragged document stops at + so the button always stays last.
class DocumentTabBar : public QTabBar
{
    Q_OBJECT

public:
    explicit DocumentTabBar(QWidget *parent = nullptr);

signals:
    void newTabRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    // Where the press landed inside the pressed tab, and that tab's width;
    // together they bound how far right the drag may carry it.
    qreal m_grabOffset = 0;
    int m_dragWidth = 0;
};
