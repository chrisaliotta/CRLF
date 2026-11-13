#pragma once

#include "io/ProjectTrust.h"

#include <QMutex>
#include <QObject>

class ProcessOrchestrator;

// What the UI does once a pin read on the pool has finished.
enum class TrustFollowup { None, Review, Trust, Restrict };

Q_DECLARE_METATYPE(TrustFollowup)

// The project's pin and the stored decision. The directory walk and the
// hashes run on the pool. allows() only reads the result, so the UI thread
// never waits on the disk.
class ProjectTrustGate : public QObject
{
    Q_OBJECT

public:
    explicit ProjectTrustGate(QObject *parent = nullptr);

    void setOrchestrator(ProcessOrchestrator *orchestrator) { m_orchestrator = orchestrator; }
    void setProject(const Project &project);
    // Reads the pin on the pool. A newer schedule replaces one that has not
    // been applied yet.
    void schedule(TrustFollowup followup);
    // True once a pin for the current project has been applied.
    bool ready() const;
    // True when `id` may start. No disk access. A gated id that is not
    // allowed emits blocked().
    bool allows(const QString &id);
    // Writes the restricted record. There is no directory to walk.
    void restrictFolder();

    ProjectPin pin() const;
    TrustDecision decision() const;
    TrustDifference difference() const;

signals:
    void checked(TrustFollowup followup);
    void blocked();

private:
    mutable QMutex m_mutex;
    ProcessOrchestrator *m_orchestrator = nullptr;
    Project m_project;
    ProjectPin m_pin;
    TrustDecision m_decision = TrustDecision::Allow;
    TrustDifference m_difference;
    TrustHashCache m_cache;
    int m_serial = 0;
    bool m_ready = false;
};
