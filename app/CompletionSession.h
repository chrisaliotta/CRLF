#pragma once

#include "editor/CompletionItem.h"
#include "io/CommandVariables.h"

#include <QHash>
#include <QList>
#include <QObject>

class ProcessOrchestrator;
class QProcess;

// One line to the completion script. See docs/completion.md.
struct CompletionRequest
{
    // "completion", "signature", "open", or "resolve".
    QString kind;
    int id = 0;
    int revision = 0;
    QString path;
    int line = 1;
    int column = 1;
    // The editor buffer. A resolve does not carry it.
    QString text;
    // completion: the character that asked, when one did.
    QString trigger;
    // resolve: the row's `data`.
    QString data;
};

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

    // Replaces a session already running for a different command. `restart`
    // replaces it even when the command is the same, so an edited script is
    // read again.
    void start(const QString &command, const QString &workingDirectory, const CommandVariables &variables,
               bool restart = false);
    // An open that arrives while the process is starting is held and written
    // once it runs. Anything else needs the process running.
    void request(const CompletionRequest &request);
    void stop();

signals:
    // `incomplete` means the list was cut short and has to be asked for again
    // when the typed word changes.
    void completed(int id, int revision, const QList<CompletionItem> &items, bool incomplete);
    void resolved(int id, const QString &data, const QString &detail, const QString &documentation);
    void signatureReady(int id, int revision, const QList<SignatureHint> &signatures, int active);
    // `path` is the path the request carried, not the one the script saw.
    void fileStateChanged(const QString &path, const CompletionFileState &state);
    void failed(const QString &message);

private:
    void launch(bool withJob = true);
    void stopTree();
    void write(const CompletionRequest &request);
    void flushHeldOpens();
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
    QList<CompletionRequest> m_heldOpens;
    // The path the script was given, to the path the editor asked with. A
    // file inside a WSL distro is sent as its Linux path.
    QHash<QString, QString> m_editorPaths;
};
