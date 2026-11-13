#include "app/AgentPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr int kRenderDelayMs = 50;
constexpr int kToolTextLength = 200;

// A fence still streaming would turn the rest of the page into code.
QString closedFences(const QString &text)
{
    return text.count(QStringLiteral("```")) % 2 == 1 ? text + QStringLiteral("\n```") : text;
}

QString quoted(const QString &text)
{
    QStringList lines = text.split(QLatin1Char('\n'));
    for (QString &line : lines)
        line.prepend(QStringLiteral("> "));
    return lines.join(QLatin1Char('\n'));
}

QString escaped(const QString &text)
{
    QString result;
    result.reserve(text.size());
    for (const QChar c : text) {
        if (QStringLiteral("\\`*_[]<>#|").contains(c))
            result += QLatin1Char('\\');
        result += c;
    }
    return result;
}

QString clipped(const QString &text, int length)
{
    const QString single = text.simplified();
    return single.size() > length ? single.left(length - 1) + QChar(0x2026) : single;
}

QString statusMark(const QString &status)
{
    if (status == QLatin1String("completed"))
        return QStringLiteral("\u2713");
    if (status == QLatin1String("failed"))
        return QStringLiteral("\u2717");
    if (status == QLatin1String("in_progress"))
        return QStringLiteral("\u25D0");
    return QStringLiteral("\u25CB");
}

QString locationLink(const AgentToolLocation &location)
{
    QUrl url = QUrl::fromLocalFile(location.path);
    QString label = QFileInfo(location.path).fileName();
    if (location.line > 0) {
        url.setFragment(QString::number(location.line));
        label += QLatin1Char(':') + QString::number(location.line);
    }
    return QStringLiteral("[%1](%2)").arg(escaped(label), url.toString(QUrl::FullyEncoded));
}

} // namespace

AgentPanel::AgentPanel(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(260);

    auto *title = new QLabel(QStringLiteral("Agent"), this);
    title->setObjectName(QStringLiteral("agentTitle"));
    m_agents = new QComboBox(this);
    m_agents->setToolTip(QStringLiteral("The agents listed under `agents` in the .crlf files"));
    m_thinking = new QToolButton(this);
    m_thinking->setText(QStringLiteral("Thinking"));
    m_thinking->setToolTip(QStringLiteral("Show the agent's thinking as it streams"));
    m_thinking->setCheckable(true);
    m_thinking->setAutoRaise(true);
    m_options = new QWidget(this);
    m_optionLayout = new QGridLayout(m_options);
    m_optionLayout->setContentsMargins(10, 0, 8, 0);
    m_optionLayout->setHorizontalSpacing(6);
    m_optionLayout->setVerticalSpacing(4);
    m_optionLayout->setColumnStretch(1, 1);
    m_optionLayout->setColumnStretch(3, 1);
    m_options->hide();
    auto *restart = new QToolButton(this);
    restart->setText(QStringLiteral("New"));
    restart->setToolTip(QStringLiteral("Start a new session"));
    restart->setAutoRaise(true);
    auto *close = new QToolButton(this);
    close->setText(QStringLiteral("\u2715"));
    close->setToolTip(QStringLiteral("Close"));
    close->setAutoRaise(true);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("agentStatus"));
    m_status->setWordWrap(true);
    m_usage = new QLabel(this);
    m_usage->setObjectName(QStringLiteral("agentUsage"));
    m_usage->setWordWrap(true);
    m_usage->setToolTip(QStringLiteral(
        "How full the agent's context is, the tokens this session has used, and what they cost at the agent's "
        "price list. The cost is an estimate."));
    m_usage->hide();

    m_transcript = new QTextBrowser(this);
    m_transcript->setOpenLinks(false);
    m_transcript->setFrameShape(QFrame::NoFrame);

    m_cards = new QWidget(this);
    m_cardLayout = new QVBoxLayout(m_cards);
    m_cardLayout->setContentsMargins(8, 4, 8, 4);
    m_cardLayout->setSpacing(6);
    m_cards->hide();

    m_input = new QPlainTextEdit(this);
    m_input->setPlaceholderText(QStringLiteral("Ask the agent. Enter sends; Shift+Enter starts a new line."));
    m_input->setFixedHeight(84);
    m_input->installEventFilter(this);
    m_attach = new QCheckBox(QStringLiteral("Include the current file"), this);
    m_attach->setChecked(true);
    m_send = new QPushButton(QStringLiteral("Send"), this);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(8, 4, 6, 4);
    header->setSpacing(6);
    header->addWidget(title);
    header->addWidget(m_agents, 1);
    header->addWidget(m_thinking);
    header->addWidget(restart);
    header->addWidget(close);

    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(8, 0, 8, 8);
    footer->addWidget(m_attach, 1);
    footer->addWidget(m_send);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addLayout(header);
    layout->addWidget(m_options);
    layout->addWidget(m_status);
    layout->addWidget(m_usage);
    layout->addWidget(m_transcript, 1);
    layout->addWidget(m_cards);
    auto *inputRow = new QHBoxLayout;
    inputRow->setContentsMargins(8, 0, 8, 0);
    inputRow->addWidget(m_input);
    layout->addLayout(inputRow);
    layout->addLayout(footer);

    m_render = new QTimer(this);
    m_render->setSingleShot(true);
    m_render->setInterval(kRenderDelayMs);
    connect(m_render, &QTimer::timeout, this, &AgentPanel::render);
    connect(m_agents, &QComboBox::textActivated, this, &AgentPanel::agentChosen);
    connect(restart, &QToolButton::clicked, this, &AgentPanel::restartRequested);
    connect(m_thinking, &QToolButton::clicked, this, [this](bool checked) {
        scheduleRender();
        emit showThinkingToggled(checked);
    });
    connect(close, &QToolButton::clicked, this, &AgentPanel::closeRequested);
    connect(m_send, &QPushButton::clicked, this, [this] {
        if (m_busy)
            emit cancelRequested();
        else
            submit();
    });
    connect(m_transcript, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        emit locationActivated(url.toLocalFile(), url.fragment().toInt());
    });
}

