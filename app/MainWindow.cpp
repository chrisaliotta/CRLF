#include "app/MainWindow.h"

#include "app/BottomPanel.h"
#include "app/SyntaxRunner.h"
#include "app/CompletionSession.h"
#include "app/CommandRunner.h"
#include "app/ProcessOrchestrator.h"
#include "app/ExplorerPanel.h"
#include "app/FindBar.h"
#include "app/GitClient.h"
#include "app/OpenWith.h"
#include "app/SourceControl.h"
#include "app/SourceControlPanel.h"
#include "app/OutputPanel.h"
#include "app/ProcessesPanel.h"
#include "app/ProblemsPanel.h"
#include "app/TabWorkspace.h"
#include "app/TemplateDialog.h"
#include "app/TerminalPanel.h"
#include "app/TitleBar.h"
#include "io/ProjectCommands.h"
#include "io/WslPath.h"
#include "editor/CodeEditor.h"
#include "editor/FoldController.h"
#include "hex/HexView.h"
#include "io/DocumentFile.h"
#include "syntax/LanguageDefinition.h"

#include <QActionGroup>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QUuid>
#include <QCoreApplication>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTextCursor>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
constexpr auto kPersistSetting = "persistUnsavedEdits";
constexpr auto kExplorerRootSetting = "explorerRoot";
constexpr auto kExplorerVisibleSetting = "explorerVisible";
constexpr auto kSourceControlVisibleSetting = "sourceControlVisible";
constexpr auto kTerminalVisibleSetting = "terminalVisible";
constexpr auto kThemeSetting = "theme";
constexpr auto kFileIconThemeSetting = "fileIconTheme";
constexpr auto kHexColumnsSetting = "hexBytesPerRow";
constexpr auto kLineNumbersSetting = "lineNumbers";
constexpr auto kSaveDirectorySetting = "saveDirectory";
constexpr int kSessionSnapshotIntervalMs = 30000;
constexpr int kResizeMargin = 6;

QString rgbaString(const QColor &color)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

// A command names ${buffer} or ${terminal}. The value is a path, not the
// text, so a newline in the buffer cannot break the shell command.
QString writePayload(const QString &prefix, const QString &text)
{
    const QString path = QDir(QDir::tempPath()).filePath(
        QStringLiteral("crlf-%1-%2.txt").arg(prefix, QUuid::createUuid().toString(QUuid::Id128)));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    file.write(text.toUtf8());
    return path;
}

QColor chromeBorder(const Theme &theme)
{
    const bool dark = theme.background.lightness() < 128;
    return dark ? theme.background.lighter(190) : theme.background.darker(122);
}

QColor chromeColor(const Theme &theme)
{
    const bool dark = theme.background.lightness() < 128;
    return dark ? theme.background.lighter(135) : theme.background.darker(106);
}

