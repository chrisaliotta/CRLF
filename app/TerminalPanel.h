#pragma once

#include <QColor>
#include <QList>
#include <QStringList>
#include <QWidget>

#include <functional>

class ConPtySession;
class QComboBox;
class QStackedWidget;
class QToolButton;
class TerminalView;

// Several shells. Each keeps running while another is on screen.
class TerminalPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TerminalPanel(QWidget *parent = nullptr);

    void setDirectoryProvider(std::function<QString()> provider);
    void setColors(const QColor &foreground, const QColor &background, const QColor &selection);
    void ensureSession();
    void focusCurrent();
    void closeSessions();
    int sessionCount() const { return m_sessions.size(); }

private:
    struct Session
    {
        ConPtySession *pty = nullptr;
        TerminalView *view = nullptr;
    };

    struct Launch
    {
        QString program;
        QStringList arguments;
        QString workingDirectory;
        QString label;
    };

    Launch launchFor(const QString &directory) const;
    QString uniqueLabel(const QString &base, int ignoreIndex) const;
    void openSession();
    void restartCurrent();
    void closeCurrent();
    void showSession(int index);
    void updateButtons();

    std::function<QString()> m_directory;
    QColor m_foreground = Qt::white;
    QColor m_background = QColor(0x1e, 0x1e, 0x1e);
    QColor m_selection = QColor(0x26, 0x4f, 0x78);
    QList<Session> m_sessions;
    QComboBox *m_sessionsCombo = nullptr;
    QStackedWidget *m_stack = nullptr;
    QToolButton *m_restart = nullptr;
    QToolButton *m_closeSession = nullptr;
};
