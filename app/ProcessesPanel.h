#pragma once

#include "app/ProcessOrchestrator.h"

#include <QWidget>

class QLabel;
class QTableWidget;

// Live tasks, then the last 20 that finished or were cancelled.
// A debug view; closing it does not stop the work.
class ProcessesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ProcessesPanel(QWidget *parent = nullptr);

    void setRecords(const QList<ProcessRecord> &live, const QList<ProcessRecord> &recent);

private:
    QTableWidget *m_live = nullptr;
    QTableWidget *m_recent = nullptr;
};
