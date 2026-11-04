#include "app/ProcessOrchestrator.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>

namespace {

struct Submission
{
    QString key;
    QString name;
    QString owner;
    int debounceMs = 0;
    QString detail;
    std::function<void()> work;
    ProcessLane lane = ProcessLane::Pool;
    QPointer<QObject> deliverTo;
};

} // namespace

struct ProcessOrchestrator::Task
{
    QString key;
    QString name;
    QString owner;
    QString detail;
    QString log;
    std::function<void()> work;
    ProcessLane lane = ProcessLane::Pool;
    QPointer<QObject> deliverTo;
    int token = 0;
    bool sleeping = false;
    QDeadlineTimer deadline;
    QTimer *timer = nullptr;
};

struct ProcessOrchestrator::Submission : ::Submission
{
};

ProcessOrchestrator::ProcessOrchestrator(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<ProcessRecord>();
    qRegisterMetaType<QList<ProcessRecord>>();
}

ProcessOrchestrator::~ProcessOrchestrator()
{
    if (m_pool != nullptr)
        m_pool->waitForDone();
}

void ProcessOrchestrator::submit(const QString &key, const QString &name, const QString &owner,
                                 int debounceMs, const QString &detail, std::function<void()> work,
                                 ProcessLane lane, QObject *deliverTo)
{
    auto submission = std::make_shared<Submission>();
    submission->key = key;
    submission->name = name;
    submission->owner = owner;
    submission->debounceMs = debounceMs;
    submission->detail = detail;
    submission->work = std::move(work);
    submission->lane = lane;
    submission->deliverTo = deliverTo;
    QMetaObject::invokeMethod(this, [this, submission] { submitOnThread(*submission); },
                              Qt::QueuedConnection);
}

void ProcessOrchestrator::cancel(const QString &key)
{
    if (QThread::currentThread() == thread()) {
        cancelOnThread(key);
        return;
    }
    QMetaObject::invokeMethod(this, [this, key] { cancelOnThread(key); }, Qt::QueuedConnection);
}

void ProcessOrchestrator::release(const QString &key, int token)
{
    if (QThread::currentThread() == thread()) {
        releaseOnThread(key, token);
        return;
    }
    QMetaObject::invokeMethod(this, [this, key, token] { releaseOnThread(key, token); },
                              Qt::QueuedConnection);
}

int ProcessOrchestrator::currentToken(const QString &key) const
{
    return m_tokens.value(key);
}

void ProcessOrchestrator::appendLog(const QString &key, const QString &text)
{
    if (text.isEmpty())
        return;
    if (QThread::currentThread() == thread()) {
        appendLogOnThread(key, text);
        return;
    }
    QMetaObject::invokeMethod(this, [this, key, text] { appendLogOnThread(key, text); },
                              Qt::QueuedConnection);
}

void ProcessOrchestrator::appendLogOnThread(const QString &key, const QString &text)
{
    Task *task = m_tasks.value(key);
    if (task == nullptr)
        return;
    task->log.append(text);
    const int maxChars = 12000;
    if (task->log.size() > maxChars)
        task->log = task->log.right(maxChars);
    publish();
}

void ProcessOrchestrator::runOnPool(const QString &key, std::function<void()> work)
{
    const int token = m_boundToken;
    const QString bound = m_boundKey;
    QMetaObject::invokeMethod(this, [this, key, token, bound, work = std::move(work)] {
        if (bound != key || m_tokens.value(key) != token)
            return;
        Task *task = m_tasks.value(key);
        if (task == nullptr || task->token != token)
            return;
        startPool(task, token, work);
    }, Qt::QueuedConnection);
}

void ProcessOrchestrator::releaseBound()
{
    release(m_boundKey, m_boundToken);
}

void ProcessOrchestrator::shutdown()
{
    if (m_tick != nullptr)
        m_tick->stop();
    const QStringList keys = m_tasks.keys();
    for (const QString &key : keys)
        cancelOnThread(key);
    if (m_pool != nullptr) {
        m_pool->clear();
        m_pool->waitForDone();
    }
    QCoreApplication::sendPostedEvents(this);
}

