#include "app/FindBar.h"

#include "editor/CodeEditor.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextBlock>
#include <QToolButton>

FindBar::FindBar(QWidget *parent)
    : QWidget(parent)
{
    // Plain QWidget subclasses ignore stylesheet backgrounds without this.
    setAttribute(Qt::WA_StyledBackground, true);

    m_findEdit = new QLineEdit(this);
    m_findEdit->setPlaceholderText(QStringLiteral("Find"));
    m_replaceEdit = new QLineEdit(this);
    m_replaceEdit->setPlaceholderText(QStringLiteral("Replace with"));

    auto *previousButton = new QPushButton(QStringLiteral("Previous"), this);
    auto *nextButton = new QPushButton(QStringLiteral("Next"), this);
    auto *replaceButton = new QPushButton(QStringLiteral("Replace"), this);
    auto *replaceAllButton = new QPushButton(QStringLiteral("Replace All"), this);
    auto *closeButton = new QToolButton(this);
    closeButton->setText(QStringLiteral("\u2715"));
    closeButton->setAutoRaise(true);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 3, 6, 3);
    layout->addWidget(new QLabel(QStringLiteral("Find:"), this));
    layout->addWidget(m_findEdit, 2);
    layout->addWidget(previousButton);
    layout->addWidget(nextButton);
    layout->addWidget(new QLabel(QStringLiteral("Replace:"), this));
    layout->addWidget(m_replaceEdit, 2);
    layout->addWidget(replaceButton);
    layout->addWidget(replaceAllButton);
    layout->addStretch();
    layout->addWidget(closeButton);

    connect(m_findEdit, &QLineEdit::returnPressed, this, &FindBar::findNext);
    connect(nextButton, &QPushButton::clicked, this, &FindBar::findNext);
    connect(previousButton, &QPushButton::clicked, this, &FindBar::findPrevious);
    connect(replaceButton, &QPushButton::clicked, this, &FindBar::replaceCurrent);
    connect(replaceAllButton, &QPushButton::clicked, this, &FindBar::replaceAll);
    connect(closeButton, &QToolButton::clicked, this, &QWidget::hide);

    hide();
}

void FindBar::open()
{
    show();
    const QString selection = m_editor->textCursor().selectedText();
    if (!selection.isEmpty() && !selection.contains(QChar::ParagraphSeparator))
        m_findEdit->setText(selection);
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void FindBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        m_editor->setFocus();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool FindBar::find(QTextDocument::FindFlags flags)
{
    const QString term = m_findEdit->text();
    if (term.isEmpty())
        return false;
    if (m_editor->find(term, flags))
        return true;
    // Wrap around and retry from the appropriate end.
    QTextCursor cursor = m_editor->textCursor();
    cursor.movePosition(flags & QTextDocument::FindBackward ? QTextCursor::End
                                                            : QTextCursor::Start);
    m_editor->setTextCursor(cursor);
    return m_editor->find(term, flags);
}

void FindBar::findNext()
{
    find(QTextDocument::FindFlags());
}

void FindBar::findPrevious()
{
    find(QTextDocument::FindBackward);
}

void FindBar::replaceCurrent()
{
    QTextCursor cursor = m_editor->textCursor();
    if (cursor.hasSelection()
        && cursor.selectedText().compare(m_findEdit->text(), Qt::CaseInsensitive) == 0) {
        cursor.insertText(m_replaceEdit->text());
    }
    findNext();
}

void FindBar::replaceAll()
{
    const QString term = m_findEdit->text();
    if (term.isEmpty())
        return;
    QTextCursor cursor(m_editor->document());
    cursor.beginEditBlock();
    QTextCursor match = m_editor->document()->find(term, 0);
    while (!match.isNull()) {
        match.insertText(m_replaceEdit->text());
        match = m_editor->document()->find(term, match.position());
    }
    cursor.endEditBlock();
}
