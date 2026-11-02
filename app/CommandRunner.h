#pragma once

#include "io/CommandVariables.h"
#include "io/ProjectCommands.h"

#include <QHash>
#include <QObject>

#include <functional>

class QProcess;

// Text of the focused editor for a command whose output is "editor".
// active is false in the hex view; that launch keeps its output in the panel.
struct EditorFeed
{
    bool active = false;
    QString text;
};

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
    // Asked when a command's output is "editor", at the same moment as the
    // context, so stdin is the document that document's variables describe.
    void setEditorInputProvider(std::function<EditorFeed(const QString &name)> provider);
    void start(const QString &name);
    void stop(const QString &name);
    void stopAll();
    bool isRunning(const QString &name) const;
    bool sendsOutputToEditor(const QString &name) const;

signals:
    void outputReceived(const QString &name, const QString &text);
    // Standard output of an "editor" command that exited 0. The file on
    // disk is untouched; the receiver replaces the editor this launch read.
    void editorOutput(const QString &name, const QString &text);
    // exitCode is negative when the user stopped the command.
    void stateChanged(const QString &name, bool running, int exitCode);

private:
    struct Running;

    const ProjectCommand *command(const QString &name) const;
    void launch(const QString &name, bool withJob = true);
    void stopTree(Running &running);
    // Panel output, or the bytes an "editor" command will apply at exit.
    // A distro's leading CRLF_PGID line is consumed here.
    void absorbStdout(const QString &name, const QByteArray &bytes);

    QList<ProjectCommand> m_commands;
    std::function<CommandVariables()> m_contextProvider;
    std::function<EditorFeed(const QString &name)> m_editorInput;
    QHash<QString, Running *> m_running;
};
