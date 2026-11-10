#pragma once

#include "Types.h"
#include "app/ProcessesPanel.h"
#include "editor/CompletionItem.h"
#include "editor/SymbolLocation.h"
#include "io/DocumentFile.h"
#include "io/ProjectCommands.h"

#include <QColor>
#include <QDateTime>
#include <QAtomicInt>
#include <QHash>
#include <QSet>
#include <QSharedPointer>
#include <QTabWidget>

class ProcessOrchestrator;
class QFileSystemWatcher;

class CodeEditor;
class HexView;
class LanguageDefinition;
class LanguageRegistry;
class ProcessesPanel;
class QLabel;
class QToolButton;
class SymbolIndex;
class Theme;

// The tab strip and the documents behind it. Each tab page is either a
// CodeEditor (text) or a HexView (bytes) over one DocumentFile; the page
// widget itself is the key for everything else. Keeps tab titles, dirty
// markers, and editor settings in sync.
class TabWorkspace : public QTabWidget
{
    Q_OBJECT

public:
    TabWorkspace(const LanguageRegistry *registry, const Theme *theme, QWidget *parent = nullptr);
    // Stops an external lookup that is still running, so exit does not wait for it.
    ~TabWorkspace() override;

    // Null when the current tab is the other page type.
    CodeEditor *currentEditor() const;
    HexView *currentHexView() const;
    const DocumentFile &fileFor(QWidget *page) const;
    QString titleFor(QWidget *page) const;

    void newDocument();
    // Focuses the existing tab when the file is already open.
    void openFile(const QString &path);
    // Points tabs at a renamed/moved file, or at files inside a moved folder.
    void retargetPath(const QString &from, const QString &to);
    bool saveCurrent();
    // `directory` is where the dialog opens. An existing document keeps its
    // file name, but the dialog does not start in that file's old folder.
    bool saveCurrentAs(const QString &directory);
    // Returns false when the user cancels the unsaved-changes prompt.
    bool closeTab(int index);

    // Swaps the current tab between the text editor and the hex editor.
    // Clean files reopen from their exact bytes; edited or untitled buffers
    // are encoded the way Save would write them.
    void setCurrentHexMode(bool hex);

    Session captureSession() const;
    // Reads tab contents off the UI thread, then builds the tabs.
    // sessionRestored fires when the tabs exist.
    void restoreSession(const Session &session);

    void setTheme(const Theme *theme);
    // Active title, inactive title, the line drawn between tabs, and the
    // opaque fill behind the scroll arrows.
    void setTabColors(const QColor &active, const QColor &inactive, const QColor &separator,
                      const QColor &scrollButton);
    void setWordWrap(bool wrap);
    void setLineNumbersVisible(bool visible);
    void setBytesPerRow(int count);
    // Folder Save As opens in when a document has no path of its own yet,
    // and the folder close-prompts reuse.
    void setSaveDirectory(const QString &directory) { m_saveDirectory = directory; }
    int bytesPerRow() const { return m_bytesPerRow; }
    // Manual syntax override for the active text tab, e.g. styling an
    // unsaved buffer as Markdown before it has an extension to detect from.
    void setCurrentLanguage(const LanguageDefinition *definition);
    void setIndent(const IndentSettings &indent);
    // Folder whose source files are indexed for Ctrl+click. An empty root
    // keeps only the open buffers. `hide` is the .crlf name list.
    void setSymbolProject(const QString &root, const QStringList &hide);
    // `false` drops the index and stops further scans.
    void setSymbolNavigation(bool enabled);
    // `false` leaves package imports and unresolved stylesheet names unlinked.
    void setExternalNavigation(bool enabled);
    // `false` stops scheduling checks and clears spans in open editors.
    void setCheckingEnabled(bool enabled);
    void setCompletionEnabled(bool enabled);
    void setOrchestrator(ProcessOrchestrator *orchestrator);
    // Focuses the Processes tab, creating it when it is closed. Only one exists.
    void showProcesses();
    void closeProcesses();
    bool processesOpen() const { return m_processes != nullptr; }
    bool isProcesses(QWidget *page) const { return page != nullptr && page == m_processes; }
    ProcessesPanel *processes() const { return m_processes; }
    CodeEditor *editorFor(quintptr editorId) const;
    QList<CodeEditor *> editors() const;
    // Focuses the open document and moves its cursor. A path that is not open
    // is left alone; check rows belong to documents that are still open.
    void goToDiagnostic(const QString &path, quintptr editorId, int line, int column);
    // The editor a command's output will replace. Cleared in the hex view.
    void rememberCommandEditor(const QString &name);
    void applyCommandOutput(const QString &name, const QString &text);

