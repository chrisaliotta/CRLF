#pragma once

#include <QColor>
#include <QList>
#include <QWidget>

#include <functional>

class OutputPanel;
class ProblemsPanel;
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
    ProblemsPanel *problems() const { return m_problems; }
    TerminalPanel *terminal() const { return m_terminal; }

    void setDirectoryProvider(std::function<QString()> provider);
    void setColors(const QColor &foreground, const QColor &background, const QColor &selection,
                   const QList<QColor> &palette);
    QSize sizeHint() const override;

    void showTerminal();
    // Hides the strip when the terminal page is the one showing.
    void hideTerminal();
    void closeSessions();
    // Opens the Problems page. `revealProblems` does that only while the
    // strip is hidden, so a visible terminal or output log stays put.
    void showProblems();
    void revealProblems();
    void setProblemCount(int count);

signals:
    void terminalActive(bool active);
    void opened();

private:
    void presentOutput();
    void showIfHidden();
    void dismiss();
    void setPageChecked(QToolButton *active);
    void refreshProblemCount();

    OutputPanel *m_output = nullptr;
    ProblemsPanel *m_problems = nullptr;
    TerminalPanel *m_terminal = nullptr;
    QStackedWidget *m_stack = nullptr;
    QToolButton *m_outputPage = nullptr;
    QToolButton *m_problemsPage = nullptr;
    QToolButton *m_terminalPage = nullptr;
};
