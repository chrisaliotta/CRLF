#include "app/TabWorkspace.h"

#include "app/DocumentTabBar.h"
#include "editor/CodeEditor.h"
#include "hex/HexView.h"
#include "syntax/LanguageRegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QTabBar>
#include <QToolButton>

namespace {

bool isPageModified(QWidget *page)
{
    if (auto *editor = qobject_cast<CodeEditor *>(page))
        return editor->document()->isModified();
    return static_cast<HexView *>(page)->isModified();
}

// Absolute, forward-slashed form for comparing paths from dialogs, the
// command line (backslashes), and the explorer. Untitled documents have none.
QString normalizedPath(const QString &path)
{
    return path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
}

void setPageModified(QWidget *page, bool modified)
{
    if (auto *editor = qobject_cast<CodeEditor *>(page))
        editor->document()->setModified(modified);
    else
        static_cast<HexView *>(page)->setModified(modified);
}

} // namespace

TabWorkspace::TabWorkspace(const LanguageRegistry *registry, const Theme *theme, QWidget *parent)
    : QTabWidget(parent)
    , m_registry(registry)
    , m_theme(theme)
{
    auto *bar = new DocumentTabBar(this);
    setTabBar(bar);
    setMovable(true);
    setDocumentMode(true);
    bar->setExpanding(false);
    // Closing the rightmost document must land on its neighbour, not on +.
    bar->setSelectionBehaviorOnRemove(QTabBar::SelectLeftTab);
    bar->setContextMenuPolicy(Qt::CustomContextMenu);

    m_newTabPage = new QWidget(this);
    addTab(m_newTabPage, QString());
    setTabToolTip(0, QStringLiteral("New Document (Ctrl+N)"));
    // The + has no close button, so its label carries both side insets.
    // Document tabs keep theirs on the title and close wrapper instead.
    auto *plus = new QLabel(QStringLiteral("+"), this);
    plus->setObjectName(QStringLiteral("newTabLabel"));
    plus->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *plusWrap = new QWidget(this);
    auto *plusLayout = new QHBoxLayout(plusWrap);
    plusLayout->setSpacing(0);
    plusLayout->setContentsMargins(10, 0, 10, 0);
    plusLayout->addWidget(plus);
    tabBar()->setTabButton(0, QTabBar::LeftSide, plusWrap);

    connect(bar, &DocumentTabBar::newTabRequested, this, &TabWorkspace::newDocument);
    connect(bar, &DocumentTabBar::tabHovered, this, [this](int index) {
        m_hoveredTab = index;
        updateCloseButtons();
    });
    connect(bar, &QTabBar::tabMoved, this, [this] { updateCloseButtons(); });
    connect(bar, &QWidget::customContextMenuRequested, this, &TabWorkspace::showTabMenu);
    connect(this, &QTabWidget::currentChanged, this, [this](int index) {
        // Wheel or keyboard navigation can still select +; step back onto
        // the last document. Clicks never select it (see DocumentTabBar).
        if (widget(index) == m_newTabPage && documentCount() > 0) {
            setCurrentIndex(index - 1);
            return;
        }
        for (auto it = m_pages.keyBegin(); it != m_pages.keyEnd(); ++it)
            refreshTabTitle(*it);
        updateCloseButtons();
        emit statusChanged();
    });
}

int TabWorkspace::documentCount() const
{
    return count() - 1;
}

CodeEditor *TabWorkspace::currentEditor() const
{
    return qobject_cast<CodeEditor *>(currentWidget());
}

HexView *TabWorkspace::currentHexView() const
{
    return qobject_cast<HexView *>(currentWidget());
}

const DocumentFile &TabWorkspace::fileFor(QWidget *page) const
{
    return m_pages.constFind(page)->file;
}

QString TabWorkspace::titleFor(QWidget *page) const
{
    return m_pages.constFind(page)->title;
}

void TabWorkspace::newDocument()
{
    addEditor(DocumentFile(), QString(), QStringLiteral("Untitled %1").arg(++m_untitledCount));
}

