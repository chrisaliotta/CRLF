#pragma once

#include <QTextDocument>
#include <QWidget>

class CodeEditor;
class QLineEdit;

// Inline find/replace strip shown below the tab area. Always targets the
// current editor; searches wrap around the document.
class FindBar : public QWidget
{
    Q_OBJECT

public:
    explicit FindBar(QWidget *parent = nullptr);

    void setEditor(CodeEditor *editor) { m_editor = editor; }
    void open();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    bool find(QTextDocument::FindFlags flags);
    void findNext();
    void findPrevious();
    void replaceCurrent();
    void replaceAll();

    CodeEditor *m_editor = nullptr;
    QLineEdit *m_findEdit = nullptr;
    QLineEdit *m_replaceEdit = nullptr;
};
