#include "app/DiffView.h"

#include "editor/CodeEditor.h"
#include "syntax/LanguageRegistry.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

// One side of the aligned view: its rows, the number drawn beside each row
// (0 on padding), and the marks that tint them.
struct Side
{
    QStringList rows;
    QList<int> numbers;
    QList<ChangeMark> marks;

    void add(const QStringList &lines, int index)
    {
        rows.append(lines.at(index));
        numbers.append(index + 1);
    }

    void pad(int count)
    {
        if (count <= 0)
            return;
        marks.append({int(rows.size()), count, ChangeKind::Padding});
        for (int i = 0; i < count; ++i) {
            rows.append(QString());
            numbers.append(0);
        }
    }
};

CodeEditor *makeSide(const LanguageRegistry *registry, const Theme *theme, QWidget *parent)
{
    auto *editor = new CodeEditor(registry, parent);
    editor->setTheme(theme);
    editor->setReadOnly(true);
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    return editor;
}

QLabel *makeLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setObjectName(QStringLiteral("diffLabel"));
    return label;
}

} // namespace

DiffView::DiffView(const LanguageRegistry *registry, const Theme *theme, QWidget *parent)
    : QWidget(parent)
    , m_registry(registry)
{
    setAttribute(Qt::WA_StyledBackground, true);
    m_left = makeSide(registry, theme, this);
    m_right = makeSide(registry, theme, this);
    m_leftLabel = makeLabel(this);
    m_rightLabel = makeLabel(this);
    m_summary = makeLabel(this);

    const auto button = [this](const QString &text, const QString &tip) {
        auto *tool = new QToolButton(this);
        tool->setText(text);
        tool->setToolTip(tip);
        tool->setAutoRaise(true);
        return tool;
    };
    // .crlf commands claim bare function keys, so these carry Alt.
    QToolButton *previous = button(QStringLiteral("\u2191"), QStringLiteral("Previous Change (Shift+Alt+F5)"));
    QToolButton *next = button(QStringLiteral("\u2193"), QStringLiteral("Next Change (Alt+F5)"));
    QToolButton *open = button(QStringLiteral("Open File"), QStringLiteral("Open the working copy"));
    connect(previous, &QToolButton::clicked, this, [this] { step(-1); });
    connect(next, &QToolButton::clicked, this, [this] { step(1); });
    connect(open, &QToolButton::clicked, this, [this] { emit openRequested(m_content.path); });
    auto *nextKey = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_F5), this);
    nextKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(nextKey, &QShortcut::activated, this, [this] { step(1); });
    auto *previousKey = new QShortcut(QKeySequence(Qt::SHIFT | Qt::ALT | Qt::Key_F5), this);
    previousKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(previousKey, &QShortcut::activated, this, [this] { step(-1); });

    // Both sides have the same row count, so one scroll value fits both.
    for (auto [from, to] : {std::pair{m_left, m_right}, std::pair{m_right, m_left}}) {
        connect(from->verticalScrollBar(), &QScrollBar::valueChanged, to->verticalScrollBar(), &QScrollBar::setValue);
        connect(from->horizontalScrollBar(), &QScrollBar::valueChanged, to->horizontalScrollBar(),
                &QScrollBar::setValue);
    }

    auto *header = new QHBoxLayout;
    header->setContentsMargins(8, 4, 8, 4);
    header->addWidget(m_leftLabel, 1);
    header->addWidget(m_summary);
    header->addWidget(previous);
    header->addWidget(next);
    header->addWidget(open);
    header->addWidget(m_rightLabel, 1);
    m_rightLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(m_left);
    splitter->addWidget(m_right);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addWidget(splitter, 1);
}

void DiffView::setTheme(const Theme *theme)
{
    m_left->setTheme(theme);
    m_right->setTheme(theme);
}

void DiffView::setContent(const DiffContent &content, const QList<LineChange> &changes)
{
    m_content = content;
    const QStringList before = splitLines(content.left);
    const QStringList after = splitLines(content.right);
    Side left;
    Side right;
    m_changeRows.clear();
    int oldLine = 0;
    int newLine = 0;
    const auto keepUntil = [&](int oldEnd) {
        while (oldLine < oldEnd) {
            left.add(before, oldLine++);
            right.add(after, newLine++);
        }
    };
    for (const LineChange &change : changes) {
        keepUntil(change.oldStart);
        const int row = int(left.rows.size());
        const int rows = qMax(change.oldCount, change.newCount);
        m_changeRows.append(row);
        if (change.oldCount > 0)
            left.marks.append({row, change.oldCount, ChangeKind::Removed});
        if (change.newCount > 0)
            right.marks.append({row, change.newCount, ChangeKind::Added});
        for (int i = 0; i < change.oldCount; ++i)
            left.add(before, oldLine++);
        for (int i = 0; i < change.newCount; ++i)
            right.add(after, newLine++);
        left.pad(rows - change.oldCount);
        right.pad(rows - change.newCount);
    }
    keepUntil(int(before.size()));

    const LanguageDefinition *language = m_registry->definitionForPath(content.path);
    for (auto [editor, side] : {std::pair{m_left, &left}, std::pair{m_right, &right}}) {
        editor->setLanguage(language);
        editor->replaceText(side->rows.join(QLatin1Char('\n')));
        editor->setLineNumberMap(side->numbers);
        editor->setChangeMarks(side->marks, true);
    }
    m_leftLabel->setText(content.leftLabel);
    m_rightLabel->setText(content.rightLabel);
    m_summary->setText(changes.size() == 1 ? QStringLiteral("1 change")
                                           : QStringLiteral("%1 changes").arg(changes.size()));
    if (!m_changeRows.isEmpty()) {
        m_left->goTo(m_changeRows.first() + 1, 1);
        m_right->goTo(m_changeRows.first() + 1, 1);
    }
}

void DiffView::step(int direction)
{
    if (m_changeRows.isEmpty())
        return;
    const int current = m_right->textCursor().blockNumber();
    int target = direction > 0 ? m_changeRows.first() : m_changeRows.last();
    if (direction > 0) {
        for (int row : std::as_const(m_changeRows)) {
            if (row > current) {
                target = row;
                break;
            }
        }
    } else {
        for (auto it = m_changeRows.crbegin(); it != m_changeRows.crend(); ++it) {
            if (*it < current) {
                target = *it;
                break;
            }
        }
    }
    m_left->goTo(target + 1, 1);
    m_right->goTo(target + 1, 1);
}
