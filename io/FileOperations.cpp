#include "io/FileOperations.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

namespace FileOperations {

QString availableDestination(const QString &source, const QString &directory)
{
    const QFileInfo info(source);
    const QDir target(directory);
    if (!target.exists(info.fileName()))
        return target.filePath(info.fileName());

    // Folders keep dots in their names; only files split off an extension.
    const QString base = info.isDir() ? info.fileName() : info.completeBaseName();
    const QString extension =
        info.isDir() || info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix();
    QString candidate = QStringLiteral("%1 - Copy%2").arg(base, extension);
    for (int n = 2; target.exists(candidate); ++n)
        candidate = QStringLiteral("%1 - Copy (%2)%3").arg(base).arg(n).arg(extension);
    return target.filePath(candidate);
}

bool copy(const QString &source, const QString &destination)
{
    if (!QFileInfo(source).isDir())
        return QFile::copy(source, destination);
    if (!QDir().mkpath(destination))
        return false;
    const QFileInfoList entries = QDir(source).entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        if (!copy(entry.filePath(), QDir(destination).filePath(entry.fileName())))
            return false;
    }
    return true;
}

bool move(const QString &source, const QString &destination)
{
    if (QDir().rename(source, destination))
        return true;
    // QDir::rename cannot move a folder to another volume.
    return QFileInfo(source).isDir() && copy(source, destination)
           && QDir(source).removeRecursively();
}

bool moveToTrash(const QString &path)
{
    return QFile::moveToTrash(path);
}

void revealInFileExplorer(const QString &path)
{
    // explorer.exe parses its own command line and needs the quotes exactly
    // here, so the arguments bypass QProcess's quoting.
    QProcess process;
    process.setProgram(QStringLiteral("explorer.exe"));
    process.setNativeArguments(
        QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(path)));
    process.startDetached();
}

} // namespace FileOperations
