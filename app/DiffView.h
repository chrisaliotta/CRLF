#pragma once

#include "git/LineDiff.h"

#include <QList>
#include <QString>
#include <QWidget>

class CodeEditor;
class LanguageRegistry;
class QLabel;
class Theme;

// Two versions of one file. `left` and `right` are LF-normalized text.
struct DiffContent
{
    // The tab title, such as "main.cpp (Index ↔ Working Tree)".
    QString title;
    // Absolute path of the file. It picks the language and is opened by
    // Open File.
    QString path;
    QString leftLabel;
    QString rightLabel;
    QString left;
    QString right;
};

// Side-by-side comparison in one tab. Each side is a read-only CodeEditor, so
// both are highlighted and numbered like any document. Blank padding rows
// keep unchanged lines level, which lets the two sides share one scroll
// position. Removed rows are tinted on the left, added rows on the right.
class DiffView : public QWidget
{
    Q_OBJECT

public:
    DiffView(const LanguageRegistry *registry, const Theme *theme, QWidget *parent = nullptr);

    void setTheme(const Theme *theme);
    // `changes` is diffLines(left, right), computed off the UI thread.
    void setContent(const DiffContent &content, const QList<LineChange> &changes);
    const DiffContent &content() const { return m_content; }

signals:
    void openRequested(const QString &path);

private:
    void step(int direction);

    const LanguageRegistry *m_registry;
    CodeEditor *m_left = nullptr;
    CodeEditor *m_right = nullptr;
    QLabel *m_leftLabel = nullptr;
    QLabel *m_rightLabel = nullptr;
    QLabel *m_summary = nullptr;
    DiffContent m_content;
    // The first aligned row of each change, for next and previous.
    QList<int> m_changeRows;
};
