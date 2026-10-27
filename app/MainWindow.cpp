#include "app/MainWindow.h"

#include "app/FindBar.h"
#include "app/TabWorkspace.h"
#include "app/TitleBar.h"
#include "editor/CodeEditor.h"
#include "editor/FoldController.h"
#include "hex/HexView.h"
#include "io/DocumentFile.h"
#include "syntax/LanguageDefinition.h"

#include <QActionGroup>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
constexpr auto kPersistSetting = "persistUnsavedEdits";
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

// The whole window chrome (title bar, tabs, menus, find bar, status bar)
// is styled from the active editor theme so it follows dark/light switches.
QString chromeStyleSheet(const Theme &theme)
{
    const bool dark = theme.background.lightness() < 128;
    const QColor chrome = dark ? theme.background.lighter(135) : theme.background.darker(106);
    const QColor border = dark ? theme.background.lighter(190) : theme.background.darker(122);
    const QColor hover = dark ? QColor(255, 255, 255, 24) : QColor(0, 0, 0, 18);
    const QColor pressed = dark ? QColor(255, 255, 255, 38) : QColor(0, 0, 0, 32);
    const QColor scrollHandle = dark ? QColor(255, 255, 255, 50) : QColor(0, 0, 0, 60);

    return QStringLiteral(R"(
QMainWindow { background: %1; }
#TitleBar { background: %1; }
#TitleBar QLabel { color: %2; background: transparent; }
#TitleBar QToolButton { border: none; background: transparent; color: %2; }
#TitleBar QToolButton:hover { background: %4; }
#TitleBar QToolButton:pressed { background: %5; }
#closeButton:hover { background: #e81123; color: #ffffff; }
#closeButton:pressed { background: #f1707a; color: #ffffff; }
QMenuBar { background: transparent; color: %2; }
QMenuBar::item { background: transparent; padding: 4px 10px; border-radius: 6px; }
QMenuBar::item:selected { background: %4; }
QMenuBar::item:pressed { background: %5; }
QMenu { background: %1; color: %2; border: 1px solid %3; border-radius: 8px; padding: 4px; }
QMenu::item { padding: 5px 28px 5px 16px; border-radius: 5px; }
QMenu::item:selected { background: %6; }
QMenu::separator { height: 1px; background: %3; margin: 4px 8px; }
QTabWidget::pane { border: none; }
QTabBar { background: transparent; qproperty-drawBase: 0; }
QPlainTextEdit { border: none; }
QTabBar::tab { background: transparent; color: %2; padding: 5px 18px;
               margin: 3px 2px 0 2px;
               border-top-left-radius: 7px; border-top-right-radius: 7px; }
QTabBar::tab:selected { background: %7; }
QTabBar::tab:hover:!selected { background: %4; }
#tabCloseButton { border: none; border-radius: 4px; background: transparent; color: %2; }
#tabCloseButton:hover { background: %4; }
#tabCloseButton:pressed { background: %5; }
QStatusBar { background: %1; color: %2; }
QStatusBar QLabel { color: %2; padding: 0 6px; }
QStatusBar::item { border: none; }
FindBar { background: %1; }
FindBar QLabel { color: %2; }
FindBar QToolButton { border: none; border-radius: 5px; color: %2; padding: 3px; }
FindBar QToolButton:hover { background: %4; }
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
)")
        .arg(chrome.name(), theme.foreground.name(), border.name(), rgbaString(hover),
             rgbaString(pressed), theme.selection.name(), theme.background.name(),
             rgbaString(scrollHandle));
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_darkTheme(Theme::fromJsonFile(QStringLiteral(":/themes/dark.json")))
    , m_lightTheme(Theme::fromJsonFile(QStringLiteral(":/themes/light.json")))
    , m_actions(this)
{
    m_tabs = new TabWorkspace(&m_registry, &m_darkTheme, this);
    m_findBar = new FindBar(this);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_tabs, 1);
    layout->addWidget(m_findBar);
    setCentralWidget(central);

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

    m_actions.persistEdits->setChecked(QSettings().value(kPersistSetting, true).toBool());

    // Periodic snapshot so unsaved edits also survive crashes and power loss,
    // not just graceful exits.
    auto *sessionTimer = new QTimer(this);
    sessionTimer->setInterval(kSessionSnapshotIntervalMs);
    connect(sessionTimer, &QTimer::timeout, this, &MainWindow::persistSession);
    sessionTimer->start();

    resize(1100, 750);
    applyTheme(&m_darkTheme);
    applyCustomFrame();
    refreshStatus();
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
    if (m_actions.persistEdits->isChecked()) {
        const Session session = m_sessionStore.load();
        if (!session.tabs.isEmpty())
            m_tabs->restoreSession(session);
    }
    for (const QString &path : paths)
        m_tabs->openFile(path);
    if (m_tabs->count() == 0)
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
    view->addAction(m_actions.wordWrap);
    view->addAction(m_actions.hexMode);
    view->addSeparator();
    view->addAction(m_actions.zoomIn);
    view->addAction(m_actions.zoomOut);
    view->addAction(m_actions.zoomReset);
    view->addSeparator();
    QMenu *theme = view->addMenu(QStringLiteral("&Theme"));
    theme->addAction(m_actions.themeDark);
    theme->addAction(m_actions.themeLight);
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
}