    // Open documents; count() also includes the trailing + tab.
    int documentCount() const;

signals:
    // Cursor moved, document swapped, or file identity changed; the status
    // bar and window title re-read everything on this signal.
    void statusChanged();
    void sessionRestored();
    // A document was written; the argument is the folder it was written in.
    void fileSaved(const QString &directory);
    // A definition was opened in a file other than the one that was current.
    void fileRevealed(const QString &path);
    void checkRequested(CodeEditor *editor);
    void completionRequested(CodeEditor *editor, const QString &trigger);
    void signatureRequested(CodeEditor *editor);
    void documentationRequested(CodeEditor *editor, const CompletionItem &item);
    void editorClosed(quintptr editorId);
    void editorRetargeted(CodeEditor *editor);
    void processesCurrent(bool current);
    void processCancelRequested(const QString &key);
    void processReloadRequested(const QString &key);
    void processServicesRestartRequested();

private:
    struct Page {
        DocumentFile file;
        QString title;
        QLabel *titleLabel = nullptr;
        QToolButton *closeButton = nullptr;
        bool pinned = false;
    };

    CodeEditor *createEditor(const DocumentFile &file, const QString &text, const QString &title,
                             const LanguageDefinition *language = nullptr);
    void noteEditorOwner(CodeEditor *editor);
    void applyExternalReload(QWidget *page, const DocumentFile &loaded, const QString &clean, bool hex,
                             const QString &text, const QByteArray &bytes);
    void applyRestoredSession(const QList<SessionTab> &tabs, const QList<DocumentFile> &files,
                              const QStringList &texts, const QList<QByteArray> &bytes,
                              int activeIndex);
    HexView *createHexView(const QByteArray &bytes);
    void insertPage(int index, QWidget *page, const DocumentFile &file, const QString &title);
    void applyIndent(CodeEditor *editor, const QString &path);
    void watchFile(const QString &path);
    void unwatchFile(const QString &path);
    void forgetCommandPage(QWidget *page);
    // False when the user keeps unsaved edits.
    bool acceptExternalReload(QWidget *page, const QString &path);
    void reloadExternal(const QString &path);
    CodeEditor *addEditor(const DocumentFile &file, const QString &text, const QString &title,
                          const LanguageDefinition *language = nullptr);
    void writePage(QWidget *page);
    // Replaces the style-drawn close button so the spacing around it can be
    // controlled; Qt ignores stylesheet margins on QTabBar::close-button.
    void installTabChrome(QWidget *page);
    void refreshTabTitle(QWidget *page);
    void updateCloseButtons();
    void showTabMenu(const QPoint &pos);
    void setPinned(int index, bool pinned);
    // Pinned documents occupy the leading tabs. The + tab stays last.
    void orderPinnedTabs();
    // `others` keeps `clicked`. `toTheRight` keeps `clicked` and everything
    // before it. Pinned tabs stay open either way.
    void closeUnpinned(int clicked, bool others, bool toTheRight);
    void revealDefinition(const QString &path, quintptr editorId, int line, int column);
    // Looks the target up under node_modules on a worker. A newer click wins.
    void revealExternal(CodeEditor *editor, const SymbolLocation &target);

    const LanguageRegistry *m_registry;
    SymbolIndex *m_symbols = nullptr;
    ProcessOrchestrator *m_orchestrator = nullptr;
    ProcessesPanel *m_processes = nullptr;
    bool m_adjustingTabs = false;
    const Theme *m_theme;
    // Empty page behind the always-last + tab, which scrolls with the
    // documents like a browser's new-tab button.
    QWidget *m_newTabPage = nullptr;
    QHash<QWidget *, Page> m_pages;
    bool m_wordWrap = false;
    bool m_lineNumbers = true;
    QString m_saveDirectory;
    int m_bytesPerRow = 16;
    int m_untitledCount = 0;
    int m_hoveredTab = -1;
    QColor m_activeTitle;
    QColor m_inactiveTitle;
    QFileSystemWatcher *m_fileWatcher = nullptr;
    // Command name to the editor whose text was sent as that command's input.
    QHash<QString, QWidget *> m_commandEditors;
    // Paths with a reload question open, so a second disk event during the
    // dialog does not ask again.
    QSet<QString> m_reloadPrompt;
    // Disk time of a change the user chose not to load. A later write, with
    // a newer time, asks again.
    QHash<QString, QDateTime> m_declinedReload;
    IndentSettings m_indent;
    bool m_checking = false;
    bool m_completion = false;
    // node_modules is searched from here when the document has no path.
    QString m_symbolRoot;
    // Shared with the lookup worker, which stops once a newer click bumps it.
    QSharedPointer<QAtomicInt> m_externalGeneration = QSharedPointer<QAtomicInt>::create(0);
};
