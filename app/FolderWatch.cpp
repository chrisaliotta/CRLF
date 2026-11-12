#include "app/FolderWatch.h"

#include "io/WslPath.h"

#include <QDir>
#include <QSet>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <QWinEventNotifier>
#include <windows.h>
#endif

namespace {

// Large enough that an ordinary burst, such as a branch switch, fits.
constexpr int kBufferBytes = 64 * 1024;

} // namespace

FolderWatch::FolderWatch(QObject *parent)
    : QObject(parent)
{
}

FolderWatch::~FolderWatch()
{
    stop();
}

#ifdef Q_OS_WIN

bool FolderWatch::watch(const QString &root)
{
    stop();
    // The share accepts the handle and then never reports a change.
    if (WslPath::toLinux(root).valid())
        return false;
    const QString native = QDir::toNativeSeparators(root);
    HANDLE directory = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), FILE_LIST_DIRECTORY,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                   FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (directory == INVALID_HANDLE_VALUE)
        return false;

    m_root = QDir::cleanPath(root);
    m_directory = directory;
    m_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_buffer.resize(kBufferBytes);
    m_overlapped = QByteArray(int(sizeof(OVERLAPPED)), '\0');
    reinterpret_cast<OVERLAPPED *>(m_overlapped.data())->hEvent = static_cast<HANDLE>(m_event);
    m_notifier = new QWinEventNotifier(static_cast<HANDLE>(m_event), this);
    connect(m_notifier, &QWinEventNotifier::activated, this, &FolderWatch::take);
    if (read())
        return true;
    stop();
    return false;
}

void FolderWatch::stop()
{
    if (m_directory == nullptr)
        return;
    delete m_notifier;
    m_notifier = nullptr;
    // A cancelled read still owns the buffer until it reports back.
    if (m_reading) {
        CancelIo(static_cast<HANDLE>(m_directory));
        DWORD bytes = 0;
        GetOverlappedResult(static_cast<HANDLE>(m_directory), reinterpret_cast<OVERLAPPED *>(m_overlapped.data()),
                            &bytes, TRUE);
        m_reading = false;
    }
    CloseHandle(static_cast<HANDLE>(m_directory));
    CloseHandle(static_cast<HANDLE>(m_event));
    m_directory = nullptr;
    m_event = nullptr;
    m_root.clear();
}

bool FolderWatch::read()
{
    ResetEvent(static_cast<HANDLE>(m_event));
    // Names only: the tree shows which entries exist, not what they hold.
    m_reading = ReadDirectoryChangesW(static_cast<HANDLE>(m_directory), m_buffer.data(), DWORD(m_buffer.size()), TRUE,
                                      FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME, nullptr,
                                      reinterpret_cast<OVERLAPPED *>(m_overlapped.data()), nullptr)
                != 0;
    return m_reading;
}

void FolderWatch::take()
{
    DWORD bytes = 0;
    const bool ok = GetOverlappedResult(static_cast<HANDLE>(m_directory),
                                        reinterpret_cast<OVERLAPPED *>(m_overlapped.data()), &bytes, FALSE)
                    != 0;
    m_reading = false;
    QSet<QString> directories;
    // A finished read with nothing in it means the buffer overflowed.
    if (ok && bytes > 0) {
        const char *record = m_buffer.constData();
        while (true) {
            const auto *info = reinterpret_cast<const FILE_NOTIFY_INFORMATION *>(record);
            const QString name = QDir::fromNativeSeparators(
                QString::fromWCharArray(info->FileName, int(info->FileNameLength / sizeof(WCHAR))));
            const int slash = name.lastIndexOf(QLatin1Char('/'));
            directories.insert(slash < 0 ? m_root : m_root + QLatin1Char('/') + name.left(slash));
            if (info->NextEntryOffset == 0)
                break;
            record += info->NextEntryOffset;
        }
    }
    // The root itself was removed or its volume went away: nothing more will come.
    const bool reading = read();
    for (const QString &directory : std::as_const(directories))
        emit changed(directory);
    if (ok && bytes == 0)
        emit overflowed();
    if (!reading)
        stop();
}

#else

bool FolderWatch::watch(const QString &)
{
    return false;
}

void FolderWatch::stop()
{
}

bool FolderWatch::read()
{
    return false;
}

void FolderWatch::take()
{
}

#endif
