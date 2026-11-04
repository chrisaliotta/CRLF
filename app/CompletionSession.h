#pragma once

#include "editor/CompletionItem.h"
#include "io/CommandVariables.h"

#include <QObject>

class ProcessOrchestrator;
class QProcess;

// One language-server process for the project. It lives on the orchestrator
// thread. The UI thread calls start, request, and stop; those queue here.
// See docs/completion.md.
class CompletionSession : public QObject
{
    Q_OBJECT

public:
    explicit CompletionSession(QObject *parent = nullptr);
    ~CompletionSession() override;

    void setOrchestrator(ProcessOrchestrator *orchestrator) { m_orchestrator = orchestrator; }

    // Replaces a session already running for a different command.
    void start(const QString &command, const QString &workingDirectory, const CommandVariables &variables);
    void request(int id, int revision, const QString &path, int line, int column, const QString &text,
                 const QString &kind);
    void stop();

signals:
    void completed(int id, int revision, const QList<CompletionItem> &items);
    void signatureReady(int id, int revision, const QString &label, const QString &parameter);
    void failed(const QString &message);

private:
    void launch(bool withJob = true);
    void stopTree();
    void writeRequest(int id, int revision, const QString &path, int line, int column, const QString &text,
                      const QString &kind);
    void absorbStdout(const QByteArray &bytes);
    void takeLine(const QByteArray &line);

    ProcessOrchestrator *m_orchestrator = nullptr;
    QProcess *m_process = nullptr;
    void *m_job = nullptr;
    QString m_command;
    QString m_workingDirectory;
    CommandVariables m_variables;
    QString m_distro;
    qint64 m_linuxGroup = 0;
    bool m_groupResolved = false;
    QByteArray m_pending;
    int m_token = 0;
    bool m_stopRequested = false;
};
