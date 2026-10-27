#include "app/TabWorkspace.h"

#include "editor/CodeEditor.h"
#include "hex/HexView.h"
#include "syntax/LanguageRegistry.h"

#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
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
    setMovable(true);
    setDocumentMode(true);

    m_newTabButton = new QToolButton(this);
    m_newTabButton->setText(QStringLiteral("+"));
    m_newTabButton->setToolTip(QStringLiteral("New Document (Ctrl+N)"));
    QFont plusFont = m_newTabButton->font();
    plusFont.setPointSize(plusFont.pointSize() + 2);
    m_newTabButton->setFont(plusFont);
    m_newTabButton->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; background: transparent; }"
        "QToolButton:hover { background: rgba(128, 128, 128, 0.3); border-radius: 4px; }"));
    connect(m_newTabButton, &QToolButton::clicked, this, &TabWorkspace::newDocument);
    connect(tabBar(), &QTabBar::tabMoved, this, [this] { positionNewTabButton(); });

    connect(this, &QTabWidget::currentChanged, this, [this] { emit statusChanged(); });
}

void TabWorkspace::positionNewTabButton()
{
    const int side = tabBar()->height() - 4;
    m_newTabButton->setFixedSize(side, side);
    const QRect lastTab = tabBar()->tabRect(count() - 1);
    const int x = qMin(tabBar()->x() + lastTab.right() + 4,
                       width() - m_newTabButton->width() - 2);
    m_newTabButton->move(x, tabBar()->y() + (tabBar()->height() - side) / 2);
    m_newTabButton->raise();
}

void TabWorkspace::tabInserted(int index)
{
    QTabWidget::tabInserted(index);
    positionNewTabButton();
}

void TabWorkspace::tabRemoved(int index)
{
    QTabWidget::tabRemoved(index);
    positionNewTabButton();
}

void TabWorkspace::resizeEvent(QResizeEvent *event)
{
    QTabWidget::resizeEvent(event);
    positionNewTabButton();
}

void TabWorkspace::showEvent(QShowEvent *event)
{
    QTabWidget::showEvent(event);
    positionNewTabButton();
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
    DocumentFile file(path);
    const QString text = file.load();
    addEditor(file, text, file.fileName());
}

CodeEditor *TabWorkspace::createEditor(const DocumentFile &file, const QString &text)
{
    auto *editor = new CodeEditor(this);
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
    installCloseButton(index);
    setTabToolTip(index, file.path());
    setCurrentIndex(index);
    page->setFocus();
}

CodeEditor *TabWorkspace::addEditor(const DocumentFile &file, const QString &text,
                                    const QString &title)
{
    CodeEditor *editor = createEditor(file, text);
    insertPage(count(), editor, file, title);
    return editor;
}

void TabWorkspace::installCloseButton(int index)
{
    auto *close = new QToolButton(this);
    close->setObjectName(QStringLiteral("tabCloseButton"));
    close->setText(QStringLiteral("\u2715"));
    close->setToolTip(QStringLiteral("Close Tab"));
    close->setFocusPolicy(Qt::NoFocus);
    close->setFixedSize(18, 18);

    // The wrapper's right margin is the breathing room between the close
    // button and the tab's right edge.
    auto *wrapper = new QWidget(this);
    auto *layout = new QHBoxLayout(wrapper);
    layout->setContentsMargins(0, 0, 6, 0);
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
        return saveCurrentAs();
    writePage(page);
    return true;
}

bool TabWorkspace::saveCurrentAs()
{
    QWidget *page = currentWidget();
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save As"),
                                                      m_pages[page].file.path());
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
    setPageModified(replacement, modified);
}

Session TabWorkspace::captureSession() const
{
    Session session;
    session.activeIndex = currentIndex();
    for (int i = 0; i < count(); ++i) {
        QWidget *page = widget(i);
        const Page &entry = *m_pages.constFind(page);
        SessionTab tab;
        tab.path = entry.file.path();
        tab.title = entry.title;
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
    for (const SessionTab &tab : session.tabs) {
        DocumentFile file(tab.path);
        if (tab.hex) {
            const QByteArray bytes = tab.modified || tab.path.isEmpty() ? tab.bytes
                                                                        : file.loadBytes();
            HexView *view = createHexView(bytes);
            insertPage(count(), view, file, tab.title);
            view->setModified(tab.modified);
            view->setCursorOffset(tab.cursorPosition);
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
        }
        // Keep future "Untitled N" numbering ahead of restored titles.
        if (tab.path.isEmpty())
            m_untitledCount = qMax(m_untitledCount,
                                   tab.title.section(QLatin1Char(' '), -1).toInt());
    }
    setCurrentIndex(session.activeIndex);
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

void TabWorkspace::setCurrentLanguage(const LanguageDefinition *definition)
{
    currentEditor()->setLanguage(definition);
    emit statusChanged();
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
    const Page &entry = m_pages[page];
    const bool modified = isPageModified(page);
    setTabText(indexOf(page), modified ? entry.title + QStringLiteral(" \u2022") : entry.title);
    positionNewTabButton();
}
