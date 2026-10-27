#pragma once

#include <QLocalServer>
#include <QObject>
#include <QStringList>

// Ensures exactly one running CRLF owns the persisted session. A second
// launch forwards its file arguments to the primary instance and exits.
// Besides matching notepad-replacement UX (double-clicking a file opens a tab
// in the existing window), this prevents two instances from overwriting each
// other's session file, which would silently lose unsaved edits.
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(QObject *parent = nullptr);

    bool isPrimary() const { return m_primary; }
    // Called from the secondary instance before it exits.
    void forwardToPrimary(const QStringList &paths) const;

signals:
    // Emitted in the primary when a secondary launch hands over its paths.
    // The list is empty when the app was launched with no files.
    void pathsReceived(const QStringList &paths);

private:
    QLocalServer m_server;
    bool m_primary = false;
};