void MainWindow::connectActions()
{
    connect(m_actions.newFile, &QAction::triggered, m_tabs, &TabWorkspace::newDocument);
    connect(m_actions.open, &QAction::triggered, this, &MainWindow::openFileDialog);
    connect(m_actions.save, &QAction::triggered, m_tabs, &TabWorkspace::saveCurrent);
    connect(m_actions.saveAs, &QAction::triggered, m_tabs, &TabWorkspace::saveCurrentAs);
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
    // triggered (not toggled): refreshStatus syncs the checked state to the
    // active tab and must not re-enter the mode switch.
    connect(m_actions.hexMode, &QAction::triggered, m_tabs, &TabWorkspace::setCurrentHexMode);
    connect(m_actions.zoomIn, &QAction::triggered, this, [this] { m_tabs->currentEditor()->zoom(1); });
    connect(m_actions.zoomOut, &QAction::triggered, this, [this] { m_tabs->currentEditor()->zoom(-1); });
    connect(m_actions.zoomReset, &QAction::triggered, this, [this] { m_tabs->currentEditor()->resetZoom(); });
    connect(m_actions.themeDark, &QAction::triggered, this, [this] { applyTheme(&m_darkTheme); });
    connect(m_actions.themeLight, &QAction::triggered, this, [this] { applyTheme(&m_lightTheme); });
    connect(m_actions.foldAll, &QAction::triggered, this,
            [this] { m_tabs->currentEditor()->foldController()->foldAll(); });
    connect(m_actions.unfoldAll, &QAction::triggered, this,
            [this] { m_tabs->currentEditor()->foldController()->unfoldAll(); });

    connect(m_tabs, &TabWorkspace::statusChanged, this, &MainWindow::refreshStatus);
    connect(m_tabs, &QTabWidget::currentChanged, this,
            [this] { m_findBar->setEditor(m_tabs->currentEditor()); });
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (m_tabs->closeTab(index) && m_tabs->count() == 0)
            m_tabs->newDocument();
    });
}

void MainWindow::openFileDialog()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, QStringLiteral("Open Files"));
    for (const QString &path : paths)
        m_tabs->openFile(path);
}

void MainWindow::closeCurrentTab()
{
    if (m_tabs->closeTab(m_tabs->currentIndex()) && m_tabs->count() == 0)
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

void MainWindow::applyTheme(const Theme *theme)
{
    m_tabs->setTheme(theme);
    setStyleSheet(chromeStyleSheet(*theme));
}

void MainWindow::refreshStatus()
{
    if (m_tabs->count() == 0) {
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
        event->accept();
        return;
    }
    m_sessionStore.clear();
    while (m_tabs->count() > 0) {
        if (!m_tabs->closeTab(0)) {
            event->ignore();
            return;
        }
    }
    event->accept();
}