// The whole window chrome (title bar, tabs, menus, find bar, status bar)
// is styled from the active editor theme so it follows dark/light switches.
QString chromeStyleSheet(const Theme &theme)
{
    const bool dark = theme.background.lightness() < 128;
    const QColor chrome = chromeColor(theme);
    const QColor border = chromeBorder(theme);
    const QColor hover = dark ? QColor(255, 255, 255, 24) : QColor(0, 0, 0, 18);
    const QColor pressed = dark ? QColor(255, 255, 255, 38) : QColor(0, 0, 0, 32);
    const QColor scrollHandle = dark ? QColor(255, 255, 255, 50) : QColor(0, 0, 0, 60);

    return QStringLiteral(R"(
QMainWindow { background: %1; }
#TitleBar { background: %1; }
#TitleBar QLabel { color: %2; background: transparent; }
#TitleBar QToolButton { border: none; background: transparent; color: %9; outline: none; }
#TitleBar QToolButton:hover, #TitleBar QToolButton:focus { background: %4; color: %2; }
#TitleBar QToolButton:pressed { background: %5; color: %2; }
#closeButton:hover, #closeButton:focus { background: #e81123; color: #ffffff; }
#closeButton:pressed { background: #f1707a; color: #ffffff; }
QMenuBar { background: transparent; color: %2; }
QMenuBar::item { background: transparent; color: %9; padding: 4px 10px; border-radius: 6px; }
QMenuBar::item:hover { color: %2; }
QMenuBar::item:selected { background: %4; color: %2; }
QMenuBar::item:pressed, QMenuBar::item:open { background: %5; color: %2; }
QMenu { background: %1; color: %2; border: 1px solid %3; border-radius: 8px; padding: 4px; }
QMenu::item { padding: 5px 28px 5px 16px; border-radius: 5px; }
QMenu::item:selected { background: %6; }
QMenu::separator { height: 1px; background: %3; margin: 4px 8px; }
QTabWidget::pane { border: none; }
QTabBar { background: transparent; qproperty-drawBase: 0; }
QPlainTextEdit { border: none; }
QTabBar::tab { background: transparent; color: %2; padding: 5px 0;
               margin: 3px 2px 0 2px;
               border-top-left-radius: 7px; border-top-right-radius: 7px; }
QTabBar::tab:selected { background: %7; }
QTabBar::tab:hover:!selected { background: %4; }
#newTabLabel { color: %2; background: transparent; }
#tabCloseButton { border: none; border-radius: 4px; background: transparent; color: %2; padding: 0; }
#tabCloseButton:hover { background: %4; }
#tabCloseButton:pressed { background: %5; }
QStatusBar { background: %1; color: %2; }
QStatusBar QLabel { color: %2; padding: 0 6px; }
QStatusBar::item { border: none; }
FindBar { background: %1; }
FindBar QLabel { color: %2; }
FindBar QToolButton { border: none; border-radius: 5px; color: %2; padding: 3px; }
FindBar QToolButton:hover { background: %4; }
FindBar QListWidget { background: %7; color: %2; border: none; outline: 0; }
FindBar QListWidget::item { padding: 2px 8px; }
FindBar QListWidget::item:selected { background: %6; color: %2; }
OutputPanel, TerminalPanel, ProblemsPanel, ProcessesPanel, BottomPanel { background: %1; }
OutputPanel QLabel, TerminalPanel QLabel, ProblemsPanel QLabel, ProcessesPanel QLabel, BottomPanel QLabel { color: %2; }
OutputPanel QToolButton, TerminalPanel QToolButton, BottomPanel QToolButton, ProcessesPanel QToolButton { border: none; border-radius: 5px; color: %2; padding: 3px 8px; }
OutputPanel QToolButton:hover, TerminalPanel QToolButton:hover, BottomPanel QToolButton:hover, ProcessesPanel QToolButton:hover { background: %4; }
ProcessesPanel QToolButton:disabled { color: %3; }
#outputPage:checked, #problemsPage:checked, #terminalPage:checked, #processesPage:checked, #processSection:checked { background: %5; }
OutputPanel QPlainTextEdit { background: %7; color: %2; }
ProblemsPanel QTreeWidget { background: %1; color: %2; border: none; outline: 0; }
ProblemsPanel QTreeWidget::item { padding: 2px 4px; }
ProblemsPanel QTreeWidget::item:hover { background: %4; }
ProblemsPanel QTreeWidget::item:selected { background: %6; color: %2; }
ProblemsPanel QHeaderView::section { background: %1; color: %2; border: none; padding: 4px; }
ProcessesPanel QTableWidget { background: %1; color: %2; border: none; outline: 0; gridline-color: %3; }
ProcessesPanel QTableWidget::item { padding: 2px 8px; }
ProcessesPanel QTableWidget::item:selected { background: %6; color: %2; }
ProcessesPanel QHeaderView::section { background: %1; color: %2; padding: 4px 8px;
    border: none; border-right: 1px solid %3; border-bottom: 1px solid %3; }
ProcessesPanel QPlainTextEdit { background: %7; color: %2; border: none; }
OutputPanel QComboBox, TerminalPanel QComboBox { background: %7; color: %2; border: 1px solid %3; border-radius: 6px; padding: 2px 8px; }
OutputPanel QComboBox QAbstractItemView, TerminalPanel QComboBox QAbstractItemView { background: %1; color: %2; selection-background-color: %6; }
TerminalPanel QComboBox QLineEdit { background: transparent; border: none; padding: 0; }
ExplorerPanel { background: %1; }
#explorerTitle { color: %2; font-weight: bold; padding: 6px 12px; }
#explorerLoading { color: %2; }
ExplorerPanel QToolButton { border: none; border-radius: 4px; background: transparent; color: %2; }
ExplorerPanel QToolButton:hover { background: %4; }
ExplorerPanel QToolButton:pressed { background: %5; }
ExplorerPanel QToolButton:disabled { color: %3; }
ExplorerPanel QTreeView { background: %1; color: %2; border: none; outline: 0; padding-left: 12px; }
ExplorerPanel QTreeView::item { padding: 2px 0; }
ExplorerPanel QTreeView::item:hover { background: %4; }
ExplorerPanel QTreeView::item:selected { background: %6; color: %2; }
SourceControlPanel, DiffView { background: %1; }
#sourceControlTitle { color: %2; font-weight: bold; padding: 6px 12px; }
#sourceControlEmpty, #diffLabel { color: %2; padding: 0 6px; }
SourceControlPanel QToolButton, DiffView QToolButton { border: none; border-radius: 4px; background: transparent; color: %2; padding: 2px 6px; }
SourceControlPanel QToolButton:hover, DiffView QToolButton:hover { background: %4; }
SourceControlPanel QToolButton:pressed, SourceControlPanel QToolButton:checked, DiffView QToolButton:pressed { background: %5; }
SourceControlPanel QTreeWidget { background: %1; color: %2; border: none; outline: 0; }
SourceControlPanel QTreeWidget::item { padding: 2px 0; }
SourceControlPanel QTreeWidget::item:hover { background: %4; }
SourceControlPanel QTreeWidget::item:selected { background: %6; }
SourceControlPanel QPlainTextEdit { background: %7; color: %2; border: 1px solid %3; border-radius: 6px; padding: 2px; }
SourceControlPanel QComboBox { background: %7; color: %2; border: 1px solid %3; border-radius: 6px; padding: 2px 8px; }
SourceControlPanel QComboBox QAbstractItemView { background: %1; color: %2; selection-background-color: %6; }
#branchButton { border: none; border-radius: 4px; color: %2; padding: 0 6px; }
#branchButton:hover { background: %4; }
QSplitter::handle { background: %3; }
QLineEdit { background: %7; color: %2; border: 1px solid %3; border-radius: 6px;
            padding: 3px 8px; selection-background-color: %6; }
QLineEdit:focus { border-color: %6; }
QPushButton { background: transparent; color: %2; border: 1px solid %3;
              border-radius: 6px; padding: 3px 12px; }
QPushButton:hover { background: %4; }
QPushButton:pressed { background: %5; }
QScrollBar:vertical { background: transparent; width: 12px; }
QScrollBar:horizontal { background: transparent; height: 12px; }
QScrollBar::handle { background: %8; border-radius: 4px; margin: 2px; }
QScrollBar::handle:vertical { min-height: 24px; }
QScrollBar::handle:horizontal { min-width: 24px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
CodeEditor QScrollBar:vertical { background: %7; width: 14px; border: none; margin: 0; }
CodeEditor QScrollBar:horizontal { background: %7; height: 14px; border: none; margin: 0; }
CodeEditor QScrollBar::groove { background: %7; border: none; }
CodeEditor QScrollBar::add-page, CodeEditor QScrollBar::sub-page { background: %7; border: none; }
CodeEditor QScrollBar::handle:vertical { background: %8; min-height: 24px; border-radius: 2px; margin: 2px 5px; }
CodeEditor QScrollBar::handle:horizontal { background: %8; min-width: 24px; border-radius: 2px; margin: 5px 2px; }
CodeEditor QScrollBar[expanded="true"]::handle:vertical { margin: 1px; border-radius: 5px; }
CodeEditor QScrollBar[expanded="true"]::handle:horizontal { margin: 1px; border-radius: 5px; }
)")
        .arg(chrome.name(), theme.foreground.name(), border.name(), rgbaString(hover),
             rgbaString(pressed), theme.selection.name(), theme.background.name(),
             rgbaString(scrollHandle), theme.gutterForeground.name());
}

QList<Theme> loadThemes()
{
    QList<Theme> themes;
    const auto add = [&themes](const QString &path) {
        const Theme theme = Theme::fromJsonFile(path);
        for (int i = 0; i < themes.size(); ++i) {
            if (themes.at(i).name == theme.name) {
                themes[i] = theme;
                return;
            }
        }
        themes.append(theme);
    };
    const QDir dir(QDir(programDirectory()).filePath(QStringLiteral("themes")));
    for (const QString &file : dir.entryList({QStringLiteral("*.json")}, QDir::Files))
        add(dir.filePath(file));
    return themes;
}

const Theme *themeNamed(const QList<Theme> &themes, const QString &name)
{
    for (const Theme &theme : themes) {
        if (theme.name == name)
            return &theme;
    }
    return &themes.first();
}

const FileIconTheme *iconThemeNamed(const QList<FileIconTheme> &themes, const QString &name)
{
    for (const FileIconTheme &theme : themes) {
        if (theme.name == name)
            return &theme;
    }
    return themes.isEmpty() ? nullptr : &themes.first();
}

constexpr auto kProjectItem = "projectItem";

QString menuTitle(const QAction *action)
{
    QString title = action->text();
    title.remove(QLatin1Char('&'));
    return title;
}

QMenu *existingMenu(const QList<QAction *> &actions, const QString &title)
{
    for (QAction *action : actions) {
        if (action->menu() && menuTitle(action).compare(title, Qt::CaseInsensitive) == 0)
            return action->menu();
    }
    return nullptr;
}

// The first path entry is a menu-bar menu. Later entries nest under it.
// Menus this creates are marked so the next reload can remove them without
// touching File, Edit, View, Language, or Run.
QMenu *menuAt(QMenuBar *bar, QMenu *run, const QStringList &path)
{
    if (path.isEmpty())
        return run;
    QMenu *menu = existingMenu(bar->actions(), path.first());
    if (!menu) {
        menu = bar->addMenu(path.first());
        menu->menuAction()->setProperty(kProjectItem, true);
    }
    for (int i = 1; i < path.size(); ++i) {
        QMenu *child = existingMenu(menu->actions(), path.at(i));
        if (!child) {
            child = menu->addMenu(path.at(i));
            child->menuAction()->setProperty(kProjectItem, true);
        }
        menu = child;
    }
    return menu;
}

QAction *addProjectAction(QMenu *menu, const QString &text)
{
    QAction *action = menu->addAction(text);
    action->setProperty(kProjectItem, true);
    return action;
}

void removeProjectItems(QMenu *menu)
{
    const QList<QAction *> actions = menu->actions();
    for (QAction *action : actions) {
        if (!action->property(kProjectItem).toBool()) {
            if (QMenu *child = action->menu())
                removeProjectItems(child);
            continue;
        }
        if (QMenu *child = action->menu())
            delete child;
        else
            delete action;
    }
}

void clearProjectCommands(QMenuBar *bar)
{
    const QList<QAction *> roots = bar->actions();
    for (QAction *action : roots) {
        QMenu *menu = action->menu();
        if (!menu)
            continue;
        if (action->property(kProjectItem).toBool())
            delete menu;
        else
            removeProjectItems(menu);
    }
}

// Only project commands are checkable for "running". Built-in items such as
// Explorer and the theme entries are checkable too, and setChecked would
// fire their toggled handlers.
void markRunningCommands(QMenu *menu, const QSet<QString> &running)
{
    for (QAction *action : menu->actions()) {
        if (QMenu *child = action->menu())
            markRunningCommands(child, running);
        else if (action->property(kProjectItem).toBool())
            action->setChecked(running.contains(action->text()));
    }
}

void addJsonFileMenu(QMenu *menu, const QString &directory, TabWorkspace *tabs)
{
    const QDir dir(directory);
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &file : files) {
        const QString path = dir.absoluteFilePath(file);
        menu->addAction(file, tabs, [tabs, path] { tabs->openFile(path); });
    }
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_themes(loadThemes())
    , m_iconThemes(FileIconTheme::loadInstalled())
    , m_actions(this)
{
    const Theme *startupTheme =
        themeNamed(m_themes, QSettings().value(kThemeSetting, QStringLiteral("Dark")).toString());

    m_tabs = new TabWorkspace(&m_registry, startupTheme, this);
    m_findBar = new FindBar(this);
    m_explorer = new ExplorerPanel(this);
    m_bottom = new BottomPanel(this);
    m_output = m_bottom->output();
    m_bottom->setDirectoryProvider([this] {
        QString directory = m_explorer->rootPath();
        if (directory.isEmpty() && m_tabs->documentCount() > 0) {
            const QString path = m_tabs->fileFor(m_tabs->currentWidget()).path();
            if (!path.isEmpty())
                directory = QFileInfo(path).absolutePath();
        }
        return directory;
    });
    m_thread = new QThread(this);
    m_orchestrator = new ProcessOrchestrator;
    m_syntax = new SyntaxRunner;
    m_completion = new CompletionSession;
    m_runner = new CommandRunner;
    m_gitClient = new GitClient;
    m_orchestrator->moveToThread(m_thread);
    m_syntax->moveToThread(m_thread);
    m_completion->moveToThread(m_thread);
    m_runner->moveToThread(m_thread);
    m_gitClient->moveToThread(m_thread);
    m_syntax->setOrchestrator(m_orchestrator);
    m_completion->setOrchestrator(m_orchestrator);
    m_runner->setOrchestrator(m_orchestrator);
    m_gitClient->setOrchestrator(m_orchestrator);
    m_sourceControl = new SourceControl(m_orchestrator, m_gitClient, this);
    m_sourcePanel = new SourceControlPanel(m_sourceControl, this);
    m_runner->setUi(this);
    m_runner->setContextProvider([this](const QString &command) {
        CommandVariables vars = commandVariables();
        if (command.contains(QStringLiteral("${buffer}"))) {
            CodeEditor *editor = m_tabs->currentEditor();
            if (editor != nullptr) {
                QString text = editor->toPlainText();
                text.remove(QLatin1Char('\r'));
                vars.buffer = writePayload(QStringLiteral("buffer"), text);
            }
        }
        if (command.contains(QStringLiteral("${terminal}")) && m_bottom->terminal()->sessionCount() > 0)
            vars.terminal = writePayload(QStringLiteral("terminal"), m_bottom->terminal()->transcript());
        return vars;
    });
    m_runner->setEditorInputProvider([this](const QString &name) {
        m_tabs->rememberCommandEditor(name);
        EditorFeed feed;
        CodeEditor *editor = m_tabs->currentEditor();
        if (!editor)
            return feed;
        feed.active = true;
        feed.text = editor->toPlainText();
        return feed;
    });
    m_tabs->setOrchestrator(m_orchestrator);
    m_explorer->setOrchestrator(m_orchestrator);
    m_thread->start();
    connect(m_orchestrator, &ProcessOrchestrator::recordsChanged, this,
            [this](const QList<ProcessRecord> &live, const QList<ProcessRecord> &recent) {
                m_liveProcesses = live;
                for (ProcessRecord &record : m_liveProcesses)
                    record.reloadable = canReload(record.key);
                m_recentProcesses = recent;
                if (ProcessesPanel *panel = m_tabs->processes())
                    panel->setRecords(m_liveProcesses, m_recentProcesses);
                refreshCompletionStatus();
            },
            Qt::QueuedConnection);
    connect(m_tabs, &TabWorkspace::processCancelRequested, this, &MainWindow::cancelProcess);
    connect(m_tabs, &TabWorkspace::processReloadRequested, this, &MainWindow::reloadProcess);
    connect(m_tabs, &TabWorkspace::processServicesRestartRequested, this, &MainWindow::restartServices);
    connect(m_tabs, &TabWorkspace::syntaxRequested, this, &MainWindow::runSyntax);
    connect(m_tabs, &TabWorkspace::completionRequested, this, &MainWindow::askCompletion);
    connect(m_tabs, &TabWorkspace::signatureRequested, this, &MainWindow::askSignature);
    connect(m_tabs, &TabWorkspace::documentationRequested, this, &MainWindow::askDocumentation);
    connect(m_tabs, &TabWorkspace::editorClosed, this, [this](quintptr editorId) {
        QMetaObject::invokeMethod(m_syntax, [this, editorId] { m_syntax->cancel(editorId); },
                                  Qt::QueuedConnection);
        const QString key = m_diagnosticKeys.take(editorId);
        if (!key.isEmpty())
            m_bottom->problems()->removeFile(key);
    });
    connect(m_tabs, &TabWorkspace::editorRetargeted, this, [this](CodeEditor *editor) {
        warmCompletion(editor);
        refreshCompletionStatus();
        const quintptr editorId = quintptr(editor);
        const QString previous = m_diagnosticKeys.value(editorId);
        if (previous.isEmpty())
            return;
        const QString key = diagnosticKey(editor);
        if (key == previous)
            return;
        m_bottom->problems()->retarget(previous, key, editor->filePath(), m_tabs->titleFor(editor), editorId);
        m_diagnosticKeys.insert(editorId, key);
    });
    connect(m_syntax, &SyntaxRunner::completed, this, &MainWindow::applySyntax, Qt::QueuedConnection);
    connect(m_syntax, &SyntaxRunner::failed, this, [this](quintptr, const QString &message) {
        statusBar()->showMessage(message, 5000);
    }, Qt::QueuedConnection);
    connect(m_completion, &CompletionSession::completed, this, &MainWindow::applyCompletion,
            Qt::QueuedConnection);
    connect(m_completion, &CompletionSession::resolved, this, &MainWindow::applyResolved,
            Qt::QueuedConnection);
    connect(m_completion, &CompletionSession::signatureReady, this, &MainWindow::applySignature,
            Qt::QueuedConnection);
    connect(m_completion, &CompletionSession::fileStateChanged, this, &MainWindow::applyFileState,
            Qt::QueuedConnection);
    connect(m_completion, &CompletionSession::failed, this, [this](const QString &message) {
        statusBar()->showMessage(message, 5000);
        if (CodeEditor *editor = m_tabs->currentEditor())
            editor->showCompletions({});
    }, Qt::QueuedConnection);
    connect(m_bottom->problems(), &ProblemsPanel::diagnosticActivated, this,
            [this](const QString &path, quintptr editorId, int line, int column) {
                m_tabs->goToDiagnostic(path, editorId, line, column);
            });
    m_projectWatcher = new QFileSystemWatcher(this);
    m_projectChanged = new QTimer(this);
    m_projectChanged->setSingleShot(true);
    m_projectChanged->setInterval(200);
    m_explorer->hide();
    m_sourcePanel->hide();

    m_sidebar = new QSplitter(Qt::Vertical, this);
    m_sidebar->setHandleWidth(1);
    m_sidebar->setChildrenCollapsible(false);
    m_sidebar->addWidget(m_explorer);
    m_sidebar->addWidget(m_sourcePanel);
    m_sidebar->hide();

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(m_sidebar);
    splitter->addWidget(m_tabs);
    splitter->setStretchFactor(1, 1);

    auto *vertical = new QSplitter(Qt::Vertical, this);
    vertical->setHandleWidth(1);
    vertical->setChildrenCollapsible(false);
    vertical->addWidget(splitter);
    vertical->addWidget(m_bottom);
    vertical->setStretchFactor(0, 1);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_findBar);
    layout->addWidget(vertical, 1);
    setCentralWidget(central);

    connect(m_bottom, &BottomPanel::opened, vertical, [vertical, this] {
        QTimer::singleShot(0, vertical, [vertical, this] {
            if (!m_bottom->isVisible() || vertical->property("bottomSized").toBool())
                return;
            int total = vertical->height();
            if (total < 120)
                total = qMax(400, height() - 120);
            vertical->setProperty("bottomSized", true);
            vertical->setSizes({total - 220, 220});
        });
    });

    m_cursorLabel = new QLabel(this);
    m_completionLabel = new QLabel(this);
    m_completionLabel->setToolTip(QStringLiteral(
        "Autocomplete for the file in front. When it is stopped, Restart Services in View > Processes starts it again."));
    m_completionLabel->hide();
    m_languageLabel = new QLabel(this);
    m_encodingLabel = new QLabel(this);
    m_eolLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_completionLabel);
    statusBar()->addPermanentWidget(m_languageLabel);
    statusBar()->addPermanentWidget(m_encodingLabel);
    statusBar()->addPermanentWidget(m_eolLabel);
    m_branchButton = new QToolButton(this);
    m_branchButton->setObjectName(QStringLiteral("branchButton"));
    m_branchButton->setAutoRaise(true);
    m_branchButton->setFocusPolicy(Qt::NoFocus);
    m_branchButton->setToolTip(QStringLiteral("Branches"));
    m_branchButton->hide();
    statusBar()->addWidget(m_branchButton);
    statusBar()->addWidget(m_cursorLabel);

    buildMenus();
    m_titleBar = new TitleBar(m_menuBar, this);
    setMenuWidget(m_titleBar);
    connect(this, &QWidget::windowTitleChanged, m_titleBar, &TitleBar::setTitle);

    connectActions();

    const QSettings settings;
    m_actions.persistEdits->setChecked(settings.value(kPersistSetting, true).toBool());
    m_tabs->setBytesPerRow(settings.value(kHexColumnsSetting, 16).toInt());
    m_actions.lineNumbers->setChecked(settings.value(kLineNumbersSetting, true).toBool());
    m_tabs->setLineNumbersVisible(m_actions.lineNumbers->isChecked());
    m_scopedRoot = settings.value(kExplorerRootSetting).toString();
    m_saveDirectory = settings.value(kSaveDirectorySetting).toString();
    if (m_saveDirectory.isEmpty())
        m_saveDirectory = m_scopedRoot;
    m_tabs->setSaveDirectory(m_saveDirectory);

    // The saved folder may have been deleted or its drive unplugged since.
    // The listing itself reports that; checking it here would stat a WSL
    // share on the UI thread before the window is shown.
    if (!m_scopedRoot.isEmpty()) {
        m_explorer->setRootPath(m_scopedRoot, false);
        m_actions.explorer->setChecked(settings.value(kExplorerVisibleSetting).toBool());
    }
    m_actions.sourceControl->setChecked(settings.value(kSourceControlVisibleSetting, false).toBool());

    // Periodic snapshot so unsaved edits also survive crashes and power loss,
    // not just graceful exits.
    auto *sessionTimer = new QTimer(this);
    sessionTimer->setInterval(kSessionSnapshotIntervalMs);
    connect(sessionTimer, &QTimer::timeout, this, &MainWindow::persistSession);
    sessionTimer->start();

    resize(1100, 750);
    applyTheme(startupTheme);
    if (settings.value(kTerminalVisibleSetting, false).toBool())
        m_actions.terminal->setChecked(true);
    applyFileIconTheme(iconThemeNamed(
        m_iconThemes, QSettings().value(kFileIconThemeSetting, QStringLiteral("Seti")).toString()));
    applyCustomFrame();
    refreshStatus();

    QStringList extensions;
    for (const LanguageDefinition &definition : m_registry.definitions())
        extensions.append(definition.extensions);
    QTimer::singleShot(0, this, [this, extensions] {
        m_orchestrator->submit(QStringLiteral("associations"), QStringLiteral("File associations"), QString(), 0,
                               QStringLiteral("registry"), [extensions] { registerOpenWith(extensions); });
    });
}

