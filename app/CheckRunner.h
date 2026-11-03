#pragma once

#include "editor/Diagnostic.h"
#include "io/CommandVariables.h"

#include <QHash>
#include <QList>
#include <QObject>

class ProcessOrchestrator;

// One check of one editor. See docs/diagnostics.md. This is not a menu
// command: each editor has its own short process, and stdout is a diagnostic
// list rather than a log.
struct CheckRequest
{
    quintptr editorId = 0;
    QString command;
    QString workingDirectory;
    CommandVariables variables;
    QString text;
    int revision = 0;
};

// Runs `request.command` the way CommandRunner launches a command: cmd.exe,
// or wsl.exe when the working directory is a distro path. Standard input is
// the editor buffer. Exit 0 with a JSON array completes; anything else fails
// and leaves the caller's current marks alone.
class CheckRunner : public QObject
{
    Q_OBJECT

public:
    explicit CheckRunner(QObject *parent = nullptr);
    ~CheckRunner() override;

    void setOrchestrator(ProcessOrchestrator *orchestrator) { m_orchestrator = orchestrator; }

    // A check already running for this editor is stopped and replaced.
    // The replacement starts from the finished handler on this thread.
    void start(const CheckRequest &request);
    // The result of a stopped check is dropped.
    void cancel(quintptr editorId);
    void cancelAll();

signals:
    void completed(quintptr editorId, int revision, const QList<Diagnostic> &spans);
    void failed(quintptr editorId, const QString &message);

private:
    struct Running;

    void launch(const CheckRequest &request, bool withJob = true);
    void stopTree(Running &running);
    void absorbStdout(Running &running, const QByteArray &bytes);

    QHash<quintptr, Running *> m_running;
    ProcessOrchestrator *m_orchestrator = nullptr;
};
