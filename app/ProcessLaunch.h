#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

class QObject;
class QProcess;

// How one child process is started and stopped. A working directory under
// \\wsl.localhost or \\wsl$ runs in that distro through wsl.exe. Any other
// directory runs through cmd.exe (a shell command) or directly (a program).
// Each start is placed in a kill-on-close job, so closing the job ends every
// program the child launched. A parent job that refuses a nested job gets one
// more start without it. One object per QProcess, used on that process's thread.
class ProcessLaunch
{
public:
    ProcessLaunch() = default;
    ~ProcessLaunch();
    ProcessLaunch(const ProcessLaunch &) = delete;
    ProcessLaunch &operator=(const ProcessLaunch &) = delete;

    // `commandText` goes to cmd.exe /d /s /c, or to bash -lc inside a distro.
    // The distro shell prints a CRLF_PGID line first so stopTree() can end the
    // Linux process group; takeOutput() removes that line.
    void startShell(QProcess *process, const QString &workingDirectory, const QString &commandText);
    // `program` and `arguments` with no shell between them, so no argument
    // needs quoting for one. A distro runs them through wsl.exe --exec.
    void startProgram(QProcess *process, const QString &workingDirectory, const QString &program,
                      const QStringList &arguments);
    // True while the job-wrapped start is inside QProcess::start. A
    // FailedToStart from that attempt is followed by a second start, so an
    // errorOccurred handler ignores it.
    bool retrying() const { return m_retrying; }
    // Closes the job, which ends the Windows tree, and ends the Linux process
    // group. Neither is waited on. The caller still kills `process` itself.
    // `owner` parents the short wsl.exe kill process.
    void stopTree(QObject *owner);
    // Empty when the process runs on Windows. Its output is UTF-8 otherwise.
    const QString &distro() const { return m_distro; }
    // Standard output with the leading CRLF_PGID line removed. Bytes before
    // that line's newline are held until it arrives.
    QByteArray takeOutput(const QByteArray &bytes);
    // What takeOutput is still holding once the process has exited.
    QByteArray flushOutput();

private:
    void start(QProcess *process, bool withJob);
    void closeJob();

    void *m_job = nullptr;
    QString m_distro;
    qint64 m_linuxGroup = 0;
    bool m_groupPending = false;
    QByteArray m_head;
    bool m_retrying = false;
};
