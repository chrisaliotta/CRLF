#pragma once

#include <QHash>
#include <QWidget>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;

// Command logs. Closing the bottom panel leaves the process running;
// starting the command again asks for the log to be shown.
class OutputPanel : public QWidget
{
    Q_OBJECT

public:
    explicit OutputPanel(QWidget *parent = nullptr);

    void showCommand(const QString &name);
    void appendOutput(const QString &name, const QString &text);
    void setState(const QString &name, bool running, int exitCode);
    // Further output stays in the log and does not open the panel.
    void suppress();

signals:
    void stopRequested(const QString &name);
    void showRequested();
    void keepVisible();

private:
    struct Log
    {
        QString text;
        bool running = false;
        int exitCode = 0;
        bool finished = false;
    };

    void ensureCommand(const QString &name);
    void showSelected();
    QString currentName() const;

    QComboBox *m_commands = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_stop = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QHash<QString, Log> m_logs;
    bool m_closed = false;
};
