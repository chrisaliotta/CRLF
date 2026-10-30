#include "app/MainWindow.h"

#include "app/BottomPanel.h"
#include "app/CommandRunner.h"
#include "app/ExplorerPanel.h"
#include "app/FindBar.h"
#include "app/OpenWith.h"
#include "app/OutputPanel.h"
#include "app/TabWorkspace.h"
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
#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTextCursor>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
constexpr auto kPersistSetting = "persistUnsavedEdits";
constexpr auto kExplorerRootSetting = "explorerRoot";
constexpr auto kExplorerVisibleSetting = "explorerVisible";
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
OutputPanel, TerminalPanel, BottomPanel { background: %1; }
OutputPanel QLabel, TerminalPanel QLabel, BottomPanel QLabel { color: %2; }
OutputPanel QToolButton, TerminalPanel QToolButton, BottomPanel QToolButton { border: none; border-radius: 5px; color: %2; padding: 3px 8px; }
OutputPanel QToolButton:hover, TerminalPanel QToolButton:hover, BottomPanel QToolButton:hover { background: %4; }
#outputPage:checked, #terminalPage:checked { background: %5; }
OutputPanel QPlainTextEdit { background: %7; color: %2; }
OutputPanel QComboBox, TerminalPanel QComboBox { background: %7; color: %2; border: 1px solid %3; border-radius: 6px; padding: 2px 8px; }
OutputPanel QComboBox QAbstractItemView, TerminalPanel QComboBox QAbstractItemView { background: %1; color: %2; selection-background-color: %6; }
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

void markRunningCommands(QMenu *menu, const CommandRunner *runner)
{
    for (QAction *action : menu->actions()) {
        if (QMenu *child = action->menu())
            markRunningCommands(child, runner);
        else if (action->isEnabled())
            action->setChecked(runner->isRunning(action->text()));
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
    m_runner = new CommandRunner(this);
    m_runner->setContextProvider([this] { return commandVariables(); });
    m_commandWatcher = new QFileSystemWatcher(this);
    m_explorer->hide();

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(m_explorer);
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
    m_languageLabel = new QLabel(this);
    m_encodingLabel = new QLabel(this);
    m_eolLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_languageLabel);
    statusBar()->addPermanentWidget(m_encodingLabel);
    statusBar()->addPermanentWidget(m_eolLabel);
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
    QTimer::singleShot(0, this, [extensions] {
        QThreadPool::globalInstance()->start([extensions] { registerOpenWith(extensions); });
    });
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
    QThreadPool::globalInstance()->start([this, paths] {
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
    view->addAction(m_actions.terminal);
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
    m_iconThemeGroup = new QActionGroup(this);
    for (const FileIconTheme &theme : m_iconThemes) {
        QAction *action = iconMenu->addAction(theme.name);
        action->setCheckable(true);
        m_iconThemeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, theme = &theme] { applyFileIconTheme(theme); });
    }
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

    m_runMenu = m_menuBar->addMenu(QStringLiteral("&Run"));
    m_runMenu->menuAction()->setVisible(false);
}

void MainWindow::connectActions()
{
    connect(m_actions.newFile, &QAction::triggered, m_tabs, &TabWorkspace::newDocument);
    connect(m_actions.open, &QAction::triggered, this, &MainWindow::openFileDialog);
    connect(m_actions.openFolder, &QAction::triggered, this, [this] {
        if (chooseExplorerFolder())
            m_actions.explorer->setChecked(true);
    });
    connect(m_actions.openWslFolder, &QAction::triggered, this, [this] {
        if (chooseWslFolder())
            m_actions.explorer->setChecked(true);
    });
    connect(m_actions.explorer, &QAction::toggled, this, [this](bool visible) {
        if (visible && m_explorer->rootPath().isEmpty() && !chooseExplorerFolder()) {
            m_actions.explorer->setChecked(false);
            return;
        }
        m_explorer->setVisible(visible);
        QSettings().setValue(kExplorerVisibleSetting, visible);
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
    });
    connect(m_commandWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        if (QFileInfo::exists(path))
            m_commandWatcher->addPath(path);
        reloadCommands();
    });
    connect(m_runner, &CommandRunner::outputReceived, m_output, &OutputPanel::appendOutput);
    connect(m_runner, &CommandRunner::stateChanged, this,
            [this](const QString &name, bool running, int exitCode) {
                m_output->setState(name, running, exitCode);
                for (QAction *action : m_menuBar->actions()) {
                    if (QMenu *menu = action->menu())
                        markRunningCommands(menu, m_runner);
                }
            });
    connect(m_output, &OutputPanel::stopRequested, m_runner, &CommandRunner::stop);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (m_tabs->closeTab(index) && m_tabs->documentCount() == 0)
            m_tabs->newDocument();
    });
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

