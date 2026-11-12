#include "app/AgentController.h"

#include "app/AgentHost.h"
#include "app/AgentPanel.h"
#include "app/OutputPanel.h"
#include "app/ProcessOrchestrator.h"
#include "app/TabWorkspace.h"
#include "editor/CodeEditor.h"

#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QSettings>
#include <QTextCursor>

namespace {

constexpr auto kAgentSetting = "agent";
constexpr auto kShowThinkingSetting = "agentShowThinking";

bool sameAgent(const AgentSettings &a, const AgentSettings &b)
{
    return a.name == b.name && a.process.command == b.process.command
           && a.process.workingDirectory == b.process.workingDirectory && a.permissions.read == b.permissions.read
           && a.permissions.write == b.permissions.write && a.permissions.terminal == b.permissions.terminal;
}

QString endNote(const QString &stopReason)
{
    if (stopReason == QLatin1String("cancelled"))
        return QStringLiteral("Stopped.");
    if (stopReason == QLatin1String("max_tokens"))
        return QStringLiteral("The agent reached its token limit.");
    if (stopReason == QLatin1String("max_turn_requests"))
        return QStringLiteral("The agent reached its limit of requests for one turn.");
    if (stopReason == QLatin1String("refusal"))
        return QStringLiteral("The agent declined.");
    return {};
}

} // namespace

AgentController::AgentController(AgentSession *session, AgentHost *host, AgentPanel *panel, TabWorkspace *tabs,
                                 OutputPanel *output, ProcessOrchestrator *orchestrator, QObject *parent)
    : QObject(parent)
    , m_session(session)
    , m_host(host)
    , m_panel(panel)
    , m_tabs(tabs)
    , m_output(output)
    , m_orchestrator(orchestrator)
    , m_watcher(new QFileSystemWatcher(this))
    , m_chosen(QSettings().value(kAgentSetting).toString())
{
    connect(m_session, &AgentSession::ready, this, [this](const QString &name) {
        m_ready = true;
        m_panel->setStatus(QStringLiteral("%1 is ready").arg(name));
        m_restored.clear();
        m_restoring = true;
        restoreNextOption();
    }, Qt::QueuedConnection);
    connect(m_session, &AgentSession::configOptionsChanged, this, [this](const QList<AgentConfigOption> &options) {
        m_options = options;
        m_panel->setConfigOptions(options);
        if (m_restoring)
            restoreNextOption();
    }, Qt::QueuedConnection);
    connect(m_session, &AgentSession::updated, m_panel, &AgentPanel::applyUpdate, Qt::QueuedConnection);
    connect(m_session, &AgentSession::requested, m_host, &AgentHost::handle, Qt::QueuedConnection);
    connect(m_session, &AgentSession::turnEnded, this, &AgentController::ended, Qt::QueuedConnection);
    connect(m_session, &AgentSession::failed, this, [this](const QString &message) {
        m_panel->addNote(message);
        m_panel->setStatus(message);
    }, Qt::QueuedConnection);
    connect(m_session, &AgentSession::stopped, this, [this] {
        m_started = false;
        m_ready = false;
        m_busy = false;
        m_restoring = false;
        m_queued.clear();
        m_panel->setBusy(false);
        m_panel->dismissAll();
        m_panel->setConfigOptions({});
        m_panel->setStatus(QStringLiteral("%1 exited. The next message starts it again.").arg(m_running.name));
    }, Qt::QueuedConnection);
    connect(m_session, &AgentSession::terminalOutput, m_output, &OutputPanel::record, Qt::QueuedConnection);
    connect(m_session, &AgentSession::terminalState, m_output, &OutputPanel::setState, Qt::QueuedConnection);

    connect(m_panel, &AgentPanel::agentChosen, this, [this](const QString &name) {
        if (name == m_chosen)
            return;
        m_chosen = name;
        QSettings().setValue(kAgentSetting, name);
        watchCurrent();
        restart();
    });
    connect(m_panel, &AgentPanel::restartRequested, this, &AgentController::restart);
    connect(m_panel, &AgentPanel::configOptionChosen, this, [this](const QString &id, const QVariant &value) {
        QSettings().setValue(optionSetting(id), value);
        setOption(id, value);
    });
    m_panel->setShowThinking(QSettings().value(kShowThinkingSetting, true).toBool());
    connect(m_panel, &AgentPanel::showThinkingToggled, this,
            [](bool show) { QSettings().setValue(kShowThinkingSetting, show); });
    connect(m_panel, &AgentPanel::promptSubmitted, this, &AgentController::submit);
    connect(m_panel, &AgentPanel::cancelRequested, this, &AgentController::cancelTurn);
    connect(m_panel, &AgentPanel::locationActivated, this, [this](const QString &path, int line) {
        m_tabs->revealDefinition(path, 0, qMax(1, line), 1);
    });
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        m_watcher->addPath(path);
        if (!m_started)
            return;
        restart();
        m_panel->addNote(QStringLiteral("Started again because %1 changed.").arg(QFileInfo(path).fileName()));
    });
}

void AgentController::setVariablesProvider(std::function<CommandVariables()> provider)
{
    m_variables = std::move(provider);
}

void AgentController::setAgents(const QList<AgentSettings> &agents)
{
    m_agents = agents;
    QStringList names;
    for (const AgentSettings &agent : agents)
        names.append(agent.name);
    if (!names.contains(m_chosen))
        m_chosen = names.isEmpty() ? QString() : names.first();
    m_panel->setAgents(names, m_chosen);
    watchCurrent();

    const AgentSettings *agent = current();
    if (m_started && (agent == nullptr || !sameAgent(*agent, m_running)))
        stop();
    if (m_started)
        return;
    m_panel->setStatus(agent == nullptr ? QStringLiteral("Add an agent to the `agents` list in .crlf to start one.")
                                        : QString());
}

