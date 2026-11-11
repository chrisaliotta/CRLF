#include "app/TabWorkspace.h"

#include <QSharedPointer>

#include "app/ProcessesPanel.h"
#include "app/DocumentTabBar.h"
#include "app/ProcessOrchestrator.h"
#include "editor/CodeEditor.h"
#include "editor/ExternalLookup.h"
#include "editor/SymbolIndex.h"
#include "hex/HexView.h"
#include "syntax/LanguageRegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTextCursor>
#include <QTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
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

TabWorkspace::~TabWorkspace()
{
    m_externalGeneration->fetchAndAddOrdered(1);
}

TabWorkspace::TabWorkspace(const LanguageRegistry *registry, const Theme *theme, QWidget *parent)
    : QTabWidget(parent)
    , m_registry(registry)
    , m_symbols(new SymbolIndex(registry, this))
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
    m_fileWatcher = new QFileSystemWatcher(this);
    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        // Replacing a file drops it from the watch. Add it back once it exists.
        if (QFileInfo::exists(path))
            m_fileWatcher->addPath(path);
        QTimer::singleShot(100, this, [this, path] {
            if (QFileInfo::exists(path))
                m_fileWatcher->addPath(normalizedPath(path));
            reloadExternal(path);
        });
    });

    connect(bar, &QTabBar::tabMoved, this, [this](int, int) {
        if (m_adjustingTabs)
            return;
        QList<QWidget *> tools;
        for (QWidget *page : {static_cast<QWidget *>(m_processes), static_cast<QWidget *>(m_diff)}) {
            if (page != nullptr)
                tools.append(page);
        }
        std::sort(tools.begin(), tools.end(), [this](QWidget *a, QWidget *b) { return indexOf(a) < indexOf(b); });
        m_adjustingTabs = true;
        int want = count() - 1 - int(tools.size());
        for (QWidget *page : std::as_const(tools)) {
            const int have = indexOf(page);
            if (have != want)
                tabBar()->moveTab(have, want);
            ++want;
        }
        m_adjustingTabs = false;
    });

    connect(this, &QTabWidget::currentChanged, this, [this](int index) {
        // Wheel or keyboard navigation can still select +; step back onto
        // the last document. Clicks never select it (see DocumentTabBar).
        if (widget(index) == m_newTabPage && documentCount() > 0) {
            setCurrentIndex(index - 1);
            return;
        }
        emit processesCurrent(widget(index) == m_processes);
        for (auto it = m_pages.keyBegin(); it != m_pages.keyEnd(); ++it)
            refreshTabTitle(*it);
        updateCloseButtons();
        emit statusChanged();
    });
}

int TabWorkspace::documentCount() const
{
    return m_pages.size();
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
    const auto it = m_pages.constFind(page);
    if (it == m_pages.cend()) {
        static const DocumentFile empty;
        return empty;
    }
    return it->file;
}

QString TabWorkspace::titleFor(QWidget *page) const
{
    if (page == m_processes)
        return QStringLiteral("Processes");
    if (page == m_diff)
        return m_diff->content().title;
    const auto it = m_pages.constFind(page);
    return it == m_pages.cend() ? QString() : it->title;
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

    const QString title = QFileInfo(absolutePath).fileName();
    QPointer<TabWorkspace> alive(this);
    m_orchestrator->submit(QStringLiteral("open:%1").arg(absolutePath), QStringLiteral("Open file"), title, 0,
                           absolutePath, [alive, absolutePath] {
        DocumentFile file(absolutePath);
        const QString text = file.load();
        const QString title = file.fileName();
        QCoreApplication *app = QCoreApplication::instance();
        if (app == nullptr)
            return;
        QMetaObject::invokeMethod(app, [alive, file, text, title, absolutePath] {
            if (alive.isNull())
                return;
            for (auto it = alive->m_pages.cbegin(); it != alive->m_pages.cend(); ++it) {
                if (normalizedPath(it->file.path()).compare(absolutePath, Qt::CaseInsensitive) == 0) {
                    alive->setCurrentWidget(it.key());
                    it.key()->setFocus();
                    return;
                }
            }
            alive->addEditor(file, text, title);
        }, Qt::QueuedConnection);
    });
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
        unwatchFile(path);
        watchFile(newPath);
        it->title = it->file.fileName();
        if (auto *editor = qobject_cast<CodeEditor *>(it.key())) {
            editor->setLanguage(m_registry->definitionForPath(newPath));
            editor->setFilePath(newPath);
            applyIndent(editor, newPath);
        }
        setTabToolTip(indexOf(it.key()), newPath);
        refreshTabTitle(it.key());
    }
    emit statusChanged();
}

