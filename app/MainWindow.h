#pragma once

#include "app/Actions.h"
#include "app/FileIconTheme.h"
#include "app/ProcessOrchestrator.h"
#include "editor/CompletionItem.h"
#include "editor/Diagnostic.h"
#include "app/ProjectTrustGate.h"
#include "io/CommandVariables.h"
#include "io/ProjectCommands.h"
#include "io/ProjectTrust.h"
#include "io/SessionStore.h"
#include "syntax/LanguageRegistry.h"
#include "syntax/Theme.h"

#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QSet>

#include <functional>

class AgentController;
class AgentPanel;
class AgentSession;
class BottomPanel;
class SyntaxRunner;
class CodeEditor;
class CommandRunner;
class CompletionSession;
class ExplorerPanel;
class GitClient;
class SourceControl;
class SourceControlPanel;
struct GitDiffRequest;
class FindBar;
class OutputPanel;
class ProcessOrchestrator;
struct Project;
class QActionGroup;
class QFileSystemWatcher;
class QLabel;
class QMenu;
class QMenuBar;
class QSplitter;
class QThread;
class QTimer;
class QToolButton;
class TabWorkspace;
class TitleBar;
class TrustBar;

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
    // Reloads the project found from the explorer root, or from the active
    // file when no folder is scoped, and places each command on the menu
    // path it names. `restart` restarts the services once the files are
    // read, because a .crlf file or a script it watches changed.
    void reloadCommands(bool restart = false);
    void applyProject(const Project &project, bool restart);
    // Runs `save` on the pool with the folder whose workspace files take an
    // edited setting, then reloads the project.
    void saveProjectSetting(std::function<bool(const QString &directory)> save);
    // The application file in use: the one chosen under View > Edit
    // Defaults > Application Settings, or application.crlf.
    QString applicationFile() const;
    // One checkable item per <name>.crlf file in the application folder,
    // then the items that open the active file and the folder.
    void fillApplicationMenu(QMenu *menu);
    // File > Copy Application Files to Workspace, for the scoped folder.
    void copyApplicationFiles();
    void showTrustReview();
    void onTrustChecked(TrustFollowup followup);
    void updateTrustBar();
    // Syntax, autocomplete, agents, and command shortcuts. `pending` holds
    // every one of them: the pin is still being read off the UI thread.
    void applyExecutables(const Project &project, bool restart, bool pending);
    // The agents the panel may start: gated ones are left out while this
    // folder is not allowed to run them.
    QList<AgentSettings> agentsForPanel(const Project &project) const;
    void finishStartup(const QStringList &paths);
    CommandVariables commandVariables() const;
    CommandVariables variablesFor(QWidget *page) const;
    void runSyntax(CodeEditor *editor);
    void applySyntax(quintptr editorId, int revision, const QList<Diagnostic> &spans);
    // True when the editor's file has, or may still get, a language server.
    // An unsaved buffer, and a file the session has called unsupported or
    // missing, are not asked about.
    bool completionServes(const CodeEditor *editor) const;
    // Queues one request of `kind` for the editor: its path, caret, revision,
    // and, for every kind but resolve, its buffer.
    void sendCompletion(CodeEditor *editor, const QString &kind, int id, const QString &trigger = QString(),
                        const QString &data = QString());
    void askCompletion(CodeEditor *editor, const QString &trigger);
    void askSignature(CodeEditor *editor);
    void askDocumentation(CodeEditor *editor, const CompletionItem &item);
    void applyCompletion(int id, int revision, const QList<CompletionItem> &items, bool incomplete);
    void applyResolved(int id, const QString &data, const QString &detail, const QString &documentation);
    void applySignature(int id, int revision, const QList<SignatureHint> &signatures, int active);
    void applyFileState(const QString &path, const CompletionFileState &state);
    QString diagnosticKey(const CodeEditor *editor) const;
    // `restart` replaces a running session, so the script is read again.
    void startCompletion(bool restart);
    // Opens an editor's file in the session so its language server loads
    // before the first completion is asked for. The session answers with
    // that file's state.
    void warmCompletion(CodeEditor *editor);
    // The session's row on the Processes page, or null when it is not running.
    const ProcessRecord *completionRecord() const;
    // What the Autocomplete field says about the tab in front.
    QString completionStatus() const;
    void refreshCompletionStatus();
    void runCommand(const QString &name);
    // Processes page requests. A key this window does not start (a scan, a
    // read) can be cancelled and cannot be reloaded.
    void cancelProcess(const QString &key);
    void reloadProcess(const QString &key);
    // Autocomplete and every open editor's check, when the project enables
    // them. These are the processes that run without being chosen from a menu.
    void restartServices();
    static bool canReload(const QString &key);
    void connectSourceControl();
    // The explorer and Source Control share the left column, which hides when
    // neither is open.
    void updateSidebar();
    // The repository Git menu items and the branch button act on: the one
    // holding the file in front, otherwise the one picked in Source Control.
    QString gitRoot() const;
    void refreshGitStatus();
    // Reads the staged text of the editor's file, which its change bars
    // compare against. A file outside every repository has no bars.
    void refreshDiffBase(CodeEditor *editor);
    void openGitDiff(GitDiffRequest request);
    // The Output log that holds one repository's operations.
    static QString gitLogName(const QString &root);

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
    TrustBar *m_trustBar = nullptr;
    ProjectTrustGate *m_trust = nullptr;
    // The project whose pin is in flight or was just applied, and whether
    // that load should restart services once the pin is known.
    Project m_trustProject;
    bool m_trustRestart = false;
    BottomPanel *m_bottom = nullptr;
    OutputPanel *m_output = nullptr;
    bool m_syncingTerminal = false;
    bool m_syncingProcesses = false;
    CommandRunner *m_runner = nullptr;
    SyntaxRunner *m_syntax = nullptr;
    CompletionSession *m_completion = nullptr;
    AgentSession *m_agentSession = nullptr;
    AgentPanel *m_agentPanel = nullptr;
    AgentController *m_agents = nullptr;
    GitClient *m_gitClient = nullptr;
    SourceControl *m_sourceControl = nullptr;
    SourceControlPanel *m_sourcePanel = nullptr;
    QSplitter *m_sidebar = nullptr;
    QToolButton *m_branchButton = nullptr;
    QMenu *m_gitMenu = nullptr;
    // Output log name to its repository, for Stop.
    QHash<QString, QString> m_gitLogs;
    ProcessOrchestrator *m_orchestrator = nullptr;
    QThread *m_thread = nullptr;
    QSet<QString> m_runningCommands;
    QSet<QString> m_editorCommands;
    QHash<QString, QString> m_commandTexts;
    bool m_syntaxEnabled = false;
    QString m_syntaxCommand;
    QString m_syntaxDirectory;
    bool m_autocompleteEnabled = false;
    QString m_autocompleteCommand;
    QString m_autocompleteDirectory;
    // The newest request of each kind. A reply with an older id is dropped.
    int m_completionId = 0;
    int m_signatureId = 0;
    int m_resolveId = 0;
    QString m_resolveData;
    // What the session last said about each file, by editor path. Emptied
    // when the session starts again.
    QHash<QString, CompletionFileState> m_fileStates;
    QList<ProcessRecord> m_liveProcesses;
    QList<ProcessRecord> m_recentProcesses;
    QHash<quintptr, QString> m_diagnosticKeys;
    // The settings files in use, every file the services list under `watch`,
    // and the project root, where a settings file can appear or go. One save
    // fires several changes, so they are gathered by m_projectChanged, and
    // those of the folder by m_projectFolderChanged.
    QFileSystemWatcher *m_projectWatcher = nullptr;
    QTimer *m_projectChanged = nullptr;
    QTimer *m_projectFolderChanged = nullptr;
    // The project root. Empty when only application.crlf applies.
    QString m_projectDirectory;
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
    QLabel *m_completionLabel = nullptr;
    QLabel *m_languageLabel = nullptr;
    QLabel *m_encodingLabel = nullptr;
    QLabel *m_eolLabel = nullptr;
};