MainWindow::~MainWindow()
{
    // The tabs go first, while the orchestrator is still running. Each editor
    // cancels its scans on it as it is destroyed, and the symbol index and
    // the external lookup stop their walks, so the wait below is for work
    // that is already ending. Left to ~QWidget, the tabs would be destroyed
    // after the orchestrator and call into freed memory.
    m_findBar->setEditor(nullptr);
    m_tabs->disconnect(this);
    delete m_tabs;
    m_tabs = nullptr;

    QThread *uiThread = QThread::currentThread();
    QMetaObject::invokeMethod(m_orchestrator, [this, uiThread] {
        m_runner->stopAll();
        m_syntax->cancelAll();
        m_completion->stop();
        m_gitClient->cancelAll();
        m_orchestrator->shutdown();
        delete m_syntax;
        m_syntax = nullptr;
        delete m_completion;
        m_completion = nullptr;
        delete m_runner;
        m_runner = nullptr;
        delete m_gitClient;
        m_gitClient = nullptr;
        m_orchestrator->moveToThread(uiThread);
    }, Qt::BlockingQueuedConnection);
    m_thread->quit();
    m_thread->wait();
    delete m_orchestrator;
    m_orchestrator = nullptr;
}

// Forces Windows to resend WM_NCCALCSIZE now that this window handles it,
// which removes the native title bar from the very first frame.
void MainWindow::applyCustomFrame()
{
#ifdef Q_OS_WIN
    SetWindowPos(reinterpret_cast<HWND>(winId()), nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
#endif
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange)
        m_titleBar->setMaximized(isMaximized());
    // Work done in a terminal or another program shows up on return.
    if (event->type() == QEvent::ActivationChange && isActiveWindow())
        m_sourceControl->refresh();
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    MSG *msg = static_cast<MSG *>(message);

    // Claiming the entire window as client area removes the native caption
    // while WS_THICKFRAME keeps native shadows, snapping, animations, and
    // Windows 11 rounded corners. When maximized, Windows extends the window
    // past the monitor by the frame width, so inset the client to compensate.
    if (msg->message == WM_NCCALCSIZE && msg->wParam) {
        auto *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam);
        if (IsZoomed(msg->hwnd)) {
            const int frame = GetSystemMetrics(SM_CXSIZEFRAME)
                              + GetSystemMetrics(SM_CXPADDEDBORDER);
            params->rgrc[0].left += frame;
            params->rgrc[0].top += frame;
            params->rgrc[0].right -= frame;
            params->rgrc[0].bottom -= frame;
        }
        *result = 0;
        return true;
    }

    // With the caption gone, tell Windows which regions still act as resize
    // borders and which part of the custom title bar drags the window.
    if (msg->message == WM_NCHITTEST) {
        const QPoint pos = mapFromGlobal(QCursor::pos());
        if (!rect().contains(pos))
            return false;
        *result = HTCLIENT;
        if (!isMaximized()) {
            const bool left = pos.x() < kResizeMargin;
            const bool right = pos.x() >= width() - kResizeMargin;
            const bool top = pos.y() < kResizeMargin;
            const bool bottom = pos.y() >= height() - kResizeMargin;
            if (top && left) *result = HTTOPLEFT;
            else if (top && right) *result = HTTOPRIGHT;
            else if (bottom && left) *result = HTBOTTOMLEFT;
            else if (bottom && right) *result = HTBOTTOMRIGHT;
            else if (left) *result = HTLEFT;
            else if (right) *result = HTRIGHT;
            else if (top) *result = HTTOP;
            else if (bottom) *result = HTBOTTOM;
        }
        if (*result == HTCLIENT && m_titleBar->geometry().contains(pos)
            && m_titleBar->isDraggableAt(m_titleBar->mapFrom(this, pos))) {
            *result = HTCAPTION;
        }
        return true;
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::startSession(const QStringList &paths)
{
    if (!m_actions.persistEdits->isChecked()) {
        finishStartup(paths);
        return;
    }
    m_orchestrator->submit(QStringLiteral("session-load"), QStringLiteral("Session"), QString(), 0,
                           QStringLiteral("session"), [this, paths] {
        const Session session = m_sessionStore.load();
        QMetaObject::invokeMethod(this, [this, session, paths] {
            if (session.tabs.isEmpty()) {
                finishStartup(paths);
                return;
            }
            connect(m_tabs, &TabWorkspace::sessionRestored, this, [this, paths] {
                finishStartup(paths);
            }, Qt::SingleShotConnection);
            m_tabs->restoreSession(session);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::finishStartup(const QStringList &paths)
{
    for (const QString &path : paths)
        m_tabs->openFile(path);
    if (paths.isEmpty() && m_tabs->documentCount() == 0)
        m_tabs->newDocument();
}

void MainWindow::persistSession()
{
    if (m_actions.persistEdits->isChecked())
        m_sessionStore.save(m_tabs->captureSession());
}

void MainWindow::openForwardedPaths(const QStringList &paths)
{
    for (const QString &path : paths)
        m_tabs->openFile(path);
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();
}

void MainWindow::buildMenus()
{
    m_menuBar = new QMenuBar(this);

    QMenu *file = m_menuBar->addMenu(QStringLiteral("&File"));
    file->addAction(m_actions.newFile);
    file->addAction(m_actions.open);
    file->addAction(m_actions.openFolder);
    file->addAction(m_actions.openWslFolder);
    file->addSeparator();
    file->addAction(m_actions.save);
    file->addAction(m_actions.saveAs);
    file->addSeparator();
    file->addAction(m_actions.persistEdits);
    file->addSeparator();
    file->addAction(m_actions.closeTab);
    file->addAction(m_actions.quit);

    QMenu *edit = m_menuBar->addMenu(QStringLiteral("&Edit"));
    edit->addAction(m_actions.undo);
    edit->addAction(m_actions.redo);
    edit->addSeparator();
    edit->addAction(m_actions.cut);
    edit->addAction(m_actions.copy);
    edit->addAction(m_actions.paste);
    edit->addSeparator();
    edit->addAction(m_actions.find);
    edit->addAction(m_actions.gotoLine);

    QMenu *view = m_menuBar->addMenu(QStringLiteral("&View"));
    view->addAction(m_actions.explorer);
    view->addAction(m_actions.sourceControl);
    view->addAction(m_actions.terminal);
    view->addAction(m_actions.processes);
    view->addSeparator();
    view->addAction(m_actions.wordWrap);
    view->addAction(m_actions.lineNumbers);
    view->addAction(m_actions.hexMode);
    view->addAction(m_actions.byteColumns);
    view->addSeparator();
    view->addAction(m_actions.zoomIn);
    view->addAction(m_actions.zoomOut);
    view->addAction(m_actions.zoomReset);
    view->addSeparator();
    QMenu *themeMenu = view->addMenu(QStringLiteral("&Theme"));
    m_themeGroup = new QActionGroup(this);
    for (const Theme &theme : m_themes) {
        QAction *action = themeMenu->addAction(theme.name);
        action->setCheckable(true);
        m_themeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, theme = &theme] { applyTheme(theme); });
    }
    QMenu *iconMenu = view->addMenu(QStringLiteral("&File Icons"));
    iconMenu->addAction(QStringLiteral("Templates…"), this, [this] { editIconTemplates(); });
    iconMenu->addSeparator();
    m_iconThemeGroup = new QActionGroup(this);
    for (const FileIconTheme &theme : m_iconThemes) {
        QAction *action = iconMenu->addAction(theme.name);
        action->setCheckable(true);
        m_iconThemeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, theme = &theme] { applyFileIconTheme(theme); });
    }
    QMenu *defaults = view->addMenu(QStringLiteral("Edit Defaults"));
    const QString commandsFile = QDir(programDirectory()).filePath(QStringLiteral(".crlf"));
    defaults->addAction(QStringLiteral("Commands"), m_tabs, [this, commandsFile] {
        m_tabs->openFile(commandsFile);
    });
    addJsonFileMenu(defaults->addMenu(QStringLiteral("Themes")),
                    QDir(programDirectory()).filePath(QStringLiteral("themes")), m_tabs);
    addJsonFileMenu(defaults->addMenu(QStringLiteral("Syntax")),
                    QDir(programDirectory()).filePath(QStringLiteral("styles")), m_tabs);
    view->addSeparator();
    view->addAction(m_actions.foldAll);
    view->addAction(m_actions.unfoldAll);

    // Manual syntax choice for the active tab, so an unsaved buffer can be
    // styled (e.g. as Markdown) before it has a file extension. The checked
    // entry tracks the active tab via refreshStatus.
    QMenu *language = m_menuBar->addMenu(QStringLiteral("&Language"));
    m_languageGroup = new QActionGroup(this);
    for (const LanguageDefinition &definition : m_registry.definitions()) {
        QAction *action = language->addAction(definition.name);
        action->setCheckable(true);
        m_languageGroup->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, def = &definition] { m_tabs->setCurrentLanguage(def); });
    }

    m_gitMenu = m_menuBar->addMenu(QStringLiteral("&Git"));
    m_gitMenu->addAction(m_actions.gitRefresh);
    m_gitMenu->addAction(m_actions.gitFetchAll);
    m_gitMenu->addSeparator();
    m_gitMenu->addAction(m_actions.gitPull);
    m_gitMenu->addAction(m_actions.gitPush);
    m_gitMenu->addAction(m_actions.gitCommit);
    m_gitMenu->addAction(m_actions.gitBranches);
    m_gitMenu->addSeparator();
    m_gitMenu->addAction(m_actions.gitStash);
    m_gitMenu->addAction(m_actions.gitPopStash);
    m_gitMenu->addSeparator();
    m_gitMenu->addAction(m_actions.gitOpenChanges);
    m_gitMenu->addAction(m_actions.gitFileHistory);
    m_gitMenu->menuAction()->setVisible(false);

    m_runMenu = m_menuBar->addMenu(QStringLiteral("&Run"));
    m_runMenu->menuAction()->setVisible(false);

    QMenu *help = m_menuBar->addMenu(QStringLiteral("&Help"));
    help->addAction(QStringLiteral("&About"), this, [this] {
        const QString name = QCoreApplication::applicationName();
        const QString path = QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toHtmlEscaped();
        QMessageBox::about(this, QStringLiteral("About %1").arg(name),
                           QStringLiteral("<p style='text-align:center'><b>%1</b><br>Version %2</p>"
                                          "<p style='text-align:center'>%3</p>"
                                          "<p style='text-align:center'>Dedicated to my wife and children.</p>")
                               .arg(name, QCoreApplication::applicationVersion(), path));
    });
}