void TabWorkspace::openFile(const QString &path)
{
    const QString absolutePath = normalizedPath(path);
    for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
        if (normalizedPath(it->file.path()).compare(absolutePath, Qt::CaseInsensitive) == 0) {
            setCurrentWidget(it.key());
            it.key()->setFocus();
            return;
        }
    }

    DocumentFile file(path);
    const QString text = file.load();
    addEditor(file, text, file.fileName());
}

void TabWorkspace::retargetPath(const QString &from, const QString &to)
{
    const QString moved = normalizedPath(from);
    for (auto it = m_pages.begin(); it != m_pages.end(); ++it) {
        const QString path = normalizedPath(it->file.path());
        if (path.compare(moved, Qt::CaseInsensitive) != 0
            && !path.startsWith(moved + QLatin1Char('/'), Qt::CaseInsensitive))
            continue;
        const QString newPath = normalizedPath(to) + path.mid(moved.length());
        it->file.setPath(newPath);
        it->title = it->file.fileName();
        if (auto *editor = qobject_cast<CodeEditor *>(it.key()))
            editor->setLanguage(m_registry->definitionForPath(newPath));
        setTabToolTip(indexOf(it.key()), newPath);
        refreshTabTitle(it.key());
    }
    emit statusChanged();
}

CodeEditor *TabWorkspace::createEditor(const DocumentFile &file, const QString &text)
{
    auto *editor = new CodeEditor(m_registry, this);
    editor->setTheme(m_theme);
    editor->setLanguage(m_registry->definitionForPath(file.path()));
    editor->setLineWrapMode(m_wordWrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    editor->setPlainText(text);
    editor->document()->setModified(false);

    connect(editor->document(), &QTextDocument::modificationChanged, this,
            [this, editor] { refreshTabTitle(editor); });
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, [this, editor] {
        if (currentWidget() == editor)
            emit statusChanged();
    });
    return editor;
}

HexView *TabWorkspace::createHexView(const QByteArray &bytes)
{
    auto *view = new HexView(this);
    view->setTheme(m_theme);
    view->setBytesPerRow(m_bytesPerRow);
    view->setData(bytes);

    connect(view, &HexView::modificationChanged, this, [this, view] { refreshTabTitle(view); });
    connect(view, &HexView::cursorChanged, this, [this, view] {
        if (currentWidget() == view)
            emit statusChanged();
    });
    return view;
}

void TabWorkspace::insertPage(int index, QWidget *page, const DocumentFile &file,
                              const QString &title)
{
    // Registered before insertTab so the currentChanged handlers that fire
    // during insertion can already resolve the page.
    m_pages.insert(page, {file, title});
    insertTab(index, page, title);
    installTabChrome(page);
    setTabToolTip(index, file.path());
    setCurrentIndex(index);
    page->setFocus();
    for (auto it = m_pages.keyBegin(); it != m_pages.keyEnd(); ++it)
        refreshTabTitle(*it);
    updateCloseButtons();
}

CodeEditor *TabWorkspace::addEditor(const DocumentFile &file, const QString &text,
                                    const QString &title)
{
    CodeEditor *editor = createEditor(file, text);
    insertPage(documentCount(), editor, file, title);
    return editor;
}

