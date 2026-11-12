#pragma once

#include <QColor>
#include <QList>
#include <QStringList>
#include <QWidget>

#include <functional>

class ConPtySession;
class QComboBox;
class QStackedWidget;
class QTimer;
class QToolButton;
class TerminalView;

// Several shells. Each keeps running while another is on screen.
// Double-clicking the session name renames it; an empty name restores the
// shell's own label.
class TerminalPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TerminalPanel(QWidget *parent = nullptr);

    void setDirectoryProvider(std::function<QString()> provider);
    void setColors(const QColor &foreground, const QColor &background, const QColor &selection,
                   const QList<QColor> &palette);
    void ensureSession();
    void focusCurrent();
    void closeSessions();
    // Plain text of the session on screen: scrollback, then the visible rows.
    QString transcript() const;
    int sessionCount() const { return m_sessions.size(); }
    // Types `text` into the session on screen, as if it were keyed in.
    void sendInput(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // `label` names the shell that was launched. `alias` is the name the
    // user gave it, and replaces the label while it is not empty.
    struct Session
    {
        ConPtySession *pty = nullptr;
        TerminalView *view = nullptr;
        QString label;
        QString alias;
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
    QString nameFor(int index) const;
    void beginRename();
    // `commit` keeps the typed name; otherwise the name shown before is restored.
    void endRename(bool commit);
    void openSession();
    void restartCurrent();
    void closeCurrent();
    void showSession(int index);
    void updateButtons();

    std::function<QString()> m_directory;
    QColor m_foreground = Qt::white;
    QColor m_background = QColor(0x1e, 0x1e, 0x1e);
    QColor m_selection = QColor(0x26, 0x4f, 0x78);
    QList<QColor> m_palette;
    QList<Session> m_sessions;
    QComboBox *m_sessionsCombo = nullptr;
    // A click on the name opens the list once a double-click can no longer follow.
    QTimer *m_openList = nullptr;
    QStackedWidget *m_stack = nullptr;
    QToolButton *m_restart = nullptr;
    QToolButton *m_closeSession = nullptr;
};
