#include "app/ProcessesPanel.h"

#include <QHeaderView>
#include <QLabel>
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

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);
    layout->addWidget(liveLabel);
    layout->addWidget(m_live, 1);
    layout->addWidget(recentLabel);
    layout->addWidget(m_recent, 1);
}

void ProcessesPanel::setRecords(const QList<ProcessRecord> &live, const QList<ProcessRecord> &recent)
{
    fill(m_live, live, true);
    fill(m_recent, recent, false);
}