void TabWorkspace::setOrchestrator(ProcessOrchestrator *orchestrator)
{
    m_orchestrator = orchestrator;
    m_symbols->setOrchestrator(orchestrator);
    for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
        if (auto *editor = qobject_cast<CodeEditor *>(it.key()))
            editor->setOrchestrator(orchestrator);
    }
}

void TabWorkspace::showProcesses()
{
    if (m_processes != nullptr) {
        setCurrentWidget(m_processes);
        return;
    }
    m_processes = new ProcessesPanel(this);
    connect(m_processes, &ProcessesPanel::cancelRequested, this, &TabWorkspace::processCancelRequested);
    connect(m_processes, &ProcessesPanel::reloadRequested, this, &TabWorkspace::processReloadRequested);
    connect(m_processes, &ProcessesPanel::restartServicesRequested, this,
            &TabWorkspace::processServicesRestartRequested);
    insertToolPage(m_processes, QStringLiteral("Processes"));
}

void TabWorkspace::showDiff(const DiffContent &content, const QList<LineChange> &changes)
{
    if (m_diff == nullptr) {
        m_diff = new DiffView(m_registry, m_theme, this);
        connect(m_diff, &DiffView::openRequested, this, &TabWorkspace::openFile);
        m_diff->setContent(content, changes);
        insertToolPage(m_diff, content.title);
    } else {
        m_diff->setContent(content, changes);
        setTabText(indexOf(m_diff), content.title);
        setCurrentWidget(m_diff);
    }
    setTabToolTip(indexOf(m_diff), content.path);
    emit statusChanged();
}

void TabWorkspace::insertToolPage(QWidget *page, const QString &title)
{
    const int index = count() - 1;
    insertTab(index, page, title);
    auto *close = new QToolButton(this);
    close->setObjectName(QStringLiteral("tabCloseButton"));
    close->setText(QStringLiteral("\u2715"));
    close->setToolTip(QStringLiteral("Close Tab"));
    close->setFocusPolicy(Qt::NoFocus);
    close->setFixedSize(18, 18);
    connect(close, &QToolButton::clicked, this, [this, page] { closeTab(indexOf(page)); });
    tabBar()->setTabButton(index, QTabBar::RightSide, close);
    setCurrentWidget(page);
}

void TabWorkspace::closeProcesses()
{
    if (m_processes == nullptr)
        return;
    const int index = indexOf(m_processes);
    if (index >= 0)
        closeTab(index);
}

