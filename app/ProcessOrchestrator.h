#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QObject;
class QThreadPool;

// One row on the Processes page. The list is a copy posted to the UI thread;
// the page never reads the orchestrator's memory.
struct ProcessRecord
{
    QString name;
    QString owner;
    // Milliseconds left on a Sleep debounce. Zero while Running.
    int timerMs = 0;
    // "Sleep" while a debounce timer is waiting, "Running" after it fires.
    QString status;
    QString detail;
};

Q_DECLARE_METATYPE(ProcessRecord)

// Where `submit` runs the function.
// Pool: the thread pool, and the record is removed when the function returns.
// Thread: this object's thread, same lifetime as Pool. Used for wsl.exe queries
// that block inside the call.
// Hold: this object's thread, and the record stays until release(). A check or
// a menu command outlives the function that started it.
// Deliver: after the debounce, the function runs on `deliverTo`'s thread. That
// function then calls runOnPool() or releaseBound().
enum class ProcessLane { Pool, Thread, Hold, Deliver };

// Owns child-process bookkeeping and the pool for scans and reads. The object
// lives on its own thread. submit() and cancel() queue onto that thread and
// return. See docs/processes.md.
class ProcessOrchestrator : public QObject
{
    Q_OBJECT

public:
    explicit ProcessOrchestrator(QObject *parent = nullptr);
    ~ProcessOrchestrator() override;

    void submit(const QString &key, const QString &name, const QString &owner, int debounceMs,
                const QString &detail, std::function<void()> work, ProcessLane lane = ProcessLane::Pool,
                QObject *deliverTo = nullptr);
    void cancel(const QString &key);

    // Drops the record when `token` is still the one submit() issued. A newer
    // submit of the same key keeps its record.
    void release(const QString &key, int token);
    // The token of the record, read from this object's thread.
    int currentToken(const QString &key) const;

    // Called from a Deliver function, on the UI thread. Runs `work` on the pool
    // under the deliver's token.
    void runOnPool(const QString &key, std::function<void()> work);
    // Drops the record the current Deliver function is holding.
    void releaseBound();

    // Cancels records and waits for pool tasks. Called on this object's thread.
    void shutdown();

signals:
    void recordsChanged(const QList<ProcessRecord> &live, const QList<ProcessRecord> &recent);

private:
    struct Task;
    struct Submission;

    void submitOnThread(const Submission &submission);
    void cancelOnThread(const QString &key);
    void releaseOnThread(const QString &key, int token);
    void fire(const QString &key);
    void begin(Task *task);
    void startPool(Task *task, int token, const std::function<void()> &work);
    void removeTask(const QString &key, const QString &outcome);
    void publish();

    QThreadPool *m_pool = nullptr;
    class QTimer *m_tick = nullptr;
    QHash<QString, Task *> m_tasks;
    QHash<QString, int> m_tokens;
    QStringList m_order;
    QList<ProcessRecord> m_recent;

    // Written only on the UI thread, from a Deliver callback, and only read
    // there to stamp the pool task with that deliver's token.
    QString m_boundKey;
    int m_boundToken = 0;
};
