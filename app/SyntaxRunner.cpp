#include "app/SyntaxRunner.h"

#include "app/ProcessLaunch.h"
#include "app/ProcessOrchestrator.h"

#include <QHash>
#include <QProcess>

namespace {


QString decoded(const QByteArray &bytes, bool utf8)
{
    return utf8 ? QString::fromUtf8(bytes) : QString::fromLocal8Bit(bytes);
}

} // namespace

struct SyntaxRunner::Running
{
    QProcess *process = nullptr;
    ProcessLaunch launch;
    bool stopRequested = false;
    bool discard = false;
    bool hasPending = false;
    SyntaxRequest pending;
    int token = 0;
    QByteArray input;
    QByteArray captured;
    QByteArray errors;
    SyntaxRequest request;
};

SyntaxRunner::SyntaxRunner(QObject *parent)
    : QObject(parent)
{
}

SyntaxRunner::~SyntaxRunner()
{
    cancelAll();
    const auto processes = findChildren<QProcess *>(Qt::FindDirectChildrenOnly);
    for (QProcess *process : processes) {
        if (process->state() != QProcess::NotRunning)
            process->setParent(nullptr);
    }
    qDeleteAll(m_running);
    m_running.clear();
}

void SyntaxRunner::start(const SyntaxRequest &request)
{
    if (request.command.isEmpty())
        return;
    Running *running = m_running.value(request.editorId);
    if (running && running->process->state() != QProcess::NotRunning) {
        running->discard = false;
        running->pending = request;
        running->hasPending = true;
        stopTree(*running);
        return;
    }
    launch(request);
}

void SyntaxRunner::cancel(quintptr editorId)
{
    if (m_orchestrator != nullptr)
        m_orchestrator->cancel(QStringLiteral("syntax:%1").arg(editorId));
    Running *running = m_running.value(editorId);
    if (!running)
        return;
    running->discard = true;
    running->hasPending = false;
    if (running->process->state() != QProcess::NotRunning)
        stopTree(*running);
}

void SyntaxRunner::cancelAll()
{
    const auto ids = m_running.keys();
    for (quintptr id : ids)
        cancel(id);
}

void SyntaxRunner::launch(const SyntaxRequest &request)
{
    Running *running = m_running.value(request.editorId);
    if (!running) {
        running = new Running;
        running->process = new QProcess(this);
        m_running.insert(request.editorId, running);
        const quintptr editorId = request.editorId;

        connect(running->process, &QProcess::readyReadStandardOutput, this, [this, editorId] {
            Running *running = m_running.value(editorId);
            if (running)
                running->captured += running->launch.takeOutput(running->process->readAllStandardOutput());
        });
        connect(running->process, &QProcess::readyReadStandardError, this, [this, editorId] {
            Running *running = m_running.value(editorId);
            if (!running)
                return;
            const QByteArray bytes = running->process->readAllStandardError();
            running->errors += bytes;
            if (m_orchestrator != nullptr && !bytes.isEmpty())
                m_orchestrator->appendLog(QStringLiteral("syntax:%1").arg(editorId), QString::fromUtf8(bytes));
        });
        connect(running->process, &QProcess::finished, this, [this, editorId](int exitCode, QProcess::ExitStatus status) {
            Running *done = m_running.value(editorId);
            if (!done)
                return;
            done->captured += done->launch.takeOutput(done->process->readAllStandardOutput());
            done->captured += done->launch.flushOutput();
            const QByteArray tail = done->process->readAllStandardError();
            done->errors += tail;
            if (m_orchestrator != nullptr && !tail.isEmpty())
                m_orchestrator->appendLog(QStringLiteral("syntax:%1").arg(editorId), QString::fromUtf8(tail));

            if (done->hasPending) {
                const SyntaxRequest next = done->pending;
                done->hasPending = false;
                done->discard = false;
                done->stopRequested = false;
                launch(next);
                return;
            }
            const int token = done->token;
            if (done->discard || done->stopRequested) {
                done->discard = false;
                done->stopRequested = false;
                if (m_orchestrator != nullptr)
                    m_orchestrator->release(QStringLiteral("syntax:%1").arg(editorId), token);
                return;
            }

            const bool utf8 = !done->launch.distro().isEmpty();
            if (status == QProcess::CrashExit || exitCode != 0) {
                QString message = decoded(done->errors, utf8).trimmed();
                if (message.isEmpty())
                    message = QStringLiteral("Syntax check failed.");
                emit failed(editorId, message);
            } else {
                QList<Diagnostic> spans;
                if (!diagnosticsFromJson(done->captured, &spans))
                    emit failed(editorId, QStringLiteral("Couldn't read syntax check results."));
                else
                    emit completed(editorId, done->request.revision, spans);
            }
            if (m_orchestrator != nullptr)
                m_orchestrator->release(QStringLiteral("syntax:%1").arg(editorId), token);
        });
        connect(running->process, &QProcess::errorOccurred, this, [this, editorId](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart)
                return;
            Running *failedRun = m_running.value(editorId);
            if (!failedRun || failedRun->launch.retrying() || failedRun->discard || failedRun->stopRequested)
                return;
            emit failed(editorId, failedRun->process->errorString());
            if (m_orchestrator != nullptr)
                m_orchestrator->release(QStringLiteral("syntax:%1").arg(editorId), failedRun->token);
        });
    }

    running->request = request;
    running->token = m_orchestrator == nullptr
                         ? 0
                         : m_orchestrator->currentToken(QStringLiteral("syntax:%1").arg(request.editorId));
    running->stopRequested = false;
    running->discard = false;
    running->captured.clear();
    running->errors.clear();
    running->input = request.text.toUtf8();

    if (m_allowed && !m_allowed(QStringLiteral("syntax"))) {
        if (m_orchestrator != nullptr)
            m_orchestrator->release(QStringLiteral("syntax:%1").arg(request.editorId), running->token);
        return;
    }

    const CommandVariables vars = launchVariables(request.variables, request.workingDirectory);
    running->process->setProcessChannelMode(QProcess::SeparateChannels);
    running->launch.startShell(running->process, request.workingDirectory, expandCommand(request.command, vars));
    if (running->process->state() != QProcess::NotRunning) {
        running->process->write(running->input);
        running->process->closeWriteChannel();
    }
}

void SyntaxRunner::stopTree(Running &running)
{
    running.stopRequested = true;
    running.launch.stopTree(this);
    running.process->kill();
}
