#include "app/FindBar.h"

#include "editor/CodeEditor.h"

#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int kResultRole = Qt::UserRole;

QString sampleAround(const QString &line, int column, int length)
{
    constexpr int radius = 40;
    const int start = qMax(0, column - radius);
    const int end = qMin(line.size(), column + length + radius);
    QString text = line.mid(start, end - start).simplified();
    if (start > 0)
        text.prepend(QStringLiteral("\u2026"));
    if (end < line.size())
        text.append(QStringLiteral("\u2026"));
    return text;
}

} // namespace

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
    auto *findAllButton = new QPushButton(QStringLiteral("Find All"), this);
    auto *replaceButton = new QPushButton(QStringLiteral("Replace"), this);
    auto *replaceAllButton = new QPushButton(QStringLiteral("Replace All"), this);
    auto *closeButton = new QToolButton(this);
    closeButton->setText(QStringLiteral("\u2715"));
    closeButton->setAutoRaise(true);

    auto *row = new QHBoxLayout;
    row->setContentsMargins(6, 3, 6, 3);
    row->addWidget(new QLabel(QStringLiteral("Find:"), this));
    row->addWidget(m_findEdit, 2);
    row->addWidget(previousButton);
    row->addWidget(nextButton);
    row->addWidget(findAllButton);
    row->addWidget(new QLabel(QStringLiteral("Replace:"), this));
    row->addWidget(m_replaceEdit, 2);
    row->addWidget(replaceButton);
    row->addWidget(replaceAllButton);
    row->addStretch();
    row->addWidget(closeButton);

    m_results = new QListWidget(this);
    m_results->setMaximumHeight(160);
    m_results->hide();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(row);
    layout->addWidget(m_results);

    connect(m_findEdit, &QLineEdit::textChanged, this, &FindBar::applyTerm);
    connect(m_findEdit, &QLineEdit::returnPressed, this, &FindBar::findNext);
    connect(nextButton, &QPushButton::clicked, this, &FindBar::findNext);
    connect(previousButton, &QPushButton::clicked, this, &FindBar::findPrevious);
    connect(findAllButton, &QPushButton::clicked, this, &FindBar::findAll);
    connect(replaceButton, &QPushButton::clicked, this, &FindBar::replaceCurrent);
    connect(replaceAllButton, &QPushButton::clicked, this, &FindBar::replaceAll);
    connect(closeButton, &QToolButton::clicked, this, &FindBar::closeBar);
    connect(m_results, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        jumpToResult(item->data(kResultRole).toInt(), item->data(kResultRole + 1).toInt());
    });

    hide();
}

void FindBar::setEditor(CodeEditor *editor)
{
    if (m_editor != nullptr)
        m_editor->setFindTerm(QString());
    m_editor = editor;
    if (isVisible())
        applyTerm();
}

void FindBar::open()
{
    show();
    const QString selection = m_editor->textCursor().selectedText();
    if (!selection.isEmpty() && !selection.contains(QChar::ParagraphSeparator))
        m_findEdit->setText(selection);
    m_findEdit->setFocus();
    m_findEdit->selectAll();
    applyTerm();
}

void FindBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        closeBar();
        return;
    }
    QWidget::keyPressEvent(event);
}

void FindBar::hideEvent(QHideEvent *event)
{
    if (m_editor != nullptr)
        m_editor->setFindTerm(QString());
    m_results->hide();
    m_results->clear();
    QWidget::hideEvent(event);
}

void FindBar::closeBar()
{
    hide();
    if (m_editor != nullptr)
        m_editor->setFocus();
}

void FindBar::applyTerm()
{
    if (m_editor == nullptr)
        return;
    m_editor->setFindTerm(isVisible() ? m_findEdit->text() : QString());
    if (m_results->isVisible())
        findAll();
}

bool FindBar::find(QTextDocument::FindFlags flags)
{
    const QString term = m_findEdit->text();
    if (term.isEmpty() || m_editor == nullptr)
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
    if (m_editor == nullptr)
        return;
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
    if (term.isEmpty() || m_editor == nullptr)
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

void FindBar::findAll()
{
    m_results->clear();
    const QString term = m_findEdit->text();
    if (term.isEmpty() || m_editor == nullptr) {
        m_results->hide();
        return;
    }
    QTextCursor match = m_editor->document()->find(term, 0);
    while (!match.isNull()) {
        const QTextBlock block = match.block();
        const int column = match.selectionStart() - block.position();
        auto *item = new QListWidgetItem(
            QStringLiteral("Ln %1: %2")
                .arg(block.blockNumber() + 1)
                .arg(sampleAround(block.text(), column, match.selectionEnd() - match.selectionStart())));
        item->setData(kResultRole, match.selectionStart());
        item->setData(kResultRole + 1, match.selectionEnd());
        m_results->addItem(item);
        match = m_editor->document()->find(term, match);
    }
    m_results->setVisible(m_results->count() > 0);
}

void FindBar::jumpToResult(int start, int end)
{
    if (m_editor == nullptr)
        return;
    QTextCursor cursor = m_editor->textCursor();
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    m_editor->setTextCursor(cursor);
    m_editor->centerCursor();
    m_editor->setFocus();
}
