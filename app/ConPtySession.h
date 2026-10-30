#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <thread>

// One Windows pseudoconsole and the process attached to it.
class ConPtySession : public QObject
{
    Q_OBJECT

public:
    explicit ConPtySession(QObject *parent = nullptr);
    ~ConPtySession() override;

    void start(const QString &program, const QStringList &arguments, const QString &workingDirectory,
               int rows, int cols);
    void writeInput(const QByteArray &data);
    void resize(int rows, int cols);
    void stop();

signals:
    void readyRead(const QByteArray &data);
    void exited();

private:
    void readLoop(int generation);
    void release();
    void fail(const QString &text);

    std::thread m_thread;
    int m_generation = 0;
    int m_rows = 24;
    int m_cols = 80;
    void *m_process = nullptr;
    void *m_pseudoConsole = nullptr;
    void *m_inputWrite = nullptr;
    void *m_outputRead = nullptr;
};
