#pragma once

#include "io/ProjectCommands.h"

#include <QHash>
#include <QObject>

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
    QHash<QString, Running *> m_running;
};
