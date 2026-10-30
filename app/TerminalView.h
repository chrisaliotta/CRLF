#pragma once

#include <vterm.h>

#include <QColor>
#include <QVector>
#include <QWidget>

class QTimer;

// A cell grid for one shell. Bytes from the pseudoconsole are VT; keys go back
// out as inputGenerated.
class TerminalView : public QWidget
{
    Q_OBJECT

public:
    explicit TerminalView(QWidget *parent = nullptr);
    ~TerminalView() override;

    void pushBytes(const QByteArray &data);
    void setColors(const QColor &foreground, const QColor &background, const QColor &selection);
    void reset();
    // Tells the pseudoconsole the cell size once the widget has one.
    void syncSize();

    int rows() const { return m_rows; }
    int cols() const { return m_cols; }

signals:
    void inputGenerated(const QByteArray &data);
    void resized(int rows, int cols);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    bool event(QEvent *event) override;
    bool focusNextPrevChild(bool next) override;

private:
    struct Point
    {
        int line = 0;
        int col = 0;
    };

    static const VTermScreenCallbacks *callbacks();
    static int damageThunk(VTermRect rect, void *user);
    static int moveCursorThunk(VTermPos pos, VTermPos oldPos, int visible, void *user);
    static int setTermPropThunk(VTermProp prop, VTermValue *val, void *user);
    static int bellThunk(void *user);
    static int resizeThunk(int rows, int cols, void *user);
    static int pushLineThunk(int cols, const VTermScreenCell *cells, void *user);
    static int popLineThunk(int cols, VTermScreenCell *cells, void *user);
    static int clearThunk(void *user);
    static void outputThunk(const char *bytes, size_t length, void *user);

    int onDamage(VTermRect rect);
    int onMoveCursor(VTermPos pos, int visible);
    int onSetTermProp(VTermProp prop, VTermValue *val);
    int onResize(int rows, int cols);
    int onPushLine(int cols, const VTermScreenCell *cells);
    int onPopLine(int cols, VTermScreenCell *cells);
    int onClearHistory();

    void sendText(const QString &text);
    void pasteClipboard();
    void copySelection();
    void stickToBottom();
    void discardHistory(int count);
    int firstLine() const;
    bool cellAt(int line, int col, VTermScreenCell *cell) const;
    Point pointAt(const QPoint &pos) const;
    bool hasSelection() const;
    bool cellSelected(int line, int col) const;
    QString textFor(const VTermScreenCell &cell) const;
    QColor colorFor(VTermColor color, bool foreground) const;

    VTerm *m_vt = nullptr;
    VTermScreen *m_screen = nullptr;
    QTimer *m_blink = nullptr;
    QFont m_boldFont;
    QColor m_foreground = Qt::white;
    QColor m_background = QColor(0x1e, 0x1e, 0x1e);
    QColor m_selection = QColor(0x26, 0x4f, 0x78);
    QVector<QVector<VTermScreenCell>> m_scrollback;
    int m_scrollOffset = 0;
    int m_rows = 24;
    int m_cols = 80;
    int m_cellWidth = 1;
    int m_cellHeight = 1;
    int m_cursorRow = 0;
    int m_cursorCol = 0;
    int m_cursorShape = VTERM_PROP_CURSORSHAPE_BLOCK;
    bool m_cursorVisible = true;
    bool m_cursorPhase = true;
    bool m_dragging = false;
    bool m_selecting = false;
    Point m_anchor;
    Point m_focus;
};
