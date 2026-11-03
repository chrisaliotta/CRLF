#pragma once

#include "app/Actions.h"
#include "app/FileIconTheme.h"
#include "editor/Diagnostic.h"
#include "editor/CompletionItem.h"
#include "io/CommandVariables.h"
#include "io/SessionStore.h"
#include "syntax/LanguageRegistry.h"
#include "syntax/Theme.h"

#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QSet>

class BottomPanel;
class CheckRunner;
class CodeEditor;
class CommandRunner;
class CompletionSession;
class ExplorerPanel;
class FindBar;
class OutputPanel;
class ProcessOrchestrator;
struct Project;
class QActionGroup;
class QFileSystemWatcher;
class QLabel;
class QMenu;
class QMenuBar;
class QThread;
class TabWorkspace;
class TitleBar;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

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
    // Returns false when the user cancels the folder picker.
    bool chooseExplorerFolder();
    void chooseWslFolder();
    void offerWslDistros(const QStringList &names);
    void offerWslPath(const QString &distro, const QString &home);
    QString workOwner(QWidget *page) const;
    void closeCurrentTab();
    void gotoLine();
    void applyTheme(const Theme *theme);
    void applyFileIconTheme(const FileIconTheme *theme);
    void applyComposedIcons();
    void editIconTemplates();
    void refreshStatus();
    void persistSession();
    // Reloads the .crlf discovered from the explorer root, or from the active
    // file when no folder is scoped, and places each command on the menu
    // path it names.
    void reloadCommands();
    void applyProject(const Project &project);
    void finishStartup(const QStringList &paths);
    CommandVariables commandVariables() const;
    CommandVariables variablesFor(QWidget *page) const;
    void runCheck(CodeEditor *editor);
    void applyCheck(quintptr editorId, int revision, const QList<Diagnostic> &spans);
    void askCompletion(CodeEditor *editor);
    void applyCompletion(int id, int revision, const QList<CompletionItem> &items);
    QString diagnosticKey(const CodeEditor *editor) const;

    LanguageRegistry m_registry;
    SessionStore m_sessionStore;
    // Built-in themes, then any JSON in the program folder's themes
    // directory. A later file with the same name replaces the earlier one.
    // Pointers into this list stay valid: nothing is added after startup.
    QList<Theme> m_themes;
    // Pointers into this list stay valid: nothing is added after startup.
    QList<FileIconTheme> m_iconThemes;
    // Holds the project composition so the explorer's pointer stays valid.
    FileIconTheme m_composedIcons;
    ProjectIcons m_projectIcons;
    bool m_projectOwnsIcons = false;
    Actions m_actions;

    TabWorkspace *m_tabs = nullptr;
    ExplorerPanel *m_explorer = nullptr;
    FindBar *m_findBar = nullptr;
    BottomPanel *m_bottom = nullptr;
    OutputPanel *m_output = nullptr;
    bool m_syncingTerminal = false;
    bool m_syncingProcesses = false;
    CommandRunner *m_runner = nullptr;
    CheckRunner *m_checks = nullptr;
    CompletionSession *m_completion = nullptr;
    ProcessOrchestrator *m_orchestrator = nullptr;
    QThread *m_thread = nullptr;
    QSet<QString> m_runningCommands;
    QSet<QString> m_editorCommands;
    bool m_checkEnabled = false;
    QString m_checkCommand;
    QString m_checkDirectory;
    bool m_completeEnabled = false;
    QString m_completeCommand;
    QString m_completeDirectory;
    int m_completionId = 0;
    QHash<quintptr, QString> m_diagnosticKeys;
    QFileSystemWatcher *m_commandWatcher = nullptr;
    QString m_projectFile;
    int m_commandGeneration = 0;
    QString m_scopedRoot;
    QString m_saveDirectory;
    QMenuBar *m_menuBar = nullptr;
    QMenu *m_runMenu = nullptr;
    TitleBar *m_titleBar = nullptr;
    QActionGroup *m_languageGroup = nullptr;
    QActionGroup *m_themeGroup = nullptr;
    QActionGroup *m_iconThemeGroup = nullptr;
    QLabel *m_cursorLabel = nullptr;
    QLabel *m_languageLabel = nullptr;
    QLabel *m_encodingLabel = nullptr;
    QLabel *m_eolLabel = nullptr;
};
