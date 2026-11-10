#pragma once

#include "app/ProcessOrchestrator.h"

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QStackedWidget;
class QTableWidget;
class QToolButton;

// Active tasks, the last 20 that finished or were cancelled, and the log of
// the row last selected in either. A debug view; closing it does not stop
// the work. Cancel and Reload are requests; MainWindow decides what they do.
class ProcessesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ProcessesPanel(QWidget *parent = nullptr);

    void setRecords(const QList<ProcessRecord> &live, const QList<ProcessRecord> &recent);

signals:
    void cancelRequested(const QString &key);
    void reloadRequested(const QString &key);
    // Start every service the project enables (autocomplete, checks) again.
    void restartServicesRequested();

private:
    void showSection(int index);
    void selectLive(int row);
    void selectRecent(int row);
    // The record whose log is shown, or nullptr once it has left both lists.
    const ProcessRecord *shownRecord() const;
    void refreshLog();
    void refreshButtons();

    QToolButton *m_sections[3] = {};
    QStackedWidget *m_stack = nullptr;
    QTableWidget *m_live = nullptr;
    QTableWidget *m_recent = nullptr;
    QLabel *m_logTitle = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QToolButton *m_cancel = nullptr;
    QToolButton *m_reload = nullptr;
    QToolButton *m_restartServices = nullptr;
    QList<ProcessRecord> m_liveRecords;
    QList<ProcessRecord> m_recentRecords;
    // A live row is found again by key, a finished row by serial.
    QString m_shownKey;
    int m_shownSerial = 0;
    QString m_logText;
};