CodeEditor *TabWorkspace::createEditor(const DocumentFile &file, const QString &text, const QString &title,
                                        const LanguageDefinition *language)
{
    auto *editor = new CodeEditor(m_registry, this);
    editor->setOrchestrator(m_orchestrator);
    const QString owner = title.isEmpty() ? file.fileName()
                                           : file.path().isEmpty() ? title
                                                                   : title + QStringLiteral(" — ") + file.path();
    editor->setWorkOwner(owner.isEmpty() ? QStringLiteral("Untitled") : owner);
    editor->setTheme(m_theme);
    editor->setLanguage(language != nullptr ? language : m_registry->definitionForPath(file.path()));
    editor->setSymbolIndex(m_symbols);
    editor->setFilePath(file.path());
    applyIndent(editor, file.path());
    connect(editor, &CodeEditor::definitionActivated, this, &TabWorkspace::revealDefinition);
    connect(editor, &CodeEditor::externalActivated, this,
            [this, editor](const SymbolLocation &target) { revealExternal(editor, target); });
    editor->setLineWrapMode(m_wordWrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    editor->setLineNumbersVisible(m_lineNumbers);
    editor->replaceText(text);
    editor->setSyntaxEnabled(m_syntaxEnabled);
    editor->setCompletionEnabled(m_completion);

    connect(editor, &CodeEditor::syntaxRequested, this, [this, editor] { emit syntaxRequested(editor); });
    connect(editor, &CodeEditor::completionRequested, this,
            [this, editor](const QString &trigger) { emit completionRequested(editor, trigger); });
    connect(editor, &CodeEditor::documentationRequested, this,
            [this, editor](const CompletionItem &item) { emit documentationRequested(editor, item); });
    connect(editor, &CodeEditor::signatureRequested, this, [this, editor] { emit signatureRequested(editor); });
    connect(editor, &CodeEditor::filePathChanged, this, [this, editor] { emit editorRetargeted(editor); });
    connect(editor->document(), &QTextDocument::modificationChanged, this,
            [this, editor] { refreshTabTitle(editor); });
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, [this, editor] {
        if (currentWidget() == editor)
            emit statusChanged();
    });
    emit editorOpened(editor);
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

void TabWorkspace::setSymbolProject(const QString &root, const QStringList &hide)
{
    m_symbolRoot = root;
    m_symbols->setProject(root, hide);
}

void TabWorkspace::setExternalNavigation(bool enabled)
{
    m_symbols->setExternalEnabled(enabled);
}

QList<CodeEditor *> TabWorkspace::editors() const
{
    QList<CodeEditor *> found;
    for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
        if (auto *editor = qobject_cast<CodeEditor *>(it.key()))
            found.append(editor);
    }
    return found;
}

void TabWorkspace::setSyntaxEnabled(bool enabled)
{
    m_syntaxEnabled = enabled;
    for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
        if (auto *editor = qobject_cast<CodeEditor *>(it.key()))
            editor->setSyntaxEnabled(enabled);
    }
}

void TabWorkspace::setCompletionEnabled(bool enabled)
{
    m_completion = enabled;
    for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
        if (auto *editor = qobject_cast<CodeEditor *>(it.key()))
            editor->setCompletionEnabled(enabled);
    }
}

CodeEditor *TabWorkspace::editorFor(quintptr editorId) const
{
    for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
        if (quintptr(it.key()) == editorId)
            return qobject_cast<CodeEditor *>(it.key());
    }
    return nullptr;
}

void TabWorkspace::goToDiagnostic(const QString &path, quintptr editorId, int line, int column)
{
    CodeEditor *editor = editorFor(editorId);
    if (!editor && !path.isEmpty()) {
        for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
            if (normalizedPath(it->file.path()).compare(normalizedPath(path), Qt::CaseInsensitive) == 0) {
                editor = qobject_cast<CodeEditor *>(it.key());
                break;
            }
        }
    }
    if (!editor)
        return;
    setCurrentWidget(editor);
    editor->goTo(line, column);
    editor->setFocus();
}

void TabWorkspace::revealExternal(CodeEditor *editor, const SymbolLocation &target)
{
    const int generation = m_externalGeneration->fetchAndAddOrdered(1) + 1;
    ExternalQuery query;
    query.target = target;
    query.root = m_symbolRoot;
    if (target.kind == ExternalKind::Stylesheet)
        query.text = editor->toPlainText();
    query.cancelled = [latest = m_externalGeneration, generation] { return latest->loadAcquire() != generation; };

    const LanguageRegistry *registry = m_registry;
    QPointer<TabWorkspace> alive(this);
    const Page &entry = m_pages[editor];
    const QString owner = entry.file.path().isEmpty()
                              ? entry.title
                              : entry.title + QStringLiteral(" — ") + entry.file.path();
    m_orchestrator->submit(QStringLiteral("external"), QStringLiteral("External lookup"), owner, 0,
                           query.target.path,
                           [alive, query, registry, generation] {
        const SymbolLocation found = findExternal(query, registry);
        QCoreApplication *app = QCoreApplication::instance();
        if (found.line == 0 || app == nullptr)
            return;
        QMetaObject::invokeMethod(app, [alive, found, generation] {
            if (alive.isNull() || alive->m_externalGeneration->loadAcquire() != generation)
                return;
            alive->revealDefinition(found.path, 0, found.line, found.column);
        }, Qt::QueuedConnection);
    });
}