void AgentController::ensureStarted()
{
    if (!m_started && current() != nullptr)
        start();
}

void AgentController::restart()
{
    m_panel->clearTranscript();
    m_queued.clear();
    if (current() != nullptr)
        start();
}

void AgentController::stop()
{
    m_started = false;
    m_ready = false;
    m_busy = false;
    m_restoring = false;
    m_queued.clear();
    m_panel->setBusy(false);
    m_panel->dismissAll();
    m_panel->setConfigOptions({});
    QMetaObject::invokeMethod(m_session, [session = m_session] { session->stop(); }, Qt::QueuedConnection);
}

void AgentController::killTerminal(const QString &terminalId)
{
    QMetaObject::invokeMethod(m_session, [session = m_session, terminalId] { session->killTerminal(terminalId); },
                              Qt::QueuedConnection);
}

const AgentSettings *AgentController::current() const
{
    for (const AgentSettings &agent : m_agents) {
        if (agent.name == m_chosen)
            return &agent;
    }
    return nullptr;
}

void AgentController::start()
{
    m_running = *current();
    m_started = true;
    m_ready = false;
    m_restoring = false;
    m_busy = !m_queued.isEmpty();
    m_panel->setBusy(m_busy);
    m_panel->dismissAll();
    m_panel->setConfigOptions({});
    m_host->begin(m_running.process.workingDirectory, m_running.permissions);
    m_panel->setStatus(QStringLiteral("Starting %1\u2026").arg(m_running.name));
    const AgentLaunch launch{m_running.name, m_running.process.command, m_running.process.workingDirectory,
                             m_variables()};
    m_orchestrator->submit(QStringLiteral("agent"), m_running.name, launch.workingDirectory, 0, launch.command,
                           [session = m_session, launch] { session->start(launch); }, ProcessLane::Hold);
}

void AgentController::submit(const QString &text, bool attachCurrentFile)
{
    AgentPrompt prompt;
    prompt.text = text;
    CodeEditor *editor = m_tabs->currentEditor();
    if (attachCurrentFile && editor != nullptr && !editor->filePath().isEmpty()) {
        prompt.files.append(editor->filePath());
        QString selection = editor->textCursor().selectedText();
        selection.replace(QChar(0x2029), QLatin1Char('\n'));
        if (!selection.isEmpty()) {
            prompt.selectionPath = editor->filePath();
            prompt.selection = selection;
        }
    }
    m_panel->addUserMessage(text);
    if (current() == nullptr) {
        m_panel->addNote(QStringLiteral("No agent is set up for this project."));
        return;
    }
    if (m_ready && !m_busy) {
        send(prompt);
        return;
    }
    m_queued.append(prompt);
    m_busy = true;
    m_panel->setBusy(true);
    ensureStarted();
}

void AgentController::send(const AgentPrompt &prompt)
{
    m_busy = true;
    m_panel->setBusy(true);
    m_panel->setStatus(QStringLiteral("%1 is working\u2026").arg(m_running.name));
    QMetaObject::invokeMethod(m_session, [session = m_session, prompt] { session->prompt(prompt); },
                              Qt::QueuedConnection);
}

void AgentController::cancelTurn()
{
    m_panel->dismissAll();
    // Nothing has been sent yet while the agent starts or takes its settings.
    if (!m_ready || m_restoring) {
        m_queued.clear();
        m_busy = false;
        m_panel->setBusy(false);
        return;
    }
    QMetaObject::invokeMethod(m_session, [session = m_session] { session->cancel(); }, Qt::QueuedConnection);
}

void AgentController::ended(const QString &stopReason)
{
    m_busy = false;
    m_panel->setBusy(false);
    m_panel->dismissAll();
    m_panel->setStatus(QStringLiteral("%1 is ready").arg(m_running.name));
    const QString note = endNote(stopReason);
    if (!note.isEmpty())
        m_panel->addNote(note);
    if (!m_queued.isEmpty())
        send(m_queued.takeFirst());
}

void AgentController::setOption(const QString &id, const QVariant &value)
{
    QMetaObject::invokeMethod(m_session, [session = m_session, id, value] { session->setConfigOption(id, value); },
                              Qt::QueuedConnection);
}

void AgentController::restoreNextOption()
{
    for (const AgentConfigOption &option : std::as_const(m_options)) {
        if (m_restored.contains(option.id))
            continue;
        const QVariant stored = QSettings().value(optionSetting(option.id));
        if (!stored.isValid())
            continue;
        // The settings store hands a bool back as text.
        const bool boolean = option.type == QLatin1String("boolean");
        const QVariant wanted = boolean ? QVariant(stored.toBool()) : QVariant(stored.toString());
        bool offered = boolean;
        for (const AgentConfigChoice &choice : option.choices)
            offered = offered || choice.value == wanted.toString();
        if (!offered || wanted == option.currentValue)
            continue;
        m_restored.insert(option.id);
        setOption(option.id, wanted);
        return;
    }
    m_restoring = false;
    if (!m_queued.isEmpty())
        send(m_queued.takeFirst());
}

QString AgentController::optionSetting(const QString &id) const
{
    return QStringLiteral("agentOptions/%1/%2").arg(m_running.name, id);
}

void AgentController::watchCurrent()
{
    if (!m_watcher->files().isEmpty())
        m_watcher->removePaths(m_watcher->files());
    const AgentSettings *agent = current();
    if (agent != nullptr && !agent->process.watch.isEmpty())
        m_watcher->addPaths(agent->process.watch);
}
