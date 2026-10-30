#pragma once

#include <QTextDocument>
#include <QWidget>

class CodeEditor;
class QLineEdit;
class QListWidget;

// Inline find/replace strip shown above the tab area. Always targets the
// current editor; searches wrap around the document. Every match is
// highlighted, and Find All lists them so a click jumps to one.
class FindBar : public QWidget
{
    Q_OBJECT

public:
    explicit FindBar(QWidget *parent = nullptr);

    void setEditor(CodeEditor *editor);
    void open();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    bool find(QTextDocument::FindFlags flags);
    void findNext();
    void findPrevious();
    void replaceCurrent();
    void replaceAll();
    void applyTerm();
    void findAll();
    void jumpToResult(int start, int end);
    void closeBar();

    CodeEditor *m_editor = nullptr;
    QLineEdit *m_findEdit = nullptr;
    QLineEdit *m_replaceEdit = nullptr;
    QListWidget *m_results = nullptr;
};