void TabWorkspace::setSymbolNavigation(bool enabled)
{
    const bool was = m_symbols->isEnabled();
    m_symbols->setEnabled(enabled);
    if (!enabled || was)
        return;
    for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
        if (auto *editor = qobject_cast<CodeEditor *>(it.key()))
            editor->refreshSymbols();
    }
}

void TabWorkspace::revealDefinition(const QString &path, quintptr editorId, int line, int column)
{
    const QString here = normalizedPath(fileFor(currentWidget()).path());
    const auto shown = [this, here](const QString &target) {
        const QString absolute = normalizedPath(target);
        if (absolute.isEmpty() || absolute.compare(here, Qt::CaseInsensitive) == 0)
            return;
        emit fileRevealed(absolute);
    };
    const auto place = [line, column](QWidget *page) {
        if (auto *editor = qobject_cast<CodeEditor *>(page))
            editor->goTo(line, column);
        page->setFocus();
    };
    if (editorId != 0) {
        for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
            if (quintptr(it.key()) == editorId) {
                setCurrentWidget(it.key());
                place(it.key());
                shown(it->file.path());
                return;
            }
        }
    }

    const QString absolute = normalizedPath(path);
    if (absolute.isEmpty())
        return;
    for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
        if (normalizedPath(it->file.path()).compare(absolute, Qt::CaseInsensitive) == 0) {
            setCurrentWidget(it.key());
            place(it.key());
            shown(absolute);
            return;
        }
    }

    QPointer<TabWorkspace> alive(this);
    m_orchestrator->submit(QStringLiteral("open:%1").arg(absolute), QStringLiteral("Open definition"),
                           QFileInfo(absolute).fileName(), 0, absolute, [alive, absolute, line, column, shown] {
        DocumentFile file(absolute);
        const QString text = file.load();
        const QString title = file.fileName();
        QCoreApplication *app = QCoreApplication::instance();
        if (app == nullptr)
            return;
        QMetaObject::invokeMethod(app, [alive, file, text, title, absolute, line, column, shown] {
            if (alive.isNull())
                return;
            for (auto it = alive->m_pages.cbegin(); it != alive->m_pages.cend(); ++it) {
                if (normalizedPath(it->file.path()).compare(absolute, Qt::CaseInsensitive) != 0)
                    continue;
                alive->setCurrentWidget(it.key());
                if (auto *editor = qobject_cast<CodeEditor *>(it.key()))
                    editor->goTo(line, column);
                it.key()->setFocus();
                shown(absolute);
                return;
            }
            CodeEditor *editor = alive->addEditor(file, text, title);
            editor->goTo(line, column);
            shown(absolute);
        }, Qt::QueuedConnection);
    });
}

void TabWorkspace::setIndent(const IndentSettings &indent)
{
    m_indent = indent;
    for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
        if (auto *editor = qobject_cast<CodeEditor *>(it.key()))
            applyIndent(editor, it->file.path());
    }
}

void TabWorkspace::applyIndent(CodeEditor *editor, const QString &path)
{
    const QString name = path.isEmpty() ? QString() : QFileInfo(path).fileName();
    const IndentRule rule = indentForFileName(m_indent, name);
    editor->setIndent(rule.spaces, rule.size, rule.keep);
}

void TabWorkspace::watchFile(const QString &path)
{
    if (!path.isEmpty())
        m_fileWatcher->addPath(normalizedPath(path));
}

void TabWorkspace::unwatchFile(const QString &path)
{
    if (path.isEmpty())
        return;
    const QString clean = normalizedPath(path);
    for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
        if (normalizedPath(it->file.path()).compare(clean, Qt::CaseInsensitive) == 0)
            return;
    }
    m_fileWatcher->removePath(clean);
}

void TabWorkspace::forgetCommandPage(QWidget *page)
{
    for (auto it = m_commandEditors.begin(); it != m_commandEditors.end();) {
        if (it.value() == page)
            it = m_commandEditors.erase(it);
        else
            ++it;
    }
}

