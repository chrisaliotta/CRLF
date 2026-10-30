#pragma once

#include <QColor>
#include <QWidget>

#include <functional>

class OutputPanel;
class QStackedWidget;
class QToolButton;
class TerminalPanel;

// Output log and interactive shells share the strip under the editor.
class BottomPanel : public QWidget
{
    Q_OBJECT

public:
    explicit BottomPanel(QWidget *parent = nullptr);

    OutputPanel *output() const { return m_output; }
    TerminalPanel *terminal() const { return m_terminal; }

    void setDirectoryProvider(std::function<QString()> provider);
    void setColors(const QColor &foreground, const QColor &background, const QColor &selection);
    QSize sizeHint() const override;

    void showTerminal();
    // Hides the strip when the terminal page is the one showing.
    void hideTerminal();
    void closeSessions();

signals:
    void terminalActive(bool active);
    void opened();

private:
    void presentOutput();
    void showIfHidden();
    void dismiss();
    void setPageChecked(QToolButton *active);

    OutputPanel *m_output = nullptr;
    TerminalPanel *m_terminal = nullptr;
    QStackedWidget *m_stack = nullptr;
    QToolButton *m_outputPage = nullptr;
    QToolButton *m_terminalPage = nullptr;
};