bool MainWindow::chooseWslFolder()
{
    const QStringList names = WslPath::distros();
    if (names.isEmpty()) {
        QMessageBox::warning(this, QCoreApplication::applicationName(),
                             QStringLiteral("WSL is not available."));
        return false;
    }

    QString distro = names.first();
    if (names.size() > 1) {
        bool accepted = false;
        distro = QInputDialog::getItem(this, QCoreApplication::applicationName(),
                                       QStringLiteral("Distro:"), names, 0, false, &accepted);
        if (!accepted || distro.isEmpty())
            return false;
    }

    const QString home = WslPath::home(distro);
    bool accepted = false;
    QString linuxPath = QInputDialog::getText(this, QCoreApplication::applicationName(),
                                              QStringLiteral("Linux path:"), QLineEdit::Normal,
                                              home.isEmpty() ? QStringLiteral("/") : home, &accepted)
                            .trimmed();
    if (!accepted)
        return false;
    if (linuxPath.isEmpty())
        linuxPath = home.isEmpty() ? QStringLiteral("/") : home;

    m_explorer->setRootPath(WslPath::toWindows(distro, linuxPath));
    return true;
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
    m_explorer->setIconTheme(theme);
    for (QAction *action : m_iconThemeGroup->actions())
        action->setChecked(action->text() == theme->name);
    QSettings().setValue(kFileIconThemeSetting, theme->name);
}

void MainWindow::applyTheme(const Theme *theme)
{
    m_tabs->setTheme(theme);
    m_bottom->setColors(theme->foreground, theme->background, theme->selection);
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
        m_runner->stopAll();
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
    m_runner->stopAll();
    event->accept();
}

CommandVariables MainWindow::commandVariables() const
{
    CommandVariables vars;
    if (!m_projectFile.isEmpty())
        vars.project = QFileInfo(m_projectFile).absolutePath();
    vars.folder = m_explorer->rootPath();

    QWidget *page = m_tabs->currentWidget();
    CodeEditor *editor = m_tabs->currentEditor();
    HexView *hex = m_tabs->currentHexView();
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

void MainWindow::reloadCommands()
{
    QString start = m_explorer->rootPath();
    if (start.isEmpty() && m_tabs->documentCount() > 0)
        start = m_tabs->fileFor(m_tabs->currentWidget()).path();

    const int generation = ++m_commandGeneration;
    QThreadPool::globalInstance()->start([this, start, generation] {
        const Project project = projectFor(start);
        QMetaObject::invokeMethod(this, [this, project, generation] {
            if (generation != m_commandGeneration)
                return;
            applyProject(project);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::applyProject(const Project &project)
{
    if (!m_projectFile.isEmpty())
        m_commandWatcher->removePath(m_projectFile);
    m_projectFile = project.filePath;
    if (!m_projectFile.isEmpty())
        m_commandWatcher->addPath(m_projectFile);

    m_runner->setProject(project.commands);
    const QString groupDirectory =
        project.filePath.isEmpty() ? QString() : QFileInfo(project.filePath).absolutePath();
    m_explorer->setGroups(groupDirectory, project.groups);
    m_explorer->setHidden(project.hide);
    m_tabs->setIndent(project.indent);
    clearProjectCommands(m_menuBar);
    if (project.commands.isEmpty()) {
        if (!project.error.isEmpty())
            statusBar()->showMessage(project.error, 5000);
    } else {
        for (const ProjectCommand &command : project.commands) {
            QAction *action = addProjectAction(menuAt(m_menuBar, m_runMenu, command.menu), command.name);
            action->setCheckable(true);
            action->setChecked(m_runner->isRunning(command.name));
            if (!command.key.isEmpty()) {
                action->setShortcut(command.key);
                action->setShortcutContext(Qt::WindowShortcut);
            }
            connect(action, &QAction::triggered, this, [this, name = command.name] {
                m_output->showCommand(name);
                m_runner->start(name);
            });
        }
    }
    m_runMenu->menuAction()->setVisible(!m_runMenu->actions().isEmpty());
}
