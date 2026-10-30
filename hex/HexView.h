#pragma once

#include <QAbstractScrollArea>
#include <QByteArray>

class Theme;

// Byte-level view and editor for one document: an offset column, sixteen
// hex bytes per row, and an ASCII column. Editing is overwrite-style in
// either pane (hex digits or printable characters), typing at the end
// appends, and Delete/Backspace remove bytes.
class HexView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit HexView(QWidget *parent = nullptr);

    void setData(const QByteArray &data);
    const QByteArray &data() const { return m_data; }

    bool isModified() const { return m_modified; }
    void setModified(bool modified);

    // Byte offset of the cursor; may equal size() (the append position).
    qint64 cursorOffset() const { return m_cursor; }
    void setCursorOffset(qint64 offset);

    void setTheme(const Theme *theme);

    // Bytes shown on each row. The default is 16.
    void setBytesPerRow(int count);
    int bytesPerRow() const { return m_bytesPerRow; }

signals:
    void modificationChanged(bool modified);
    void cursorChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    // Keeps Tab for switching panes instead of moving focus.
    bool focusNextPrevChild(bool next) override;

private:
    enum class Pane { Hex, Ascii };

    // Pixel layout derived from the current font.
    struct Metrics {
        int charWidth;
        int rowHeight;
        int hexX;
        int asciiX;
        int totalWidth;
    };

    Metrics metrics() const;
    int hexByteX(const Metrics &m, int column) const;
    int rowCount() const;
    int visibleRows() const;
    void updateScrollBars();
    void ensureCursorVisible();

    void typeNibble(int value);
    void overwriteByte(qint64 offset, char value);
    void markModified();

    QByteArray m_data;
    int m_bytesPerRow = 16;
    qint64 m_cursor = 0;
    bool m_highNibble = true;
    Pane m_pane = Pane::Hex;
    bool m_modified = false;
    const Theme *m_theme = nullptr;
};
