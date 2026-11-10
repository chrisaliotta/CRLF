#include "app/ProcessesPanel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

enum Column { Name, Owner, Timer, Status, Activity, Detail, ColumnCount };

void configureTable(QTableWidget *table)
{
    table->setColumnCount(ColumnCount);
    table->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Owner"), QStringLiteral("Timer"),
                                      QStringLiteral("Status"), QStringLiteral("Activity"),
                                      QStringLiteral("Detail")});
    table->verticalHeader()->hide();
    table->setShowGrid(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setFocusPolicy(Qt::NoFocus);
    QHeaderView *header = table->horizontalHeader();
    header->setMinimumSectionSize(72);
    header->setStretchLastSection(true);
    header->setSectionResizeMode(Name, QHeaderView::Interactive);
    header->setSectionResizeMode(Owner, QHeaderView::Stretch);
    header->setSectionResizeMode(Timer, QHeaderView::Interactive);
    header->setSectionResizeMode(Status, QHeaderView::Interactive);
    header->setSectionResizeMode(Activity, QHeaderView::Interactive);
    header->setSectionResizeMode(Detail, QHeaderView::Stretch);
    table->setColumnWidth(Name, 140);
    table->setColumnWidth(Timer, 88);
    table->setColumnWidth(Status, 110);
    table->setColumnWidth(Activity, 240);
}

void fill(QTableWidget *table, const QList<ProcessRecord> &records)
{
    table->setRowCount(records.size());
    for (int row = 0; row < records.size(); ++row) {
        const ProcessRecord &record = records.at(row);
        const QString timer = record.status == QLatin1String("Sleep") ? QString::number(record.timerMs)
                                                                       : QString();
        const QStringList cells{record.name, record.owner, timer, record.status, record.activity, record.detail};
        for (int column = 0; column < cells.size(); ++column) {
            auto *item = new QTableWidgetItem(cells.at(column));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            item->setToolTip(cells.at(column));
            table->setItem(row, column, item);
        }
    }
}

QToolButton *sectionButton(const QString &text, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("processSection"));
    button->setText(text);
    button->setCheckable(true);
    button->setAutoExclusive(true);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

QToolButton *actionButton(const QString &text, const QString &tip, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setToolTip(tip);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

} // namespace

ProcessesPanel::ProcessesPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ProcessesPanel"));

    m_sections[0] = sectionButton(QStringLiteral("Active"), this);
    m_sections[1] = sectionButton(QStringLiteral("History"), this);
    m_sections[2] = sectionButton(QStringLiteral("Log"), this);
    m_cancel = actionButton(QStringLiteral("Cancel"), QStringLiteral("Stop the selected task"), this);
    m_reload = actionButton(QStringLiteral("Reload"),
                            QStringLiteral("Stop the selected task and start it again. A script is read from disk again."),
                            this);
    m_restartServices = actionButton(
        QStringLiteral("Restart Services"),
        QStringLiteral("Start autocomplete and the open editors' checks again, as the project enables them"), this);

    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(0, 0, 0, 0);
    bar->setSpacing(4);
    for (QToolButton *button : m_sections)
        bar->addWidget(button);
    bar->addStretch(1);
    bar->addWidget(m_cancel);
    bar->addWidget(m_reload);
    bar->addWidget(m_restartServices);

    m_live = new QTableWidget(this);
    m_recent = new QTableWidget(this);
    configureTable(m_live);
    configureTable(m_recent);
    m_recent->setColumnHidden(Timer, true);

    auto *logPage = new QWidget(this);
    m_logTitle = new QLabel(logPage);
    m_logTitle->setTextFormat(Qt::PlainText);
    m_log = new QPlainTextEdit(logPage);
    m_log->setObjectName(QStringLiteral("processLog"));
    m_log->setReadOnly(true);
    m_log->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_log->setPlaceholderText(QStringLiteral("Standard error from the selected task's script is written here."));
    auto *logLayout = new QVBoxLayout(logPage);
    logLayout->setContentsMargins(0, 0, 0, 0);
    logLayout->setSpacing(4);
    logLayout->addWidget(m_logTitle);
    logLayout->addWidget(m_log, 1);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_live);
    m_stack->addWidget(m_recent);
    m_stack->addWidget(logPage);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(6);
    layout->addLayout(bar);
    layout->addWidget(m_stack, 1);

    for (int index = 0; index < 3; ++index)
        connect(m_sections[index], &QToolButton::clicked, this, [this, index] { showSection(index); });
    connect(m_live, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) { selectLive(row); });
    connect(m_recent, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) { selectRecent(row); });
    connect(m_live, &QTableWidget::cellDoubleClicked, this, [this] { showSection(2); });
    connect(m_recent, &QTableWidget::cellDoubleClicked, this, [this] { showSection(2); });
    connect(m_cancel, &QToolButton::clicked, this, [this] {
        if (const ProcessRecord *record = shownRecord(); record != nullptr && record->serial == 0)
            emit cancelRequested(record->key);
    });
    connect(m_reload, &QToolButton::clicked, this, [this] {
        if (const ProcessRecord *record = shownRecord(); record != nullptr && record->serial == 0)
            emit reloadRequested(record->key);
    });
    connect(m_restartServices, &QToolButton::clicked, this, &ProcessesPanel::restartServicesRequested);

    showSection(0);
}