void MainWindow::connectActions()
{
    connect(m_actions.newFile, &QAction::triggered, m_tabs, &TabWorkspace::newDocument);
    connect(m_actions.open, &QAction::triggered, this, &MainWindow::openFileDialog);
    connect(m_actions.openFolder, &QAction::triggered, this, [this] {
        if (chooseExplorerFolder())
            m_actions.explorer->setChecked(true);
    });
    connect(m_actions.openWslFolder, &QAction::triggered, this, &MainWindow::chooseWslFolder);
    connect(m_actions.explorer, &QAction::toggled, this, [this](bool visible) {
        if (visible && m_explorer->rootPath().isEmpty() && !chooseExplorerFolder()) {
            m_actions.explorer->setChecked(false);
            return;
        }
        m_explorer->setVisible(visible);
        QSettings().setValue(kExplorerVisibleSetting, visible);
        updateSidebar();
    });
    connect(m_actions.sourceControl, &QAction::toggled, this, [this](bool visible) {
        m_sourcePanel->setVisible(visible);
        QSettings().setValue(kSourceControlVisibleSetting, visible);
        updateSidebar();
    });
    connect(m_actions.terminal, &QAction::toggled, this, [this](bool visible) {
        if (m_syncingTerminal)
            return;
        QSettings().setValue(kTerminalVisibleSetting, visible);
        m_syncingTerminal = true;
        if (visible)
            m_bottom->showTerminal();
        else
            m_bottom->hideTerminal();
        m_syncingTerminal = false;
    });
    connect(m_actions.processes, &QAction::toggled, this, [this](bool visible) {
        if (m_syncingProcesses)
            return;
        m_syncingProcesses = true;
        if (visible) {
            m_tabs->showProcesses();
            if (ProcessesPanel *panel = m_tabs->processes())
                panel->setRecords(m_liveProcesses, m_recentProcesses);
        } else {
            m_tabs->closeProcesses();
        }
        m_syncingProcesses = false;
    });
    connect(m_tabs, &TabWorkspace::processesCurrent, this, [this](bool visible) {
        if (m_syncingProcesses)
            return;
        m_syncingProcesses = true;
        m_actions.processes->setChecked(visible);
        m_syncingProcesses = false;
    });
    connect(m_bottom, &BottomPanel::terminalActive, this, [this](bool visible) {
        if (m_syncingTerminal)
            return;
        m_syncingTerminal = true;
        m_actions.terminal->setChecked(visible);
        QSettings().setValue(kTerminalVisibleSetting, visible);
        m_syncingTerminal = false;
    });
    connect(m_explorer, &ExplorerPanel::rootPathChanged, this, [this](const QString &path) {
        QSettings().setValue(kExplorerRootSetting, path);
        if (path != m_scopedRoot) {
            m_scopedRoot = path;
            if (!path.isEmpty()) {
                m_saveDirectory = path;
                QSettings().setValue(kSaveDirectorySetting, path);
                m_tabs->setSaveDirectory(path);
            }
        }
        reloadCommands();
    });
    connect(m_explorer, &ExplorerPanel::fileActivated, m_tabs, &TabWorkspace::openFile);
    connect(m_tabs, &TabWorkspace::fileRevealed, m_explorer, &ExplorerPanel::reveal);
    connect(m_explorer, &ExplorerPanel::pathMoved, m_tabs, &TabWorkspace::retargetPath);
    connect(m_explorer, &ExplorerPanel::closeRequested, this,
            [this] { m_actions.explorer->setChecked(false); });
    connect(m_explorer, &ExplorerPanel::virtualFoldersEdited, this, [this](const QList<FileGroup> &groups) {
        QString file = m_projectFile;
        if (file.isEmpty())
            file = QDir(m_explorer->rootPath()).filePath(QStringLiteral(".crlf"));
        if (!saveProjectGroups(file, groups)) {
            statusBar()->showMessage(QStringLiteral("Couldn't write .crlf"), 5000);
            return;
        }
        reloadCommands();
    });
    connect(m_tabs, &TabWorkspace::fileSaved, this, [this](const QString &directory) {
        m_saveDirectory = directory;
        QSettings().setValue(kSaveDirectorySetting, directory);
        reloadCommands();
    });
    connect(m_actions.save, &QAction::triggered, m_tabs, &TabWorkspace::saveCurrent);
    connect(m_actions.saveAs, &QAction::triggered, this, [this] { m_tabs->saveCurrentAs(m_saveDirectory); });
    connect(m_actions.persistEdits, &QAction::toggled, this, [this](bool enabled) {
        QSettings().setValue(kPersistSetting, enabled);
        if (enabled)
            persistSession();
        else
            m_sessionStore.clear();
    });
    connect(m_actions.closeTab, &QAction::triggered, this, &MainWindow::closeCurrentTab);
    connect(m_actions.quit, &QAction::triggered, this, &QWidget::close);

    connect(m_actions.undo, &QAction::triggered, this, [this] { m_tabs->currentEditor()->undo(); });
    connect(m_actions.redo, &QAction::triggered, this, [this] { m_tabs->currentEditor()->redo(); });
    connect(m_actions.cut, &QAction::triggered, this, [this] { m_tabs->currentEditor()->cut(); });
    connect(m_actions.copy, &QAction::triggered, this, [this] { m_tabs->currentEditor()->copy(); });
    connect(m_actions.paste, &QAction::triggered, this, [this] { m_tabs->currentEditor()->paste(); });
    connect(m_actions.find, &QAction::triggered, this, [this] { m_findBar->open(); });
    connect(m_actions.gotoLine, &QAction::triggered, this, &MainWindow::gotoLine);

    connect(m_actions.wordWrap, &QAction::toggled, m_tabs, &TabWorkspace::setWordWrap);
    connect(m_actions.lineNumbers, &QAction::toggled, this, [this](bool visible) {
        m_tabs->setLineNumbersVisible(visible);
        QSettings().setValue(kLineNumbersSetting, visible);
    });
    // triggered (not toggled): refreshStatus syncs the checked state to the
    // active tab and must not re-enter the mode switch.
    connect(m_actions.hexMode, &QAction::triggered, m_tabs, &TabWorkspace::setCurrentHexMode);
    connect(m_actions.byteColumns, &QAction::triggered, this, [this] {
        bool accepted = false;
        const int columns = QInputDialog::getInt(this, QStringLiteral("Byte Columns"),
                                                 QStringLiteral("Columns:"), m_tabs->bytesPerRow(),
                                                 1, 64, 1, &accepted);
        if (!accepted)
            return;
        QSettings().setValue(kHexColumnsSetting, columns);
        m_tabs->setBytesPerRow(columns);
    });
    connect(m_actions.zoomIn, &QAction::triggered, this, [this] { m_tabs->currentEditor()->zoom(1); });
    connect(m_actions.zoomOut, &QAction::triggered, this, [this] { m_tabs->currentEditor()->zoom(-1); });
    connect(m_actions.zoomReset, &QAction::triggered, this, [this] { m_tabs->currentEditor()->resetZoom(); });
    connect(m_actions.foldAll, &QAction::triggered, this,
            [this] { m_tabs->currentEditor()->foldController()->foldAll(); });
    connect(m_actions.unfoldAll, &QAction::triggered, this,
            [this] { m_tabs->currentEditor()->foldController()->unfoldAll(); });

    connect(m_tabs, &TabWorkspace::statusChanged, this, &MainWindow::refreshStatus);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        m_findBar->setEditor(m_tabs->currentEditor());
        reloadCommands();
        // A reply still on its way belongs to the tab that was left.
        ++m_completionId;
        ++m_signatureId;
        if (CodeEditor *editor = m_tabs->currentEditor()) {
            // A tab opened since the session started has no state yet. One
            // reopened on a file the session knows takes that file's triggers.
            const auto state = m_fileStates.constFind(editor->filePath());
            if (state != m_fileStates.constEnd())
                editor->setCompletionTriggers(state->triggers);
            else if (completionRecord() != nullptr)
                warmCompletion(editor);
        }
        refreshCompletionStatus();
    });
    connect(m_projectWatcher, &QFileSystemWatcher::fileChanged, m_projectChanged, qOverload<>(&QTimer::start));
    connect(m_projectChanged, &QTimer::timeout, this, [this] { reloadCommands(true); });
    connect(m_runner, &CommandRunner::outputReceived, this,
            [this](const QString &name, const QString &text) {
                if (m_editorCommands.contains(name) && m_output->currentName() != name)
                    m_output->showCommand(name);
                m_output->appendOutput(name, text);
            },
            Qt::QueuedConnection);
    connect(m_runner, &CommandRunner::editorOutput, m_tabs, &TabWorkspace::applyCommandOutput,
            Qt::QueuedConnection);
    connect(m_runner, &CommandRunner::stateChanged, this,
            [this](const QString &name, bool running, int exitCode) {
                if (running)
                    m_runningCommands.insert(name);
                else
                    m_runningCommands.remove(name);
                m_output->setState(name, running, exitCode);
                if (!running && exitCode > 0 && m_editorCommands.contains(name))
                    m_output->showCommand(name);
                for (QAction *action : m_menuBar->actions()) {
                    if (QMenu *menu = action->menu())
                        markRunningCommands(menu, m_runningCommands);
                }
            },
            Qt::QueuedConnection);
    connect(m_output, &OutputPanel::stopRequested, this, [this](const QString &name) {
        QMetaObject::invokeMethod(m_runner, [this, name] { m_runner->stop(name); }, Qt::QueuedConnection);
    });
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (m_tabs->closeTab(index) && m_tabs->documentCount() == 0)
            m_tabs->newDocument();
    });
    connectSourceControl();
}