void TabWorkspace::rememberCommandEditor(const QString &name)
{
    CodeEditor *editor = currentEditor();
    if (editor)
        m_commandEditors.insert(name, editor);
    else
        m_commandEditors.remove(name);
}

void TabWorkspace::applyCommandOutput(const QString &name, const QString &text)
{
    QWidget *page = m_commandEditors.value(name);
    if (!m_pages.contains(page))
        return;
    auto *editor = qobject_cast<CodeEditor *>(page);
    if (!editor)
        return;
    editor->applyCommandOutput(text);
    if (currentWidget() == page)
        emit statusChanged();
}

bool TabWorkspace::acceptExternalReload(QWidget *page, const QString &path)
{
    const QDateTime modified = QFileInfo(path).lastModified();
    if (m_declinedReload.contains(path) && m_declinedReload.value(path) == modified)
        return false;

    setCurrentIndex(indexOf(page));
    m_reloadPrompt.insert(path);
    const auto choice = QMessageBox::question(
        this, QCoreApplication::applicationName(),
        QStringLiteral("%1 has changed on disk. Reload the file and discard your unsaved edits?")
            .arg(m_pages[page].title),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    m_reloadPrompt.remove(path);
    if (choice != QMessageBox::Yes) {
        m_declinedReload.insert(path, QFileInfo(path).lastModified());
        return false;
    }
    m_declinedReload.remove(path);
    return true;
}

void TabWorkspace::reloadExternal(const QString &path)
{
    const QString clean = normalizedPath(path);
    if (m_reloadPrompt.contains(clean))
        return;

    QWidget *page = nullptr;
    DocumentFile file;
    QString title;
    for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
        if (normalizedPath(it->file.path()).compare(clean, Qt::CaseInsensitive) != 0)
            continue;
        page = it.key();
        file = it->file;
        title = it->title;
        break;
    }
    if (page == nullptr)
        return;

    const bool hex = qobject_cast<HexView *>(page) != nullptr;
    QPointer<TabWorkspace> alive(this);
    QPointer<QWidget> alivePage(page);
    m_orchestrator->submit(QStringLiteral("reload:%1").arg(clean), QStringLiteral("Reload"), title, 0, clean,
                           [alive, alivePage, file, clean, hex] {
        if (!QFileInfo::exists(clean))
            return;
        DocumentFile probe = file;
        QString text;
        QByteArray bytes;
        if (hex)
            bytes = probe.loadBytes();
        else
            text = probe.load();
        QCoreApplication *app = QCoreApplication::instance();
        if (app == nullptr)
            return;
        QMetaObject::invokeMethod(app, [alive, alivePage, probe, clean, hex, text, bytes] {
            if (alive.isNull() || alivePage.isNull())
                return;
            alive->applyExternalReload(alivePage, probe, clean, hex, text, bytes);
        }, Qt::QueuedConnection);
    });
}

void TabWorkspace::applyExternalReload(QWidget *page, const DocumentFile &loaded, const QString &clean, bool hex,
                                       const QString &text, const QByteArray &bytes)
{
    if (!m_pages.contains(page))
        return;
    if (normalizedPath(m_pages[page].file.path()).compare(clean, Qt::CaseInsensitive) != 0)
        return;
    m_fileWatcher->addPath(clean);
    if (m_reloadPrompt.contains(clean))
        return;

    if (hex) {
        auto *view = static_cast<HexView *>(page);
        if (bytes == view->data())
            return;
        if (isPageModified(page) && !acceptExternalReload(page, clean))
            return;
        m_pages[page].file = loaded;
        const qint64 position = view->cursorOffset();
        view->setData(bytes);
        view->setCursorOffset(qMin(position, qint64(bytes.size())));
    } else {
        auto *editor = static_cast<CodeEditor *>(page);
        if (text == editor->toPlainText())
            return;
        if (isPageModified(page) && !acceptExternalReload(page, clean))
            return;
        m_pages[page].file = loaded;
        const int position = editor->textCursor().position();
        editor->replaceText(text);
        QTextCursor cursor = editor->textCursor();
        const int end = qMax(0, int(editor->document()->characterCount()) - 1);
        cursor.setPosition(qMin(position, end));
        editor->setTextCursor(cursor);
    }
    refreshTabTitle(page);
    if (currentWidget() == page)
        emit statusChanged();
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
    watchFile(file.path());
    setCurrentIndex(index);
    page->setFocus();
    for (auto it = m_pages.keyBegin(); it != m_pages.keyEnd(); ++it)
        refreshTabTitle(*it);
    updateCloseButtons();
}