void ProcessesPanel::showSection(int index)
{
    m_sections[index]->setChecked(true);
    m_stack->setCurrentIndex(index);
    refreshButtons();
}

void ProcessesPanel::selectLive(int row)
{
    if (row < 0 || row >= m_liveRecords.size())
        return;
    {
        const QSignalBlocker blocker(m_recent);
        m_recent->clearSelection();
        m_recent->setCurrentCell(-1, -1);
    }
    m_shownKey = m_liveRecords.at(row).key;
    m_shownSerial = 0;
    refreshLog();
    refreshButtons();
}

void ProcessesPanel::selectRecent(int row)
{
    if (row < 0 || row >= m_recentRecords.size())
        return;
    {
        const QSignalBlocker blocker(m_live);
        m_live->clearSelection();
        m_live->setCurrentCell(-1, -1);
    }
    m_shownKey = m_recentRecords.at(row).key;
    m_shownSerial = m_recentRecords.at(row).serial;
    refreshLog();
    refreshButtons();
}

const ProcessRecord *ProcessesPanel::shownRecord() const
{
    if (m_shownKey.isEmpty())
        return nullptr;
    if (m_shownSerial > 0) {
        for (const ProcessRecord &record : m_recentRecords) {
            if (record.serial == m_shownSerial)
                return &record;
        }
        return nullptr;
    }
    for (const ProcessRecord &record : m_liveRecords) {
        if (record.key == m_shownKey)
            return &record;
    }
    // A live task that just finished keeps its log on screen as its
    // History row.
    for (const ProcessRecord &record : m_recentRecords) {
        if (record.key == m_shownKey)
            return &record;
    }
    return nullptr;
}

void ProcessesPanel::setRecords(const QList<ProcessRecord> &live, const QList<ProcessRecord> &recent)
{
    m_liveRecords = live;
    m_recentRecords = recent;
    const QSignalBlocker liveBlocker(m_live);
    const QSignalBlocker recentBlocker(m_recent);
    fill(m_live, m_liveRecords);
    fill(m_recent, m_recentRecords);

    if (m_shownKey.isEmpty() && !m_liveRecords.isEmpty()) {
        m_shownKey = m_liveRecords.first().key;
        m_shownSerial = 0;
    }
    m_live->clearSelection();
    m_recent->clearSelection();
    m_live->setCurrentCell(-1, -1);
    m_recent->setCurrentCell(-1, -1);
    if (const ProcessRecord *record = shownRecord()) {
        if (record->serial == 0) {
            m_live->setCurrentCell(int(record - m_liveRecords.constData()), Name);
        } else {
            m_recent->setCurrentCell(int(record - m_recentRecords.constData()), Name);
        }
    }
    refreshLog();
    refreshButtons();
}

void ProcessesPanel::refreshLog()
{
    const ProcessRecord *record = shownRecord();
    if (record == nullptr) {
        m_logTitle->setText(QStringLiteral("Select a row in Active or History."));
    } else {
        QString title = record->name + QStringLiteral(" \u2014 ") + record->owner + QStringLiteral("  (")
                        + record->status + QLatin1Char(')');
        if (!record->activity.isEmpty())
            title += QStringLiteral("  ") + record->activity;
        m_logTitle->setText(title);
    }
    const QString text = record != nullptr ? record->log : QString();
    if (text == m_logText)
        return;
    QScrollBar *bar = m_log->verticalScrollBar();
    const bool atEnd = bar->value() >= bar->maximum() - 2;
    const int value = bar->value();
    if (!m_logText.isEmpty() && text.startsWith(m_logText)) {
        QTextCursor cursor(m_log->document());
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(text.mid(m_logText.size()));
    } else {
        m_log->setPlainText(text);
    }
    m_logText = text;
    bar->setValue(atEnd ? bar->maximum() : value);
}

void ProcessesPanel::refreshButtons()
{
    const bool active = m_stack->currentIndex() == 0;
    m_cancel->setVisible(active);
    m_reload->setVisible(active);
    m_restartServices->setVisible(active);
    const ProcessRecord *record = shownRecord();
    const bool live = record != nullptr && record->serial == 0;
    m_cancel->setEnabled(live);
    m_reload->setEnabled(live && record->reloadable);
}