void TabWorkspace::installTabChrome(QWidget *page)
{
    const int index = indexOf(page);
    Page &entry = m_pages[page];

    // The bar draws one font for every tab, so the title is its own label
    // and can be muted or italic on its own. The bar's text is cleared so
    // the name is not drawn twice.
    auto *title = new QLabel(entry.title, this);
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    entry.titleLabel = title;
    setTabText(index, QString());
    // Horizontal insets live here. The tab stylesheet's own padding would
    // sit outside these buttons and stack with them.
    auto *titleWrap = new QWidget(this);
    auto *titleLayout = new QHBoxLayout(titleWrap);
    titleLayout->setSpacing(0);
    titleLayout->setContentsMargins(10, 0, 8, 0);
    titleLayout->addWidget(title);
    tabBar()->setTabButton(index, QTabBar::LeftSide, titleWrap);

    auto *close = new QToolButton(this);
    close->setObjectName(QStringLiteral("tabCloseButton"));
    close->setText(QStringLiteral("\u2715"));
    close->setToolTip(QStringLiteral("Close Tab"));
    close->setFocusPolicy(Qt::NoFocus);
    close->setFixedSize(18, 18);
    entry.closeButton = close;

    // The wrapper keeps its width while the button is hidden, so hovering
    // a tab does not resize it. The right margin is the room past the button.
    auto *wrapper = new QWidget(this);
    wrapper->setFixedWidth(26);
    auto *layout = new QHBoxLayout(wrapper);
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 8, 0);
    layout->addWidget(close);
    tabBar()->setTabButton(index, QTabBar::RightSide, wrapper);

    connect(close, &QToolButton::clicked, this, [this, wrapper] {
        for (int i = 0; i < count(); ++i) {
            if (tabBar()->tabButton(i, QTabBar::RightSide) == wrapper) {
                emit tabCloseRequested(i);
                return;
            }
        }
    });

    refreshTabTitle(page);
    updateCloseButtons();
}

void TabWorkspace::writePage(QWidget *page)
{
    const DocumentFile &file = m_pages[page].file;
    if (auto *editor = qobject_cast<CodeEditor *>(page))
        file.save(editor->toPlainText());
    else
        file.saveBytes(static_cast<HexView *>(page)->data());
    setPageModified(page, false);
}

bool TabWorkspace::saveCurrent()
{
    QWidget *page = currentWidget();
    if (m_pages[page].file.path().isEmpty())
        return saveCurrentAs(m_saveDirectory);
    writePage(page);
    const QString saved = QFileInfo(m_pages[page].file.path()).absolutePath();
    m_saveDirectory = saved;
    emit fileSaved(saved);
    return true;
}

bool TabWorkspace::saveCurrentAs(const QString &directory)
{
    QWidget *page = currentWidget();
    const QString existing = m_pages[page].file.path();
    QString suggested = existing;
    if (!directory.isEmpty()) {
        const QString name = existing.isEmpty() ? m_pages[page].title : QFileInfo(existing).fileName();
        suggested = QDir(directory).filePath(name);
    }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save As"), suggested);
    if (path.isEmpty())
        return false;

    Page &entry = m_pages[page];
    entry.file.setPath(path);
    entry.title = entry.file.fileName();
    writePage(page);
    if (auto *editor = qobject_cast<CodeEditor *>(page))
        editor->setLanguage(m_registry->definitionForPath(path));

    setTabToolTip(indexOf(page), path);
    refreshTabTitle(page);
    const QString saved = QFileInfo(path).absolutePath();
    m_saveDirectory = saved;
    emit fileSaved(saved);
    emit statusChanged();
    return true;
}