void AgentPanel::setAgents(const QStringList &names, const QString &current)
{
    m_agents->clear();
    m_agents->addItems(names);
    m_agents->setCurrentText(current);
}

QString AgentPanel::currentAgent() const
{
    return m_agents->currentText();
}

void AgentPanel::setConfigOptions(const QList<AgentConfigOption> &options)
{
    while (QLayoutItem *item = m_optionLayout->takeAt(0)) {
        item->widget()->deleteLater();
        delete item;
    }
    // Two settings to a row. The model list gets a row to itself.
    int row = 0;
    int column = 0;
    for (const AgentConfigOption &option : options) {
        const QString id = option.id;
        if (option.type == QLatin1String("boolean")) {
            auto *box = new QCheckBox(option.name, m_options);
            box->setToolTip(option.description);
            box->setChecked(option.currentValue.toBool());
            connect(box, &QCheckBox::clicked, this, [this, id](bool checked) { emit configOptionChosen(id, checked); });
            m_optionLayout->addWidget(box, row, column * 2, 1, 2);
        } else {
            auto *label = new QLabel(option.name, m_options);
            label->setObjectName(QStringLiteral("agentOptionLabel"));
            auto *combo = new QComboBox(m_options);
            combo->setToolTip(option.description.isEmpty() ? option.name : option.description);
            combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            combo->setMinimumContentsLength(6);
            for (const AgentConfigChoice &choice : option.choices) {
                combo->addItem(choice.name, choice.value);
                combo->setItemData(combo->count() - 1, choice.description, Qt::ToolTipRole);
            }
            combo->setCurrentIndex(combo->findData(option.currentValue.toString()));
            connect(combo, &QComboBox::activated, this,
                    [this, id, combo](int index) { emit configOptionChosen(id, combo->itemData(index)); });
            const bool wide = option.category == QLatin1String("model");
            if (wide && column != 0) {
                ++row;
                column = 0;
            }
            m_optionLayout->addWidget(label, row, column * 2);
            m_optionLayout->addWidget(combo, row, column * 2 + 1, 1, wide ? 3 : 1);
            if (wide)
                column = 1;
        }
        if (++column == 2) {
            ++row;
            column = 0;
        }
    }
    m_options->setVisible(!options.isEmpty());
}