CodeEditor *TabWorkspace::addEditor(const DocumentFile &file, const QString &text,
                                    const QString &title, const LanguageDefinition *language)
{
    CodeEditor *editor = createEditor(file, text, title, language);
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
    if (isToolPage(page))
        return true;
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
    if (isToolPage(page) || page == m_newTabPage)
        return false;
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
    const QString previous = entry.file.path();
    entry.file.setPath(path);
    unwatchFile(previous);
    watchFile(path);
    entry.title = entry.file.fileName();
    writePage(page);
    if (auto *editor = qobject_cast<CodeEditor *>(page)) {
        editor->setLanguage(m_registry->definitionForPath(path));
        editor->setFilePath(path);
        applyIndent(editor, path);
    }

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
    if (page == m_processes) {
        removeTab(index);
        m_processes->deleteLater();
        m_processes = nullptr;
        emit processesCurrent(false);
        return true;
    }
    if (page == m_diff) {
        removeTab(index);
        m_diff->deleteLater();
        m_diff = nullptr;
        return true;
    }
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
    const QString closed = m_pages[page].file.path();
    if (auto *editor = qobject_cast<CodeEditor *>(page)) {
        editor->setSyntaxEnabled(false);
        emit editorClosed(quintptr(editor));
    }
    removeTab(index);
    m_pages.remove(page);
    forgetCommandPage(page);
    if (m_pages.isEmpty())
        m_untitledCount = 0;
    unwatchFile(closed);
    page->deleteLater();
    orderPinnedTabs();
    return true;
}