bool TabWorkspace::closeTab(int index)
{
    QWidget *page = widget(index);
    if (isPageModified(page)) {
        setCurrentIndex(index);
        const auto choice = QMessageBox::question(
            this, QCoreApplication::applicationName(),
            QStringLiteral("Save changes to %1?").arg(m_pages[page].title),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (choice == QMessageBox::Cancel)
            return false;
        if (choice == QMessageBox::Save && !saveCurrent())
            return false;
    }
    removeTab(index);
    m_pages.remove(page);
    page->deleteLater();
    orderPinnedTabs();
    return true;
}

void TabWorkspace::setCurrentHexMode(bool hex)
{
    QWidget *page = currentWidget();
    const int index = currentIndex();
    const bool modified = isPageModified(page);
    Page entry = m_pages.take(page);

    QWidget *replacement = nullptr;
    if (hex) {
        auto *editor = static_cast<CodeEditor *>(page);
        const QByteArray bytes = !modified && !entry.file.path().isEmpty()
                                     ? entry.file.loadBytes()
                                     : entry.file.encode(editor->toPlainText());
        replacement = createHexView(bytes);
    } else {
        auto *view = static_cast<HexView *>(page);
        const QString text = entry.file.decode(view->data());
        replacement = createEditor(entry.file, text);
    }

    removeTab(index);
    page->deleteLater();
    insertPage(index, replacement, entry.file, entry.title);
    m_pages[replacement].pinned = entry.pinned;
    refreshTabTitle(replacement);
    orderPinnedTabs();
    setPageModified(replacement, modified);
}

Session TabWorkspace::captureSession() const
{
    Session session;
    session.activeIndex = currentIndex();
    for (int i = 0; i < documentCount(); ++i) {
        QWidget *page = widget(i);
        const Page &entry = *m_pages.constFind(page);
        SessionTab tab;
        tab.path = entry.file.path();
        tab.title = entry.title;
        tab.pinned = entry.pinned;
        tab.modified = isPageModified(page);
        if (auto *editor = qobject_cast<CodeEditor *>(page)) {
            tab.language = editor->language()->name;
            if (tab.modified)
                tab.text = editor->toPlainText();
            tab.cursorPosition = editor->textCursor().position();
        } else {
            auto *view = static_cast<HexView *>(page);
            tab.hex = true;
            if (tab.modified)
                tab.bytes = view->data();
            tab.cursorPosition = int(view->cursorOffset());
        }
        session.tabs.append(tab);
    }
    return session;
}

void TabWorkspace::restoreSession(const Session &session)
{
    QWidget *activePage = nullptr;
    int inserted = 0;
    for (const SessionTab &tab : session.tabs) {
        DocumentFile file(tab.path);
        if (tab.hex) {
            const QByteArray bytes = tab.modified || tab.path.isEmpty() ? tab.bytes
                                                                        : file.loadBytes();
            HexView *view = createHexView(bytes);
            insertPage(documentCount(), view, file, tab.title);
            m_pages[view].pinned = tab.pinned;
            view->setModified(tab.modified);
            view->setCursorOffset(tab.cursorPosition);
            if (inserted == session.activeIndex)
                activePage = view;
        } else {
            // Loading establishes encoding and line endings even when the
            // disk text is superseded by the persisted unsaved buffer.
            const QString diskText = tab.path.isEmpty() ? QString() : file.load();
            CodeEditor *editor = addEditor(file, tab.modified ? tab.text : diskText, tab.title);
            if (!tab.language.isEmpty())
                editor->setLanguage(m_registry->definitionForName(tab.language));
            editor->document()->setModified(tab.modified);
            QTextCursor cursor = editor->textCursor();
            cursor.setPosition(qMin(tab.cursorPosition,
                                    int(editor->document()->characterCount()) - 1));
            editor->setTextCursor(cursor);
            m_pages[editor].pinned = tab.pinned;
            if (inserted == session.activeIndex)
                activePage = editor;
        }
        ++inserted;
        // Keep future "Untitled N" numbering ahead of restored titles.
        if (tab.path.isEmpty())
            m_untitledCount = qMax(m_untitledCount,
                                   tab.title.section(QLatin1Char(' '), -1).toInt());
    }
    orderPinnedTabs();
    if (activePage != nullptr)
        setCurrentWidget(activePage);
    // Languages were applied after each tab's insertion notified the status
    // bar, so re-announce the final state.
    emit statusChanged();
}

void TabWorkspace::setTheme(const Theme *theme)
{
    m_theme = theme;
    for (auto it = m_pages.keyBegin(); it != m_pages.keyEnd(); ++it) {
        if (auto *editor = qobject_cast<CodeEditor *>(*it))
            editor->setTheme(theme);
        else
            static_cast<HexView *>(*it)->setTheme(theme);
    }
}

void TabWorkspace::setTabColors(const QColor &active, const QColor &inactive,
                                const QColor &separator, const QColor &scrollButton)
{
    m_activeTitle = active;
    m_inactiveTitle = inactive;
    auto *bar = static_cast<DocumentTabBar *>(tabBar());
    bar->setSeparatorColor(separator);
    bar->setScrollButtonColor(scrollButton);
    for (auto it = m_pages.keyBegin(); it != m_pages.keyEnd(); ++it)
        refreshTabTitle(*it);
}

void TabWorkspace::setCurrentLanguage(const LanguageDefinition *definition)
{
    currentEditor()->setLanguage(definition);
    emit statusChanged();
}

void TabWorkspace::setBytesPerRow(int count)
{
    m_bytesPerRow = qBound(1, count, 64);
    for (auto it = m_pages.keyBegin(); it != m_pages.keyEnd(); ++it) {
        if (auto *view = qobject_cast<HexView *>(*it))
            view->setBytesPerRow(m_bytesPerRow);
    }
}

void TabWorkspace::setWordWrap(bool wrap)
{
    m_wordWrap = wrap;
    for (auto it = m_pages.keyBegin(); it != m_pages.keyEnd(); ++it) {
        if (auto *editor = qobject_cast<CodeEditor *>(*it))
            editor->setLineWrapMode(wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    }
}

void TabWorkspace::refreshTabTitle(QWidget *page)
{
    Page &entry = m_pages[page];
    if (entry.titleLabel == nullptr)
        return;
    QFont font = tabBar()->font();
    font.setItalic(isPageModified(page));
    entry.titleLabel->setFont(font);
    entry.titleLabel->setText(entry.pinned ? QStringLiteral("\u25C6 ") + entry.title : entry.title);
    const QColor color = page == currentWidget() ? m_activeTitle : m_inactiveTitle;
    if (color.isValid()) {
        entry.titleLabel->setStyleSheet(
            QStringLiteral("color: %1; background: transparent;").arg(color.name()));
    }
}

void TabWorkspace::updateCloseButtons()
{
    for (int i = 0; i < documentCount(); ++i) {
        QToolButton *close = m_pages.value(widget(i)).closeButton;
        if (close == nullptr)
            continue;
        close->setVisible(i == currentIndex() || i == m_hoveredTab);
    }
}

void TabWorkspace::showTabMenu(const QPoint &pos)
{
    const int index = tabBar()->tabAt(pos);
    if (index < 0 || widget(index) == m_newTabPage)
        return;
    const bool pinned = m_pages[widget(index)].pinned;
    QMenu menu(this);
    menu.addAction(pinned ? QStringLiteral("Unpin") : QStringLiteral("Pin"), this, [this, index, pinned] {
        setPinned(index, !pinned);
    });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Close"), this, [this, index] {
        if (closeTab(index) && documentCount() == 0)
            newDocument();
    });
    menu.addAction(QStringLiteral("Close Others"), this, [this, index] { closeUnpinned(index, true, false); });
    menu.addAction(QStringLiteral("Close to the Right"), this,
                   [this, index] { closeUnpinned(index, false, true); });
    menu.addAction(QStringLiteral("Close All"), this, [this] { closeUnpinned(-1, false, false); });
    menu.exec(tabBar()->mapToGlobal(pos));
}

