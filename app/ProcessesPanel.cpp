#include "app/ProcessesPanel.h"

#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

void configureTable(QTableWidget *table)
{
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Owner"), QStringLiteral("Timer"),
                                      QStringLiteral("Status"), QStringLiteral("Detail")});
    table->verticalHeader()->hide();
    table->setShowGrid(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setFocusPolicy(Qt::NoFocus);
    table->horizontalHeader()->setMinimumSectionSize(72);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    table->setColumnWidth(0, 140);
    table->setColumnWidth(2, 88);
    table->setColumnWidth(3, 110);
}

void fill(QTableWidget *table, const QList<ProcessRecord> &records, bool showTimer)
{
    table->setRowCount(records.size());
    for (int row = 0; row < records.size(); ++row) {
        const ProcessRecord &record = records.at(row);
        const QString timer = showTimer && record.status == QLatin1String("Sleep")
                                  ? QString::number(record.timerMs)
                                  : QString();
        const QStringList cells{record.name, record.owner, timer, record.status, record.detail};
        for (int column = 0; column < cells.size(); ++column) {
            auto *item = new QTableWidgetItem(cells.at(column));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            table->setItem(row, column, item);
        }
    }
}

} // namespace

ProcessesPanel::ProcessesPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ProcessesPanel"));
    auto *liveLabel = new QLabel(QStringLiteral("Live"), this);
    auto *recentLabel = new QLabel(QStringLiteral("Recent"), this);
    m_live = new QTableWidget(this);
    m_recent = new QTableWidget(this);
    configureTable(m_live);
    configureTable(m_recent);
    m_log = new QPlainTextEdit(this);
    m_log->setObjectName(QStringLiteral("processLog"));
    m_log->setReadOnly(true);
    m_log->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_log->setPlaceholderText(QStringLiteral("Script log. Select a row. Standard error from the script is written here."));
    auto *logLabel = new QLabel(QStringLiteral("Log"), this);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);
    layout->addWidget(liveLabel);
    layout->addWidget(m_live, 2);
    layout->addWidget(recentLabel);
    layout->addWidget(m_recent, 2);
    layout->addWidget(logLabel);
    layout->addWidget(m_log, 3);

    const auto showRow = [this](QTableWidget *table, const QList<ProcessRecord> *records, QTableWidget *other) {
        other->clearSelection();
        const int row = table->currentRow();
        if (row < 0 || row >= records->size()) {
            m_log->clear();
            return;
        }
        m_shown = records;
        m_shownRow = row;
        m_log->setPlainText(records->at(row).log);
        m_log->moveCursor(QTextCursor::End);
    };
    connect(m_live, &QTableWidget::currentCellChanged, this, [this, showRow](int, int, int, int) {
        showRow(m_live, &m_liveRecords, m_recent);
    });
    connect(m_recent, &QTableWidget::currentCellChanged, this, [this, showRow](int, int, int, int) {
        showRow(m_recent, &m_recentRecords, m_live);
    });
}

void ProcessesPanel::setRecords(const QList<ProcessRecord> &live, const QList<ProcessRecord> &recent)
{
    const bool liveShown = m_shown == &m_liveRecords;
    const int shownRow = m_shownRow;
    QString shownName;
    QString shownOwner;
    QString shownStatus;
    const QList<ProcessRecord> &previous = liveShown ? m_liveRecords : m_recentRecords;
    if (shownRow >= 0 && shownRow < previous.size()) {
        shownName = previous.at(shownRow).name;
        shownOwner = previous.at(shownRow).owner;
        shownStatus = previous.at(shownRow).status;
    }

    m_liveRecords = live;
    m_recentRecords = recent;
    fill(m_live, m_liveRecords, true);
    fill(m_recent, m_recentRecords, false);

    const auto reselect = [this](QTableWidget *table, const QList<ProcessRecord> &records, const QString &name,
                                 const QString &owner, const QString &status) {
        for (int row = 0; row < records.size(); ++row) {
            const ProcessRecord &record = records.at(row);
            if (record.name == name && record.owner == owner && record.status == status) {
                table->setCurrentCell(row, 0);
                return true;
            }
        }
        return false;
    };
    if (!shownName.isEmpty()
        && (reselect(m_live, m_liveRecords, shownName, shownOwner, shownStatus)
            || reselect(m_recent, m_recentRecords, shownName, shownOwner, shownStatus))) {
        const int liveRow = m_live->currentRow();
        const int recentRow = m_recent->currentRow();
        if (liveRow >= 0 && liveRow < m_liveRecords.size())
            m_log->setPlainText(m_liveRecords.at(liveRow).log);
        else if (recentRow >= 0 && recentRow < m_recentRecords.size())
            m_log->setPlainText(m_recentRecords.at(recentRow).log);
        m_log->moveCursor(QTextCursor::End);
        return;
    }
    if (!m_liveRecords.isEmpty())
        m_live->setCurrentCell(0, 0);
    else if (!m_recentRecords.isEmpty())
        m_recent->setCurrentCell(0, 0);
    else
        m_log->clear();
}