void TabWorkspace::setCurrentHexMode(bool hex)
{
    QWidget *page = currentWidget();
    if (isToolPage(page) || page == m_newTabPage)
        return;
    const int index = currentIndex();
    const bool modified = isPageModified(page);
    Page entry = m_pages.take(page);
    forgetCommandPage(page);

    QWidget *replacement = nullptr;
    if (hex) {
        auto *editor = static_cast<CodeEditor *>(page);
        editor->setSyntaxEnabled(false);
        emit editorClosed(quintptr(editor));
        const QByteArray bytes = !modified && !entry.file.path().isEmpty()
                                     ? entry.file.loadBytes()
                                     : entry.file.encode(editor->toPlainText());
        replacement = createHexView(bytes);
    } else {
        auto *view = static_cast<HexView *>(page);
        const QString text = entry.file.decode(view->data());
        replacement = createEditor(entry.file, text, entry.title);
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
    if (session.tabs.isEmpty()) {
        emit sessionRestored();
        return;
    }

    struct Batch {
        int remaining = 0;
        int activeIndex = 0;
        QList<SessionTab> tabs;
        QList<DocumentFile> files;
        QStringList texts;
        QList<QByteArray> bytes;
    };
    auto batch = QSharedPointer<Batch>::create();
    batch->activeIndex = session.activeIndex;
    batch->remaining = session.tabs.size();
    batch->tabs = session.tabs;
    for (int i = 0; i < session.tabs.size(); ++i) {
        batch->files.append(DocumentFile());
        batch->texts.append(QString());
        batch->bytes.append(QByteArray());
    }

    for (int i = 0; i < session.tabs.size(); ++i) {
        const SessionTab tab = session.tabs.at(i);
        const QString owner = tab.title.isEmpty() ? tab.path : tab.path.isEmpty()
                                                                      ? tab.title
                                                                      : tab.title + QStringLiteral(" — ") + tab.path;
        QPointer<TabWorkspace> alive(this);
        m_orchestrator->submit(QStringLiteral("session:%1").arg(i), QStringLiteral("Restore"), owner, 0,
                               tab.path.isEmpty() ? tab.title : tab.path, [alive, batch, i, tab] {
            DocumentFile file(tab.path);
            QString text;
            QByteArray bytes;
            if (tab.hex) {
                bytes = tab.modified || tab.path.isEmpty() ? tab.bytes : file.loadBytes();
            } else if (tab.path.isEmpty()) {
                text = tab.text;
            } else {
                // load() records encoding and line endings. A modified tab
                // keeps the persisted buffer instead of the disk text.
                const QString disk = file.load();
                text = tab.modified ? tab.text : disk;
            }
            QCoreApplication *app = QCoreApplication::instance();
            if (app == nullptr)
                return;
            QMetaObject::invokeMethod(app, [alive, batch, i, file, text, bytes] {
                if (alive.isNull())
                    return;
                batch->files[i] = file;
                batch->texts[i] = text;
                batch->bytes[i] = bytes;
                if (--batch->remaining > 0)
                    return;
                alive->applyRestoredSession(batch->tabs, batch->files, batch->texts, batch->bytes,
                                            batch->activeIndex);
            }, Qt::QueuedConnection);
        });
    }
}

void TabWorkspace::applyRestoredSession(const QList<SessionTab> &tabs, const QList<DocumentFile> &files,
                                        const QStringList &texts, const QList<QByteArray> &bytes,
                                        int activeIndex)
{
    QWidget *activePage = nullptr;
    // Inserting each page would otherwise reload project commands and
    // refresh the status bar once per tab.
    blockSignals(true);
    for (int i = 0; i < tabs.size(); ++i) {
        const SessionTab &tab = tabs.at(i);
        const DocumentFile &file = files.at(i);
        if (tab.hex) {
            HexView *view = createHexView(bytes.at(i));
            insertPage(documentCount(), view, file, tab.title);
            m_pages[view].pinned = tab.pinned;
            view->setModified(tab.modified);
            view->setCursorOffset(tab.cursorPosition);
            if (i == activeIndex)
                activePage = view;
        } else {
            const LanguageDefinition *language = tab.language.isEmpty()
                                                     ? nullptr
                                                     : m_registry->definitionForName(tab.language);
            CodeEditor *editor = addEditor(file, texts.at(i), tab.title, language);
            editor->document()->setModified(tab.modified);
            QTextCursor cursor = editor->textCursor();
            cursor.setPosition(qMin(tab.cursorPosition, int(editor->document()->characterCount()) - 1));
            editor->setTextCursor(cursor);
            m_pages[editor].pinned = tab.pinned;
            if (i == activeIndex)
                activePage = editor;
        }
        if (tab.path.isEmpty())
            m_untitledCount = qMax(m_untitledCount, tab.title.section(QLatin1Char(' '), -1).toInt());
    }
    orderPinnedTabs();
    blockSignals(false);
    if (activePage != nullptr)
        setCurrentWidget(activePage);
    emit statusChanged();
    emit sessionRestored();
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
    if (m_diff != nullptr)
        m_diff->setTheme(theme);
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

void TabWorkspace::setLineNumbersVisible(bool visible)
{
    m_lineNumbers = visible;
    for (auto it = m_pages.keyBegin(); it != m_pages.keyEnd(); ++it) {
        if (auto *editor = qobject_cast<CodeEditor *>(*it))
            editor->setLineNumbersVisible(visible);
    }
}

void TabWorkspace::refreshTabTitle(QWidget *page)
{
    if (isToolPage(page) || !m_pages.contains(page))
        return;
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
    if (auto *editor = qobject_cast<CodeEditor *>(page))
        noteEditorOwner(editor);
}

void TabWorkspace::noteEditorOwner(CodeEditor *editor)
{
    const Page &entry = m_pages[editor];
    const QString path = entry.file.path();
    editor->setWorkOwner(path.isEmpty() ? entry.title : entry.title + QStringLiteral(" — ") + path);
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
    if (isToolPage(widget(index))) {
        QMenu menu(this);
        menu.addAction(QStringLiteral("Close"), this, [this, index] {
            if (closeTab(index) && documentCount() == 0)
                newDocument();
        });
        menu.exec(tabBar()->mapToGlobal(pos));
        return;
    }
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
