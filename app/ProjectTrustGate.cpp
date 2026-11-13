#include "app/ProjectTrustGate.h"

#include "app/ProcessOrchestrator.h"

#include <QMutexLocker>
#include <QSettings>

namespace {

TrustRecord trustedRecord(const ProjectPin &pin)
{
    TrustRecord record;
    record.state = TrustState::Trusted;
    for (const TrustEntry &entry : pin.entries)
        record.templates.append({entry.id, entry.templateText});
    record.files = pin.files;
    return record;
}

} // namespace

ProjectTrustGate::ProjectTrustGate(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<TrustFollowup>();
}

void ProjectTrustGate::setProject(const Project &project)
{
    QMutexLocker lock(&m_mutex);
    m_project = project;
    m_ready = false;
}

void ProjectTrustGate::schedule(TrustFollowup followup)
{
    Project project;
    TrustHashCache cache;
    int serial = 0;
    {
        QMutexLocker lock(&m_mutex);
        project = m_project;
        cache = m_cache;
        serial = ++m_serial;
    }
    const QString root = project.directory;
    m_orchestrator->submit(QStringLiteral("trust"), QStringLiteral("Workspace scripts"), root, 0, root,
                           [this, project, cache, serial, followup]() mutable {
                               const ProjectPin pin = pinProject(project, &cache);
                               QSettings settings;
                               TrustDifference difference;
                               TrustDecision decision = decideTrust(pin, loadTrust(settings, project.directory), &difference);
                               {
                                   QMutexLocker lock(&m_mutex);
                                   if (serial != m_serial)
                                       return;
                               }
                               if (followup == TrustFollowup::Trust) {
                                   saveTrust(settings, project.directory, trustedRecord(pin));
                                   decision = TrustDecision::Allow;
                                   difference = {};
                               }
                               QMetaObject::invokeMethod(this, [this, pin, cache, decision, difference, serial, followup] {
                                   {
                                       QMutexLocker lock(&m_mutex);
                                       if (serial != m_serial)
                                           return;
                                       m_cache = cache;
                                       m_pin = pin;
                                       m_decision = decision;
                                       m_difference = difference;
                                       m_ready = true;
                                   }
                                   emit checked(followup);
                               }, Qt::QueuedConnection);
                           }, ProcessLane::Pool, nullptr, false);
}

bool ProjectTrustGate::ready() const
{
    QMutexLocker lock(&m_mutex);
    return m_ready;
}

bool ProjectTrustGate::allows(const QString &id)
{
    bool allowed = false;
    {
        QMutexLocker lock(&m_mutex);
        // The pin is not in yet. Refuse rather than read the disk here.
        if (!m_ready)
            return false;
        allowed = m_decision == TrustDecision::Allow || !m_pin.gates(id);
    }
    if (!allowed)
        emit blocked();
    return allowed;
}

void ProjectTrustGate::restrictFolder()
{
    QString root;
    {
        QMutexLocker lock(&m_mutex);
        root = m_project.directory;
        m_decision = TrustDecision::Restricted;
        m_difference = {};
        m_ready = true;
    }
    TrustRecord record;
    record.state = TrustState::Restricted;
    QSettings settings;
    saveTrust(settings, root, record);
}

ProjectPin ProjectTrustGate::pin() const
{
    QMutexLocker lock(&m_mutex);
    return m_pin;
}

TrustDecision ProjectTrustGate::decision() const
{
    QMutexLocker lock(&m_mutex);
    return m_decision;
}

TrustDifference ProjectTrustGate::difference() const
{
    QMutexLocker lock(&m_mutex);
    return m_difference;
}