void ProcessOrchestrator::submitOnThread(const Submission &submission)
{
    if (m_pool == nullptr)
        m_pool = new QThreadPool(this);

    const int token = ++m_tokens[submission.key];
    Task *task = m_tasks.value(submission.key);
    if (task == nullptr) {
        task = new Task;
        task->key = submission.key;
        task->timer = new QTimer(this);
        task->timer->setSingleShot(true);
        const QString key = submission.key;
        connect(task->timer, &QTimer::timeout, this, [this, key] { fire(key); });
        m_tasks.insert(submission.key, task);
        m_order.append(submission.key);
    }

    task->name = submission.name;
    task->owner = submission.owner;
    task->detail = submission.detail;
    task->work = submission.work;
    task->lane = submission.lane;
    task->deliverTo = submission.deliverTo;
    task->token = token;
    task->timer->stop();

    // Debounce replaces a running scan as well: the old pool task keeps the
    // token it started with, and release() ignores it once this token changes.
    if (submission.debounceMs > 0) {
        task->sleeping = true;
        task->deadline = QDeadlineTimer(submission.debounceMs);
        task->timer->start(submission.debounceMs);
        publish();
        return;
    }

    // Hold resubmitted while the previous process is still the current record:
    // token was incremented above, and the task already existed. `token > 1`
    // is true for a reused task. A brand-new task also has token >= 1 and
    // sleeping false. Both call begin(); Hold's begin() invokes work(), and
    // CheckRunner::start replaces a live process.
    begin(task);
}

void ProcessOrchestrator::cancelOnThread(const QString &key)
{
    ++m_tokens[key];
    removeTask(key, QStringLiteral("Cancelled"));
}

void ProcessOrchestrator::releaseOnThread(const QString &key, int token)
{
    Task *task = m_tasks.value(key);
    if (task == nullptr || task->token != token)
        return;
    removeTask(key, QStringLiteral("Finished"));
}

void ProcessOrchestrator::fire(const QString &key)
{
    Task *task = m_tasks.value(key);
    if (task == nullptr || !task->sleeping)
        return;
    begin(task);
}

void ProcessOrchestrator::begin(Task *task)
{
    task->timer->stop();
    task->sleeping = false;
    publish();

    if (task->lane == ProcessLane::Pool) {
        startPool(task, task->token, task->work);
        return;
    }
    if (task->lane == ProcessLane::Thread) {
        const QString key = task->key;
        const int token = task->token;
        task->work();
        releaseOnThread(key, token);
        return;
    }
    if (task->lane == ProcessLane::Hold) {
        task->work();
        return;
    }

    QObject *target = task->deliverTo.data();
    if (target == nullptr) {
        removeTask(task->key, QStringLiteral("Finished"));
        return;
    }
    const QString key = task->key;
    const int token = task->token;
    const auto work = task->work;
    QMetaObject::invokeMethod(target, [this, key, token, work] {
        m_boundKey = key;
        m_boundToken = token;
        work();
        m_boundKey.clear();
        m_boundToken = 0;
    }, Qt::QueuedConnection);
}

void ProcessOrchestrator::startPool(Task *task, int token, const std::function<void()> &work)
{
    task->sleeping = false;
    publish();
    const QString key = task->key;
    m_pool->start([this, key, token, work] {
        work();
        QMetaObject::invokeMethod(this, [this, key, token] { releaseOnThread(key, token); },
                                  Qt::QueuedConnection);
    });
}

void ProcessOrchestrator::removeTask(const QString &key, const QString &outcome)
{
    Task *task = m_tasks.take(key);
    if (task == nullptr)
        return;
    ProcessRecord record;
    record.name = task->name;
    record.owner = task->owner;
    record.detail = task->detail;
    record.log = task->log;
    record.status = outcome;
    m_recent.prepend(record);
    while (m_recent.size() > 20)
        m_recent.removeLast();
    task->timer->stop();
    task->timer->deleteLater();
    m_order.removeOne(key);
    delete task;
    publish();
}

void ProcessOrchestrator::publish()
{
    if (m_tick == nullptr) {
        m_tick = new QTimer(this);
        m_tick->setInterval(100);
        connect(m_tick, &QTimer::timeout, this, [this] { publish(); });
    }

    QList<ProcessRecord> records;
    bool sleeping = false;
    for (const QString &key : m_order) {
        const Task *task = m_tasks.value(key);
        if (task == nullptr)
            continue;
        ProcessRecord record;
        record.name = task->name;
        record.owner = task->owner;
        record.detail = task->detail;
        record.log = task->log;
        if (task->sleeping) {
            sleeping = true;
            record.status = QStringLiteral("Sleep");
            record.timerMs = qMax(0, int(task->deadline.remainingTime()));
        } else {
            record.status = QStringLiteral("Running");
        }
        records.append(record);
    }
    if (sleeping) {
        if (!m_tick->isActive())
            m_tick->start();
    } else {
        m_tick->stop();
    }
    emit recordsChanged(records, m_recent);
}