void AgentPanel::clearUsage()
{
    m_usage->clear();
    m_usage->hide();
}

void AgentPanel::setShowThinking(bool show)
{
    m_thinking->setChecked(show);
    scheduleRender();
}

void AgentPanel::setStatus(const QString &text)
{
    m_status->setText(text);
    m_status->setVisible(!text.isEmpty());
}

void AgentPanel::setBusy(bool busy)
{
    m_busy = busy;
    m_send->setText(busy ? QStringLiteral("Stop") : QStringLiteral("Send"));
    scheduleRender();
}

void AgentPanel::clearTranscript()
{
    m_entries.clear();
    scheduleRender();
}

void AgentPanel::addUserMessage(const QString &text)
{
    m_entries.append({Entry::Kind::User, text});
    scheduleRender();
}

void AgentPanel::addNote(const QString &text)
{
    m_entries.append({Entry::Kind::Note, text});
    scheduleRender();
}

void AgentPanel::applyUpdate(const AgentUpdate &update)
{
    switch (update.kind) {
    case AgentUpdateKind::AgentText:
        appendText(Entry::Kind::Agent, update.text);
        break;
    case AgentUpdateKind::Thought:
        appendText(Entry::Kind::Thought, update.text);
        break;
    case AgentUpdateKind::ToolCall:
    case AgentUpdateKind::ToolCallUpdate:
        applyTool(update);
        break;
    case AgentUpdateKind::Plan: {
        Entry entry;
        entry.kind = Entry::Kind::Plan;
        entry.plan = update.plan;
        if (!m_entries.isEmpty() && m_entries.last().kind == Entry::Kind::Plan)
            m_entries.last() = entry;
        else
            m_entries.append(entry);
        break;
    }
    case AgentUpdateKind::Usage:
        m_usage->setText(usageLine(update.usage));
        m_usage->show();
        return;
    case AgentUpdateKind::UserText:
    case AgentUpdateKind::ConfigOptions:
    case AgentUpdateKind::Ignored:
        return;
    }
    scheduleRender();
}

void AgentPanel::appendText(Entry::Kind kind, const QString &text)
{
    if (!m_entries.isEmpty() && m_entries.last().kind == kind)
        m_entries.last().text += text;
    else
        m_entries.append({kind, text});
}

void AgentPanel::applyTool(const AgentUpdate &update)
{
    for (Entry &entry : m_entries) {
        if (entry.kind != Entry::Kind::Tool || entry.toolCallId != update.toolCallId)
            continue;
        if (!update.title.isEmpty())
            entry.title = update.title;
        if (!update.status.isEmpty())
            entry.status = update.status;
        if (!update.text.isEmpty())
            entry.text = update.text;
        if (!update.locations.isEmpty())
            entry.locations = update.locations;
        return;
    }
    Entry entry;
    entry.kind = Entry::Kind::Tool;
    entry.toolCallId = update.toolCallId;
    entry.title = update.title;
    entry.status = update.status;
    entry.text = update.text;
    entry.locations = update.locations;
    m_entries.append(entry);
}