void MainWindow::connectSourceControl()
{
    connect(m_sourcePanel, &SourceControlPanel::closeRequested, this,
            [this] { m_actions.sourceControl->setChecked(false); });
    connect(m_sourcePanel, &SourceControlPanel::fileActivated, m_tabs, &TabWorkspace::openFile);
    connect(m_sourcePanel, &SourceControlPanel::diffRequested, this, &MainWindow::openGitDiff);
    connect(m_branchButton, &QToolButton::clicked, this, [this] {
        m_sourcePanel->showBranchMenu(gitRoot(), m_branchButton->mapToGlobal(QPoint(0, 0)));
    });

    const auto badgesChanged = [this] {
        m_explorer->setGitBadges(m_sourceControl->badges());
        refreshGitStatus();
    };
    connect(m_sourceControl, &SourceControl::repositoriesChanged, this, badgesChanged);
    connect(m_sourceControl, &SourceControl::statusChanged, this, badgesChanged);
    connect(m_sourceControl, &SourceControl::repositoriesChanged, this, [this] {
        m_gitMenu->menuAction()->setVisible(m_sourceControl->enabled());
        for (CodeEditor *editor : m_tabs->editors())
            refreshDiffBase(editor);
    });
    connect(m_sourceControl, &SourceControl::baseChanged, this, [this](const QString &root) {
        for (CodeEditor *editor : m_tabs->editors()) {
            if (m_sourceControl->repositoryFor(editor->filePath()) == root)
                refreshDiffBase(editor);
        }
    });
    connect(m_sourceControl, &SourceControl::message, this,
            [this](const QString &text) { statusBar()->showMessage(text, 5000); });
    connect(m_sourceControl, &SourceControl::operationStarted, this, [this](const QString &root, const QString &title) {
        const QString log = gitLogName(root);
        m_gitLogs.insert(log, root);
        m_output->record(log, QStringLiteral("$ %1\n").arg(title));
        m_output->setState(log, true, 0);
    });
    connect(m_sourceControl, &SourceControl::operationFinished, this,
            [this](const QString &root, const QString &title, int exitCode, const QString &output) {
                Q_UNUSED(title);
                const QString log = gitLogName(root);
                QString text = output;
                if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n')))
                    text += QLatin1Char('\n');
                m_output->record(log, text);
                m_output->setState(log, false, exitCode);
                if (exitCode != 0)
                    m_output->showCommand(log);
            });
    connect(m_output, &OutputPanel::stopRequested, this, [this](const QString &name) {
        if (m_gitLogs.contains(name))
            m_sourceControl->cancel(m_gitLogs.value(name));
    });

    connect(m_tabs, &TabWorkspace::editorOpened, this, &MainWindow::refreshDiffBase);
    connect(m_tabs, &TabWorkspace::editorRetargeted, this, &MainWindow::refreshDiffBase);
    connect(m_tabs, &TabWorkspace::sessionRestored, this, [this] {
        for (CodeEditor *editor : m_tabs->editors())
            refreshDiffBase(editor);
    });
    connect(m_tabs, &TabWorkspace::fileSaved, this, [this](const QString &directory) {
        const QString root = m_sourceControl->repositoryFor(directory);
        if (!root.isEmpty())
            m_sourceControl->refresh(root);
    });
    connect(m_explorer, &ExplorerPanel::pathMoved, this, [this] { m_sourceControl->refresh(); });

    connect(m_actions.gitRefresh, &QAction::triggered, m_sourceControl, qOverload<>(&SourceControl::refresh));
    connect(m_actions.gitFetchAll, &QAction::triggered, m_sourceControl, &SourceControl::fetchAll);
    connect(m_actions.gitPull, &QAction::triggered, this, [this] { m_sourceControl->pull(gitRoot()); });
    connect(m_actions.gitPush, &QAction::triggered, this, [this] { m_sourceControl->push(gitRoot()); });
    connect(m_actions.gitStash, &QAction::triggered, this, [this] { m_sourceControl->stash(gitRoot()); });
    connect(m_actions.gitPopStash, &QAction::triggered, this, [this] { m_sourceControl->popStash(gitRoot()); });
    connect(m_actions.gitCommit, &QAction::triggered, this, [this] {
        m_sourcePanel->setCurrentRoot(gitRoot());
        m_actions.sourceControl->setChecked(true);
        m_sourcePanel->focusMessage();
    });
    connect(m_actions.gitBranches, &QAction::triggered, this, [this] {
        m_sourcePanel->showBranchMenu(gitRoot(), mapToGlobal(rect().center()));
    });
    connect(m_actions.gitOpenChanges, &QAction::triggered, this, [this] {
        CodeEditor *editor = m_tabs->currentEditor();
        const QString root = editor == nullptr ? QString() : m_sourceControl->repositoryFor(editor->filePath());
        if (root.isEmpty())
            return;
        GitDiffRequest request;
        request.root = root;
        request.path = m_sourceControl->relativePath(root, editor->filePath());
        request.leftObject = QLatin1Char(':') + request.path;
        request.rightWorking = true;
        request.leftLabel = QStringLiteral("Index");
        request.rightLabel = QStringLiteral("Working Tree");
        openGitDiff(request);
    });
    connect(m_actions.gitFileHistory, &QAction::triggered, this, [this] {
        CodeEditor *editor = m_tabs->currentEditor();
        const QString root = editor == nullptr ? QString() : m_sourceControl->repositoryFor(editor->filePath());
        if (!root.isEmpty())
            m_sourcePanel->showHistory(root, m_sourceControl->relativePath(root, editor->filePath()));
    });
}