void TabWorkspace::setPinned(int index, bool pinned)
{
    QWidget *page = widget(index);
    if (page == m_newTabPage || m_pages[page].pinned == pinned)
        return;
    m_pages[page].pinned = pinned;
    orderPinnedTabs();
}

void TabWorkspace::orderPinnedTabs()
{
    QList<QWidget *> ordered;
    int pins = 0;
    for (int i = 0; i < documentCount(); ++i) {
        QWidget *page = widget(i);
        if (m_pages[page].pinned) {
            ordered.insert(pins, page);
            ++pins;
        } else {
            ordered.append(page);
        }
    }
    for (int index = 0; index < ordered.size(); ++index) {
        const int current = indexOf(ordered.at(index));
        if (current != index)
            tabBar()->moveTab(current, index);
    }
    static_cast<DocumentTabBar *>(tabBar())->setPinnedCount(pins);
    for (QWidget *page : ordered)
        refreshTabTitle(page);
}

void TabWorkspace::closeUnpinned(int clicked, bool others, bool toTheRight)
{
    for (int i = documentCount() - 1; i >= 0; --i) {
        if (m_pages[widget(i)].pinned)
            continue;
        if (others && i == clicked)
            continue;
        if (toTheRight && i <= clicked)
            continue;
        if (!closeTab(i))
            return;
    }
    if (documentCount() == 0)
        newDocument();
}