void AgentPanel::ask(const QString &title, const QString &detail, const QList<AgentChoice> &choices,
                     std::function<void()> dismissed)
{
    auto *card = new QFrame(m_cards);
    card->setObjectName(QStringLiteral("agentCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(10, 8, 10, 8);
    auto *heading = new QLabel(title, card);
    heading->setObjectName(QStringLiteral("agentCardTitle"));
    heading->setWordWrap(true);
    layout->addWidget(heading);
    if (!detail.isEmpty()) {
        auto *body = new QLabel(detail, card);
        body->setWordWrap(true);
        body->setTextFormat(Qt::PlainText);
        body->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(body);
    }
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    for (const AgentChoice &choice : choices) {
        auto *button = new QPushButton(choice.label, card);
        connect(button, &QPushButton::clicked, this, [this, card, choice] {
            if (choice.closes)
                closeCard(card);
            choice.chosen();
        });
        buttons->addWidget(button);
    }
    layout->addLayout(buttons);
    m_cardLayout->addWidget(card);
    m_open.append({card, std::move(dismissed)});
    m_cards->show();
}

void AgentPanel::closeCard(QWidget *widget)
{
    for (int i = 0; i < m_open.size(); ++i) {
        if (m_open.at(i).widget == widget) {
            m_open.removeAt(i);
            break;
        }
    }
    widget->deleteLater();
    m_cards->setVisible(!m_open.isEmpty());
}

void AgentPanel::dismissAll()
{
    const QList<Card> open = m_open;
    m_open.clear();
    for (const Card &card : open) {
        card.widget->deleteLater();
        card.dismissed();
    }
    m_cards->hide();
}

void AgentPanel::focusInput()
{
    m_input->setFocus();
}

bool AgentPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && !(key->modifiers() & Qt::ShiftModifier)) {
            submit();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void AgentPanel::submit()
{
    const QString text = m_input->toPlainText().trimmed();
    if (m_busy || text.isEmpty())
        return;
    m_input->clear();
    emit promptSubmitted(text, m_attach->isChecked());
}

void AgentPanel::scheduleRender()
{
    if (!m_render->isActive())
        m_render->start();
}

void AgentPanel::render()
{
    QScrollBar *bar = m_transcript->verticalScrollBar();
    const bool following = bar->value() >= bar->maximum() - 4;
    const int kept = bar->value();
    m_transcript->setMarkdown(markdown());
    bar->setValue(following ? bar->maximum() : kept);
}

QString AgentPanel::markdown() const
{
    QStringList blocks;
    for (const Entry &entry : m_entries) {
        switch (entry.kind) {
        case Entry::Kind::User:
            blocks.append(QStringLiteral("**You**\n\n") + quoted(entry.text));
            break;
        case Entry::Kind::Agent:
            blocks.append(closedFences(entry.text));
            break;
        case Entry::Kind::Thought:
            // Shown in full, or as one line for as long as it is the newest
            // thing the agent is doing.
            if (m_thinking->isChecked())
                blocks.append(QStringLiteral("*Thinking*\n\n") + quoted(closedFences(entry.text.trimmed())));
            else if (m_busy && &entry == &m_entries.last())
                blocks.append(QStringLiteral("*Thinking\u2026*"));
            break;
        case Entry::Kind::Tool: {
            QString line = QStringLiteral("%1 **%2**").arg(statusMark(entry.status), escaped(entry.title));
            QStringList links;
            for (const AgentToolLocation &location : entry.locations)
                links.append(locationLink(location));
            if (!links.isEmpty())
                line += QStringLiteral(" \u00B7 ") + links.join(QStringLiteral(", "));
            if (!entry.text.isEmpty())
                line += QStringLiteral("  \n") + escaped(clipped(entry.text.section(QLatin1Char('\n'), 0, 0), kToolTextLength));
            blocks.append(line);
            break;
        }
        case Entry::Kind::Plan: {
            QStringList items;
            for (const AgentPlanEntry &step : entry.plan)
                items.append(QStringLiteral("- [%1] %2").arg(step.status == QLatin1String("completed") ? QStringLiteral("x")
                                                                                                   : QStringLiteral(" "),
                                                            escaped(step.content)));
            blocks.append(QStringLiteral("**Plan**\n\n") + items.join(QLatin1Char('\n')));
            break;
        }
        case Entry::Kind::Note:
            blocks.append(QStringLiteral("*%1*").arg(escaped(entry.text)));
            break;
        }
    }
    return blocks.join(QStringLiteral("\n\n"));
}