void MainWindow::updateSidebar()
{
    m_sidebar->setVisible(m_explorer->isVisibleTo(m_sidebar) || m_sourcePanel->isVisibleTo(m_sidebar));
}

QString MainWindow::gitRoot() const
{
    const QString path = m_tabs->fileFor(m_tabs->currentWidget()).path();
    const QString root = path.isEmpty() ? QString() : m_sourceControl->repositoryFor(path);
    return root.isEmpty() ? m_sourcePanel->currentRoot() : root;
}

void MainWindow::refreshGitStatus()
{
    const QString root = gitRoot();
    for (QAction *action : {m_actions.gitPull, m_actions.gitPush, m_actions.gitCommit, m_actions.gitBranches,
                            m_actions.gitStash, m_actions.gitPopStash})
        action->setEnabled(!root.isEmpty());
    const QString path = m_tabs->fileFor(m_tabs->currentWidget()).path();
    const bool tracked = !path.isEmpty() && !m_sourceControl->repositoryFor(path).isEmpty();
    m_actions.gitOpenChanges->setEnabled(tracked && m_tabs->currentEditor() != nullptr);
    m_actions.gitFileHistory->setEnabled(tracked && m_tabs->currentEditor() != nullptr);
    const GitStatus *status = root.isEmpty() ? nullptr : m_sourceControl->status(root);
    m_branchButton->setVisible(status != nullptr);
    if (status == nullptr)
        return;
    QString text = QStringLiteral("\u2387 ") + (status->branch.isEmpty() ? status->head.left(7) : status->branch);
    if (status->ahead > 0)
        text += QStringLiteral(" \u2191%1").arg(status->ahead);
    if (status->behind > 0)
        text += QStringLiteral(" \u2193%1").arg(status->behind);
    if (!status->changes.isEmpty())
        text += QStringLiteral(" \u2022%1").arg(status->changes.size());
    m_branchButton->setText(text);
    m_branchButton->setToolTip(QStringLiteral("%1 — branches").arg(QDir::toNativeSeparators(root)));
}

void MainWindow::refreshDiffBase(CodeEditor *editor)
{
    const QString path = editor->filePath();
    const QString root = path.isEmpty() ? QString() : m_sourceControl->repositoryFor(path);
    if (root.isEmpty()) {
        editor->clearDiffBase();
        return;
    }
    QPointer<CodeEditor> alive(editor);
    m_sourceControl->readObject(root, QLatin1Char(':') + m_sourceControl->relativePath(root, path),
                                [alive, path](bool found, const QByteArray &bytes) {
                                    if (alive.isNull() || alive->filePath() != path)
                                        return;
                                    if (!found) {
                                        alive->clearDiffBase();
                                        return;
                                    }
                                    DocumentFile file;
                                    alive->setDiffBase(file.decode(bytes));
                                });
}

void MainWindow::openGitDiff(GitDiffRequest request)
{
    const QString path = m_sourceControl->absolutePath(request.root, request.path);
    if (request.rightWorking) {
        for (CodeEditor *editor : m_tabs->editors()) {
            if (editor->filePath().compare(path, Qt::CaseInsensitive) == 0) {
                request.hasWorkingText = true;
                request.workingText = editor->toPlainText();
                break;
            }
        }
    }
    const QString title = QStringLiteral("%1 (%2 \u2194 %3)")
                              .arg(QFileInfo(path).fileName(), request.leftLabel, request.rightLabel);
    m_sourceControl->loadDiff(request, [this, request, path, title](const QString &left, const QString &right,
                                                                     const QList<LineChange> &changes) {
        DiffContent content;
        content.title = title;
        content.path = path;
        content.leftLabel = request.leftLabel;
        content.rightLabel = request.rightLabel;
        content.left = left;
        content.right = right;
        m_tabs->showDiff(content, changes);
    });
}

QString MainWindow::gitLogName(const QString &root)
{
    return QStringLiteral("git: ") + QDir(root).dirName();
}

void MainWindow::openFileDialog()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, QStringLiteral("Open Files"));
    for (const QString &path : paths)
        m_tabs->openFile(path);
}

bool MainWindow::chooseExplorerFolder()
{
    const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("Open Folder"),
                                                           m_explorer->rootPath());
    if (path.isEmpty())
        return false;
    m_explorer->setRootPath(path);
    return true;
}

void MainWindow::chooseWslFolder()
{
    m_orchestrator->submit(QStringLiteral("wsl-distros"), QStringLiteral("WSL distros"), QString(), 0,
                           QStringLiteral("wsl --list --verbose"), [this] {
        const QStringList names = WslPath::distros();
        QMetaObject::invokeMethod(this, [this, names] { offerWslDistros(names); }, Qt::QueuedConnection);
    }, ProcessLane::Thread);
}

void MainWindow::offerWslDistros(const QStringList &names)
{
    if (names.isEmpty()) {
        QMessageBox::warning(this, QCoreApplication::applicationName(),
                             QStringLiteral("WSL is not available."));
        return;
    }

    QString distro = names.first();
    if (names.size() > 1) {
        bool accepted = false;
        distro = QInputDialog::getItem(this, QCoreApplication::applicationName(),
                                       QStringLiteral("Distro:"), names, 0, false, &accepted);
        if (!accepted || distro.isEmpty())
            return;
    }

    m_orchestrator->submit(QStringLiteral("wsl-home"), QStringLiteral("WSL home"), distro, 0, distro,
                           [this, distro] {
        const QString home = WslPath::home(distro);
        QMetaObject::invokeMethod(this, [this, distro, home] { offerWslPath(distro, home); },
                                  Qt::QueuedConnection);
    }, ProcessLane::Thread);
}

void MainWindow::offerWslPath(const QString &distro, const QString &home)
{
    bool accepted = false;
    QString linuxPath = QInputDialog::getText(this, QCoreApplication::applicationName(),
                                              QStringLiteral("Linux path:"), QLineEdit::Normal,
                                              home.isEmpty() ? QStringLiteral("/") : home, &accepted)
                            .trimmed();
    if (!accepted)
        return;
    if (linuxPath.isEmpty())
        linuxPath = home.isEmpty() ? QStringLiteral("/") : home;
    if (linuxPath == QLatin1String("~"))
        linuxPath = home.isEmpty() ? QStringLiteral("/") : home;
    else if (linuxPath.startsWith(QLatin1String("~/")))
        linuxPath = (home.isEmpty() ? QStringLiteral("/") : home) + linuxPath.mid(1);

    m_explorer->setRootPath(WslPath::toWindows(distro, linuxPath));
    m_actions.explorer->setChecked(true);
}

QString MainWindow::workOwner(QWidget *page) const
{
    if (page == nullptr)
        return m_explorer->rootPath();
    const QString title = m_tabs->titleFor(page);
    if (title.isEmpty())
        return m_explorer->rootPath();
    const QString path = m_tabs->fileFor(page).path();
    if (path.isEmpty())
        return title;
    return title + QStringLiteral(" — ") + path;
}

void MainWindow::closeCurrentTab()
{
    if (m_tabs->closeTab(m_tabs->currentIndex()) && m_tabs->documentCount() == 0)
        m_tabs->newDocument();
}

void MainWindow::gotoLine()
{
    CodeEditor *editor = m_tabs->currentEditor();
    bool accepted = false;
    const int line = QInputDialog::getInt(this, QStringLiteral("Go to Line"),
                                          QStringLiteral("Line:"),
                                          editor->textCursor().blockNumber() + 1, 1,
                                          editor->blockCount(), 1, &accepted);
    if (!accepted)
        return;
    QTextCursor cursor(editor->document()->findBlockByNumber(line - 1));
    editor->setTextCursor(cursor);
    editor->centerCursor();
    editor->setFocus();
}

void MainWindow::applyFileIconTheme(const FileIconTheme *theme)
{
    if (theme == nullptr)
        return;
    for (QAction *action : m_iconThemeGroup->actions())
        action->setChecked(action->text() == theme->name);
    QSettings().setValue(kFileIconThemeSetting, theme->name);
    if (m_projectOwnsIcons && !m_projectIcons.theme.isEmpty())
        return;
    if (m_projectOwnsIcons)
        applyComposedIcons();
    else
        m_explorer->setIconTheme(theme);
}

void MainWindow::applyComposedIcons()
{
    const QString global = QSettings().value(kFileIconThemeSetting, QStringLiteral("Seti")).toString();
    if (m_projectIcons.theme.isEmpty() && m_projectIcons.rules.isEmpty()) {
        m_projectOwnsIcons = false;
        m_explorer->setIconTheme(iconThemeNamed(m_iconThemes, global));
        return;
    }
    const QString baseName = m_projectIcons.theme.isEmpty() ? global : m_projectIcons.theme;
    const FileIconTheme *base = iconThemeNamed(m_iconThemes, baseName);
    if (base == nullptr)
        return;
    m_composedIcons = FileIconTheme::compose(*base, m_projectIcons.rules, m_iconThemes);
    m_projectOwnsIcons = true;
    m_explorer->setIconTheme(&m_composedIcons);
}

