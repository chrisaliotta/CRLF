#pragma once

#include "Types.h"
#include "io/DocumentFile.h"

#include <QHash>
#include <QTabWidget>

class CodeEditor;
class HexView;
class LanguageDefinition;
class LanguageRegistry;
class QToolButton;
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

    // Null when the current tab is the other page type.
    CodeEditor *currentEditor() const;
    HexView *currentHexView() const;
    const DocumentFile &fileFor(QWidget *page) const;
    QString titleFor(QWidget *page) const;

    void newDocument();
    void openFile(const QString &path);
    bool saveCurrent();
    bool saveCurrentAs();
    // Returns false when the user cancels the unsaved-changes prompt.
    bool closeTab(int index);

    // Swaps the current tab between the text editor and the hex editor.
    // Clean files reopen from their exact bytes; edited or untitled buffers
    // are encoded the way Save would write them.
    void setCurrentHexMode(bool hex);

    Session captureSession() const;
    void restoreSession(const Session &session);

    void setTheme(const Theme *theme);
    void setWordWrap(bool wrap);
    // Manual syntax override for the active text tab, e.g. styling an
    // unsaved buffer as Markdown before it has an extension to detect from.
    void setCurrentLanguage(const LanguageDefinition *definition);

signals:
    // Cursor moved, document swapped, or file identity changed; the status
    // bar and window title re-read everything on this signal.
    void statusChanged();

protected:
    void tabInserted(int index) override;
    void tabRemoved(int index) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    struct Page {
        DocumentFile file;
        QString title;
    };

    CodeEditor *createEditor(const DocumentFile &file, const QString &text);
    HexView *createHexView(const QByteArray &bytes);
    void insertPage(int index, QWidget *page, const DocumentFile &file, const QString &title);
    CodeEditor *addEditor(const DocumentFile &file, const QString &text, const QString &title);
    void writePage(QWidget *page);
    // Replaces the style-drawn close button so the spacing around it can be
    // controlled; Qt ignores stylesheet margins on QTabBar::close-button.
    void installCloseButton(int index);
    void refreshTabTitle(QWidget *page);
    // The + button floats directly after the last tab, like a browser's
    // new-tab button, and follows every tab insert/remove/move/retitle.
    void positionNewTabButton();

    const LanguageRegistry *m_registry;
    const Theme *m_theme;
    QToolButton *m_newTabButton = nullptr;
    QHash<QWidget *, Page> m_pages;
    bool m_wordWrap = false;
    int m_untitledCount = 0;
};
