#pragma once

#include "io/CommandVariables.h"
#include "io/ProjectCommands.h"

#include <QHash>
#include <QObject>

#include <functional>

class QProcess;

// Owns one process per project command. A command keeps running until it
// exits or stop() kills its process tree, so a dev server stays up and a
// build is the same mechanism that happens to finish.
class CommandRunner : public QObject
{
    Q_OBJECT

public:
    explicit CommandRunner(QObject *parent = nullptr);
    ~CommandRunner() override;

    void setProject(const QList<ProjectCommand> &commands);
    // Asked again every time a command starts, including a restart, so
    // ${file} and the rest follow the document focused at that moment.
    void setContextProvider(std::function<CommandVariables()> provider);
    void start(const QString &name);
    void stop(const QString &name);
    void stopAll();
    bool isRunning(const QString &name) const;

signals:
    void outputReceived(const QString &name, const QString &text);
    // exitCode is negative when the user stopped the command.
    void stateChanged(const QString &name, bool running, int exitCode);

private:
    struct Running;

    const ProjectCommand *command(const QString &name) const;
    void launch(const QString &name, bool withJob = true);
    void stopTree(Running &running);

    QList<ProjectCommand> m_commands;
    std::function<CommandVariables()> m_contextProvider;
    QHash<QString, Running *> m_running;
};