void MainWindow::editIconTemplates()
{
    const QString fallback = QSettings().value(kFileIconThemeSetting, QStringLiteral("Seti")).toString();
    TemplateDialog dialog(m_iconThemes, m_projectIcons, fallback, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QString file = m_projectFile;
    if (file.isEmpty()) {
        if (m_explorer->rootPath().isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Open a folder before saving icon rules"), 5000);
            return;
        }
        file = QDir(m_explorer->rootPath()).filePath(QStringLiteral(".crlf"));
    }
    if (!saveProjectIcons(file, dialog.icons())) {
        statusBar()->showMessage(QStringLiteral("Couldn't write .crlf"), 5000);
        return;
    }
    reloadCommands();
}

void MainWindow::applyTheme(const Theme *theme)
{
    m_tabs->setTheme(theme);
    m_bottom->setColors(theme->foreground, theme->background, theme->selection, theme->terminal);
    m_explorer->setGitColors(theme->gitAdded, theme->gitModified, theme->gitDeleted, theme->gitConflicted);
    m_sourcePanel->setToneColors(theme->gitAdded, theme->gitModified, theme->gitDeleted, theme->gitConflicted);
    setStyleSheet(chromeStyleSheet(*theme));
    // After the stylesheet, so polish does not clear the scroll buttons' fill.
    m_tabs->setTabColors(theme->foreground, theme->gutterForeground, chromeBorder(*theme),
                         chromeColor(*theme));
    for (QAction *action : m_themeGroup->actions())
        action->setChecked(action->text() == theme->name);
    QSettings().setValue(kThemeSetting, theme->name);
}

void MainWindow::refreshStatus()
{
    refreshGitStatus();
    if (m_tabs->isToolPage(m_tabs->currentWidget())) {
        setWindowTitle(QStringLiteral("%1 - %2").arg(m_tabs->titleFor(m_tabs->currentWidget()),
                                                     QCoreApplication::applicationName()));
        m_actions.hexMode->setChecked(false);
        m_actions.byteColumns->setEnabled(false);
        m_actions.setTextEditingEnabled(false);
        m_languageGroup->setEnabled(false);
        return;
    }
    if (m_tabs->documentCount() == 0) {
        setWindowTitle(QCoreApplication::applicationName());
        return;
    }
    QWidget *page = m_tabs->currentWidget();
    const DocumentFile &file = m_tabs->fileFor(page);
    setWindowTitle(QStringLiteral("%1 - %2").arg(m_tabs->titleFor(page),
                                                 QCoreApplication::applicationName()));
    m_encodingLabel->setText(file.encodingName());

    HexView *hex = m_tabs->currentHexView();
    m_actions.hexMode->setChecked(hex != nullptr);
    m_actions.byteColumns->setEnabled(hex != nullptr);
    m_actions.setTextEditingEnabled(hex == nullptr);
    m_languageGroup->setEnabled(hex == nullptr);

    if (hex) {
        m_findBar->hide();
        m_cursorLabel->setText(QStringLiteral("Offset 0x%1")
                                   .arg(QString::number(hex->cursorOffset(), 16)
                                            .rightJustified(8, QLatin1Char('0'))
                                            .toUpper()));
        m_languageLabel->setText(QStringLiteral("Hex"));
        m_eolLabel->setText(QStringLiteral("%1 bytes").arg(hex->data().size()));
        return;
    }

    CodeEditor *editor = m_tabs->currentEditor();
    const QTextCursor cursor = editor->textCursor();
    m_cursorLabel->setText(QStringLiteral("Ln %1, Col %2")
                               .arg(cursor.blockNumber() + 1)
                               .arg(cursor.positionInBlock() + 1));
    m_languageLabel->setText(editor->language()->name);
    for (QAction *action : m_languageGroup->actions())
        action->setChecked(action->text() == editor->language()->name);
    m_eolLabel->setText(file.lineEndingName());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // With persistence on, exiting loses nothing: the session carries every
    // unsaved buffer, so no save prompts are needed.
    if (m_actions.persistEdits->isChecked()) {
        m_sessionStore.save(m_tabs->captureSession());
        m_bottom->closeSessions();
        QMetaObject::invokeMethod(m_runner, [this] { m_runner->stopAll(); }, Qt::QueuedConnection);
        QMetaObject::invokeMethod(m_syntax, [this] { m_syntax->cancelAll(); }, Qt::QueuedConnection);
        QMetaObject::invokeMethod(m_gitClient, [this] { m_gitClient->cancelAll(); }, Qt::QueuedConnection);
        event->accept();
        return;
    }
    m_sessionStore.clear();
    while (m_tabs->documentCount() > 0) {
        if (!m_tabs->closeTab(0)) {
            event->ignore();
            return;
        }
    }
    m_bottom->closeSessions();
    QMetaObject::invokeMethod(m_runner, [this] { m_runner->stopAll(); }, Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_syntax, [this] { m_syntax->cancelAll(); }, Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_gitClient, [this] { m_gitClient->cancelAll(); }, Qt::QueuedConnection);
    event->accept();
}

CommandVariables MainWindow::commandVariables() const
{
    return variablesFor(m_tabs->currentWidget());
}

CommandVariables MainWindow::variablesFor(QWidget *page) const
{
    CommandVariables vars;
    if (!m_projectFile.isEmpty())
        vars.project = QFileInfo(m_projectFile).absolutePath();
    vars.folder = m_explorer->rootPath();

    CodeEditor *editor = qobject_cast<CodeEditor *>(page);
    HexView *hex = qobject_cast<HexView *>(page);
    if (!editor && !hex)
        return vars;

    const DocumentFile &file = m_tabs->fileFor(page);
    vars.file = file.path();
    if (vars.file.isEmpty()) {
        vars.fileName = m_tabs->titleFor(page);
    } else {
        const QFileInfo info(vars.file);
        vars.fileName = info.fileName();
        vars.fileBase = info.completeBaseName();
        vars.fileExt = info.suffix();
        vars.fileDir = info.absolutePath();
        if (!vars.project.isEmpty())
            vars.relativeFile = QDir(vars.project).relativeFilePath(vars.file);
    }

    if (hex) {
        vars.language = QStringLiteral("Hex");
        return vars;
    }

    const QTextCursor cursor = editor->textCursor();
    vars.line = QString::number(cursor.blockNumber() + 1);
    vars.column = QString::number(cursor.positionInBlock() + 1);
    vars.selection = cursor.selectedText();
    vars.selection.replace(QChar(0x2029), QLatin1Char('\n'));
    vars.selection.replace(QChar(0x2028), QLatin1Char('\n'));
    vars.language = editor->language()->name;
    return vars;
}

QString MainWindow::diagnosticKey(const CodeEditor *editor) const
{
    if (editor->filePath().isEmpty())
        return QStringLiteral("unsaved:%1").arg(quintptr(editor));
    return editor->filePath();
}

void MainWindow::runSyntax(CodeEditor *editor)
{
    if (!m_syntaxEnabled || m_syntaxCommand.isEmpty() || editor == nullptr)
        return;
    QString text = editor->toPlainText();
    text.remove(QLatin1Char('\r'));
    SyntaxRequest request;
    request.editorId = quintptr(editor);
    request.command = m_syntaxCommand;
    request.workingDirectory = m_syntaxDirectory;
    request.variables = variablesFor(editor);
    request.text = text;
    request.revision = editor->documentRevision();
    const QString key = QStringLiteral("syntax:%1").arg(request.editorId);
    m_orchestrator->submit(key, QStringLiteral("Syntax"), workOwner(editor), 0, request.command,
                           [checks = m_syntax, request] { checks->start(request); }, ProcessLane::Hold);
}

void MainWindow::startCompletion(bool restart)
{
    const QString groupDirectory =
        m_projectFile.isEmpty() ? QString() : QFileInfo(m_projectFile).absolutePath();
    CommandVariables vars;
    vars.project = groupDirectory;
    vars.folder = m_explorer->rootPath();
    vars.cwd = m_autocompleteDirectory;
    const QString command = m_autocompleteCommand;
    const QString directory = m_autocompleteDirectory;
    // A new process knows no files. Each open below is answered with a state.
    m_fileStates.clear();
    m_orchestrator->submit(QStringLiteral("autocomplete"), QStringLiteral("autocomplete"), groupDirectory, 0, command,
                           [session = m_completion, command, directory, vars, restart] {
                               session->start(command, directory, vars, restart);
                           },
                           ProcessLane::Hold);
    // Queued behind the start on the same thread, so they reach the new
    // session. Every tab is opened, so its server is loaded before it is
    // selected.
    for (CodeEditor *editor : m_tabs->editors())
        warmCompletion(editor);
}

void MainWindow::warmCompletion(CodeEditor *editor)
{
    if (!m_autocompleteEnabled || editor->filePath().isEmpty())
        return;
    sendCompletion(editor, QStringLiteral("open"), 0);
}

const ProcessRecord *MainWindow::completionRecord() const
{
    for (const ProcessRecord &record : m_liveProcesses) {
        if (record.key == QLatin1String("autocomplete"))
            return &record;
    }
    return nullptr;
}

QString MainWindow::completionStatus() const
{
    const ProcessRecord *record = completionRecord();
    if (record == nullptr)
        return QStringLiteral("stopped");
    const CodeEditor *editor = m_tabs->currentEditor();
    // A page that is not an editor shows the session as a whole.
    if (editor == nullptr)
        return record->activity.isEmpty() ? QStringLiteral("starting") : record->activity;
    if (editor->filePath().isEmpty())
        return QStringLiteral("not available until the file is saved");
    const auto state = m_fileStates.constFind(editor->filePath());
    if (state == m_fileStates.constEnd())
        return QStringLiteral("starting");
    if (state->state == QLatin1String("unsupported"))
        return QStringLiteral("not available for ") + editor->language()->name;
    return state->message;
}

void MainWindow::refreshCompletionStatus()
{
    m_completionLabel->setVisible(m_autocompleteEnabled);
    if (m_autocompleteEnabled)
        m_completionLabel->setText(QStringLiteral("Autocomplete: ") + completionStatus());
}

void MainWindow::applyFileState(const QString &path, const CompletionFileState &state)
{
    m_fileStates.insert(path, state);
    for (CodeEditor *editor : m_tabs->editors()) {
        if (editor->filePath() == path)
            editor->setCompletionTriggers(state.triggers);
    }
    refreshCompletionStatus();
}

void MainWindow::runCommand(const QString &name)
{
    if (m_editorCommands.contains(name) && m_tabs->currentEditor() != nullptr)
        m_output->allowReveal();
    else
        m_output->showCommand(name);
    m_orchestrator->submit(QStringLiteral("command:%1").arg(name), name, workOwner(m_tabs->currentWidget()), 0,
                           m_commandTexts.value(name), [runner = m_runner, name] { runner->requestLaunch(name); },
                           ProcessLane::Hold);
}

bool MainWindow::canReload(const QString &key)
{
    return key == QLatin1String("autocomplete") || key.startsWith(QLatin1String("syntax:"))
           || key.startsWith(QLatin1String("command:"));
}

void MainWindow::cancelProcess(const QString &key)
{
    m_orchestrator->cancel(key);
    if (key == QLatin1String("autocomplete")) {
        QMetaObject::invokeMethod(m_completion, [session = m_completion] { session->stop(); },
                                  Qt::QueuedConnection);
    } else if (key.startsWith(QLatin1String("syntax:"))) {
        const quintptr editorId = key.mid(7).toULongLong();
        QMetaObject::invokeMethod(m_syntax, [checks = m_syntax, editorId] { checks->cancel(editorId); },
                                  Qt::QueuedConnection);
    } else if (key.startsWith(QLatin1String("command:"))) {
        const QString name = key.mid(8);
        QMetaObject::invokeMethod(m_runner, [runner = m_runner, name] { runner->stop(name); },
                                  Qt::QueuedConnection);
    } else if (key.startsWith(QLatin1String("git:"))) {
        QMetaObject::invokeMethod(m_gitClient, [client = m_gitClient, key] { client->cancel(key); },
                                  Qt::QueuedConnection);
    }
}

void MainWindow::restartServices()
{
    if (m_autocompleteEnabled)
        startCompletion(true);
    if (m_syntaxEnabled) {
        for (CodeEditor *editor : m_tabs->editors())
            runSyntax(editor);
    }
}

void MainWindow::reloadProcess(const QString &key)
{
    if (key == QLatin1String("autocomplete")) {
        if (m_autocompleteEnabled)
            startCompletion(true);
    } else if (key.startsWith(QLatin1String("syntax:"))) {
        if (CodeEditor *editor = m_tabs->editorFor(key.mid(7).toULongLong()))
            runSyntax(editor);
    } else if (key.startsWith(QLatin1String("command:"))) {
        runCommand(key.mid(8));
    }
}

bool MainWindow::completionServes(const CodeEditor *editor) const
{
    if (!m_autocompleteEnabled || editor->filePath().isEmpty())
        return false;
    const QString state = m_fileStates.value(editor->filePath()).state;
    return state != QLatin1String("unsupported") && state != QLatin1String("missing");
}

void MainWindow::sendCompletion(CodeEditor *editor, const QString &kind, int id, const QString &trigger,
                                const QString &data)
{
    CompletionRequest request;
    request.kind = kind;
    request.id = id;
    request.revision = editor->documentRevision();
    request.path = editor->filePath();
    request.line = editor->textCursor().blockNumber() + 1;
    request.column = editor->textCursor().positionInBlock() + 1;
    request.trigger = trigger;
    request.data = data;
    if (kind != QLatin1String("resolve")) {
        request.text = editor->toPlainText();
        request.text.remove(QLatin1Char('\r'));
    }
    QMetaObject::invokeMethod(m_completion, [session = m_completion, request] { session->request(request); },
                              Qt::QueuedConnection);
}

void MainWindow::askCompletion(CodeEditor *editor, const QString &trigger)
{
    if (completionServes(editor))
        sendCompletion(editor, QStringLiteral("completion"), ++m_completionId, trigger);
}

void MainWindow::askDocumentation(CodeEditor *editor, const CompletionItem &item)
{
    if (item.data == m_resolveData)
        return;
    m_resolveData = item.data;
    sendCompletion(editor, QStringLiteral("resolve"), ++m_resolveId, QString(), item.data);
}

void MainWindow::askSignature(CodeEditor *editor)
{
    if (completionServes(editor))
        sendCompletion(editor, QStringLiteral("signature"), ++m_signatureId);
}

void MainWindow::applyCompletion(int id, int revision, const QList<CompletionItem> &items, bool incomplete)
{
    if (id != m_completionId)
        return;
    CodeEditor *editor = m_tabs->currentEditor();
    if (editor == nullptr || editor->documentRevision() != revision)
        return;
    editor->showCompletions(items, incomplete);
}

void MainWindow::applyResolved(int id, const QString &data, const QString &detail, const QString &documentation)
{
    if (id != m_resolveId || data != m_resolveData)
        return;
    if (CodeEditor *editor = m_tabs->currentEditor())
        editor->showResolved(data, detail, documentation);
}

void MainWindow::applySignature(int id, int revision, const QList<SignatureHint> &signatures, int active)
{
    if (id != m_signatureId)
        return;
    CodeEditor *editor = m_tabs->currentEditor();
    if (editor == nullptr || editor->documentRevision() != revision)
        return;
    editor->showSignatures(signatures, active);
}

void MainWindow::applySyntax(quintptr editorId, int revision, const QList<Diagnostic> &spans)
{
    if (!m_syntaxEnabled || m_syntaxCommand.isEmpty())
        return;
    CodeEditor *editor = m_tabs->editorFor(editorId);
    if (!editor || editor->documentRevision() != revision)
        return;
    editor->setDiagnostics(spans);
    const QString key = diagnosticKey(editor);
    const QString previous = m_diagnosticKeys.value(editorId);
    if (!previous.isEmpty() && previous != key)
        m_bottom->problems()->removeFile(previous);
    if (spans.isEmpty()) {
        m_bottom->problems()->removeFile(key);
        m_diagnosticKeys.remove(editorId);
        return;
    }
    m_diagnosticKeys.insert(editorId, key);
    m_bottom->problems()->setFile(key, editor->filePath(), m_tabs->titleFor(editor), editorId, spans);
}

void MainWindow::reloadCommands(bool restart)
{
    QString start = m_explorer->rootPath();
    if (start.isEmpty() && m_tabs->documentCount() > 0)
        start = m_tabs->fileFor(m_tabs->currentWidget()).path();

    const int generation = ++m_commandGeneration;
    const QString owner = start.isEmpty() ? QString() : start;
    m_orchestrator->submit(QStringLiteral("project"), QStringLiteral("Project"), owner, 0,
                           owner.isEmpty() ? QStringLiteral(".crlf") : owner, [this, start, generation, restart] {
        const Project project = projectFor(start);
        QMetaObject::invokeMethod(this, [this, project, generation, restart] {
            if (generation != m_commandGeneration)
                return;
            applyProject(project, restart);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::applyProject(const Project &project, bool restart)
{
    if (!m_projectWatcher->files().isEmpty())
        m_projectWatcher->removePaths(m_projectWatcher->files());
    m_projectFile = project.filePath;
    if (!m_projectFile.isEmpty())
        m_projectWatcher->addPaths(QStringList{m_projectFile} + project.syntax.watch + project.autocomplete.watch);

    QMetaObject::invokeMethod(m_runner, [runner = m_runner, commands = project.commands] {
        runner->setProject(commands);
    }, Qt::BlockingQueuedConnection);
    m_editorCommands.clear();
    for (const ProjectCommand &command : project.commands) {
        if (command.outputToEditor)
            m_editorCommands.insert(command.name);
    }
    const QString groupDirectory =
        project.filePath.isEmpty() ? QString() : QFileInfo(project.filePath).absolutePath();
    m_explorer->setGroups(groupDirectory, project.groups);
    m_explorer->setHidden(project.hide);
    m_projectIcons = project.icons;
    applyComposedIcons();
    m_tabs->setSymbolNavigation(project.navigation);
    m_tabs->setExternalNavigation(project.externalNavigation);
    m_tabs->setSymbolProject(m_explorer->rootPath(), project.hide);
    m_tabs->setIndent(project.indent);
    m_sourceControl->setProject(project.git, m_explorer->rootPath().isEmpty() ? groupDirectory : m_explorer->rootPath(),
                                project.hide);
    const bool syntaxEnabled = project.syntax.enabled && !project.syntax.command.isEmpty();
    const bool syntaxChanged = syntaxEnabled != m_syntaxEnabled || project.syntax.command != m_syntaxCommand
                              || project.syntax.workingDirectory != m_syntaxDirectory;
    m_syntaxEnabled = syntaxEnabled;
    m_syntaxCommand = project.syntax.command;
    m_syntaxDirectory = project.syntax.workingDirectory;
    m_bottom->problems()->setProjectDirectory(
        project.filePath.isEmpty() ? QString() : QFileInfo(project.filePath).absolutePath());
    if (syntaxChanged)
        m_tabs->setSyntaxEnabled(m_syntaxEnabled);
    if (!m_syntaxEnabled) {
        QMetaObject::invokeMethod(m_syntax, [this] { m_syntax->cancelAll(); }, Qt::QueuedConnection);
        m_bottom->problems()->clear();
        m_diagnosticKeys.clear();
    }
    const bool autocompleteEnabled = project.autocomplete.enabled && !project.autocomplete.command.isEmpty();
    const bool autocompleteChanged = autocompleteEnabled != m_autocompleteEnabled || project.autocomplete.command != m_autocompleteCommand
                                 || project.autocomplete.workingDirectory != m_autocompleteDirectory;
    m_autocompleteEnabled = autocompleteEnabled;
    m_autocompleteCommand = project.autocomplete.command;
    m_autocompleteDirectory = project.autocomplete.workingDirectory;
    m_orchestrator->setUnlimitedLog(project.unlimitedLog);
    if (autocompleteChanged)
        m_tabs->setCompletionEnabled(m_autocompleteEnabled);
    if (!m_autocompleteEnabled)
        QMetaObject::invokeMethod(m_completion, [this] { m_completion->stop(); }, Qt::QueuedConnection);
    else if (autocompleteChanged && !restart)
        startCompletion(false);
    if (restart)
        restartServices();
    refreshCompletionStatus();
    m_commandTexts.clear();
    for (const ProjectCommand &command : project.commands)
        m_commandTexts.insert(command.name, command.command);
    clearProjectCommands(m_menuBar);
    if (project.commands.isEmpty()) {
        if (!project.error.isEmpty())
            statusBar()->showMessage(project.error, 5000);
    } else {
        for (const ProjectCommand &command : project.commands) {
            QAction *action = addProjectAction(menuAt(m_menuBar, m_runMenu, command.menu), command.name);
            action->setCheckable(true);
            action->setChecked(m_runningCommands.contains(command.name));
            if (!command.key.isEmpty()) {
                action->setShortcut(command.key);
                action->setShortcutContext(Qt::WindowShortcut);
            }
            connect(action, &QAction::triggered, this, [this, name = command.name] { runCommand(name); });
        }
    }
    m_runMenu->menuAction()->setVisible(!m_runMenu->actions().isEmpty());
}
