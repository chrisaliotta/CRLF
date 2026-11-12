#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QWinEventNotifier;

// Reports the folders under one root whose entries were added, removed, or
// renamed, by this program or any other. It holds one handle, on the root,
// and asks Windows for the whole subtree. QFileSystemWatcher would need a
// handle per folder, and a handle on a folder stops its parents from being
// renamed or moved for as long as it is held.
//
// A root that cannot be watched reports nothing: a WSL share, where Windows
// delivers no change notices, or any folder on another platform. The owner
// then reads its folders again on a timer.
class FolderWatch : public QObject
{
    Q_OBJECT

public:
    explicit FolderWatch(QObject *parent = nullptr);
    ~FolderWatch() override;

    // Replaces the root. Returns false when it cannot be watched.
    bool watch(const QString &root);
    void stop();

signals:
    // An entry directly inside `directory` appeared, went away, or changed
    // its name. The path uses forward slashes.
    void changed(const QString &directory);
    // More changed than one read could hold. Anything may be out of date.
    void overflowed();

private:
    bool read();
    void take();

    QString m_root;
    QByteArray m_buffer;
    QWinEventNotifier *m_notifier = nullptr;
    void *m_directory = nullptr;
    void *m_event = nullptr;
    // A read is waiting on the handle.
    bool m_reading = false;
    // An OVERLAPPED, kept out of the header with the rest of windows.h.
    QByteArray m_overlapped;
};
