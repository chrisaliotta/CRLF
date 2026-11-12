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
    bool recorded = true;
};

} // namespace

struct ProcessOrchestrator::Task
{
    QString key;
    QString name;
    QString owner;
    QString detail;
    QString activity;
    QString log;
    // Standard error after the last newline. A status prefix can arrive
    // split across reads, so lines are classified only once complete.
    QString partial;
    std::function<void()> work;
    ProcessLane lane = ProcessLane::Pool;
    QPointer<QObject> deliverTo;
    bool recorded = true;
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
                                 ProcessLane lane, QObject *deliverTo, bool recorded)
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
    submission->recorded = recorded;
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
    static const QString marker = QStringLiteral("CRLF_STATUS:");
    QString pending = task->partial + text;
    pending.remove(QLatin1Char('\r'));
    const int lastNewline = pending.lastIndexOf(QLatin1Char('\n'));
    task->partial = pending.mid(lastNewline + 1);
    if (lastNewline >= 0) {
        const QStringList lines = pending.left(lastNewline).split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            if (line.startsWith(marker))
                task->activity = line.mid(marker.size()).trimmed();
            else
                task->log += line + QLatin1Char('\n');
        }
    }
    int excess = m_unlimitedLog ? 0 : task->log.count(QLatin1Char('\n')) - kMaxLogLines;
    if (excess > 0) {
        int cut = 0;
        while (excess-- > 0)
            cut = task->log.indexOf(QLatin1Char('\n'), cut) + 1;
        task->log.remove(0, cut);
    }
    publish();
}

void ProcessOrchestrator::setUnlimitedLog(bool unlimited)
{
    QMetaObject::invokeMethod(this, [this, unlimited] { m_unlimitedLog = unlimited; }, Qt::QueuedConnection);
}

void ProcessOrchestrator::setActivity(const QString &key, const QString &text)
{
    if (QThread::currentThread() == thread()) {
        setActivityOnThread(key, text);
        return;
    }
    QMetaObject::invokeMethod(this, [this, key, text] { setActivityOnThread(key, text); },
                              Qt::QueuedConnection);
}

void ProcessOrchestrator::setActivityOnThread(const QString &key, const QString &text)
{
    Task *task = m_tasks.value(key);
    if (task == nullptr)
        return;
    task->activity = text;
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
    task->recorded = submission.recorded;
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
    // SyntaxRunner::start replaces a live process.
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
    if (!task->partial.isEmpty())
        task->log += task->partial + QLatin1Char('\n');
    if (task->recorded) {
        ProcessRecord record = recordFor(task);
        record.status = outcome;
        record.serial = ++m_serial;
        m_recent.prepend(record);
        while (m_recent.size() > 20)
            m_recent.removeLast();
    }
    task->timer->stop();
    task->timer->deleteLater();
    m_order.removeOne(key);
    delete task;
    publish();
}

ProcessRecord ProcessOrchestrator::recordFor(const Task *task) const
{
    ProcessRecord record;
    record.key = task->key;
    record.name = task->name;
    record.owner = task->owner;
    record.detail = task->detail;
    record.activity = task->activity;
    record.log = task->log;
    return record;
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
        ProcessRecord record = recordFor(task);
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
