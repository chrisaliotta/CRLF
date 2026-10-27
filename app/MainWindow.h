#pragma once

#include "app/Actions.h"
#include "io/SessionStore.h"
#include "syntax/LanguageRegistry.h"
#include "syntax/Theme.h"

#include <QMainWindow>

class FindBar;
class QActionGroup;
class QLabel;
class QMenuBar;
class TabWorkspace;
class TitleBar;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Restores the persisted session (when enabled), opens any command-line
    // paths on top, and guarantees at least one tab.
    void startSession(const QStringList &paths);
    // Files handed over by a second application launch.
    void openForwardedPaths(const QStringList &paths);

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    // Replaces the native title bar with the custom one while keeping the
    // OS-provided resize borders, drag, snapping, and rounded corners.
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    void buildMenus();
    void applyCustomFrame();
    void connectActions();
    void openFileDialog();
    void closeCurrentTab();
    void gotoLine();
    void applyTheme(const Theme *theme);
    void refreshStatus();
    void persistSession();

    LanguageRegistry m_registry;
    SessionStore m_sessionStore;
    Theme m_darkTheme;
    Theme m_lightTheme;
    Actions m_actions;

    TabWorkspace *m_tabs = nullptr;
    FindBar *m_findBar = nullptr;
    QMenuBar *m_menuBar = nullptr;
    TitleBar *m_titleBar = nullptr;
    QActionGroup *m_languageGroup = nullptr;
    QLabel *m_cursorLabel = nullptr;
    QLabel *m_languageLabel = nullptr;
    QLabel *m_encodingLabel = nullptr;
    QLabel *m_eolLabel = nullptr;
};
