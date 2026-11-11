#include "git/GitDiscovery.h"

#include "io/NamePattern.h"
#include "io/WslPath.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {

bool isRepositoryRoot(const QDir &dir)
{
    return QFileInfo::exists(dir.filePath(QStringLiteral(".git")));
}

// The nearest folder at or above `scope` that holds `.git`. Stops at the
// drive root or the distro root, as the .crlf search does.
QString enclosingRepository(const QString &scope)
{
    QDir dir(scope);
    while (true) {
        if (isRepositoryRoot(dir))
            return dir.absolutePath();
        if (WslPath::isDistroRoot(dir.absolutePath()) || !dir.cdUp())
            return {};
    }
}

} // namespace

QStringList findRepositories(const QString &scope, int depth, const QStringList &hide)
{
    const QString enclosing = enclosingRepository(scope);
    QStringList found;
    QList<QPair<QString, int>> pending{{QDir(scope).absolutePath(), 0}};
    while (!pending.isEmpty()) {
        const auto [path, level] = pending.takeFirst();
        const QDir dir(path);
        if (path != enclosing && isRepositoryRoot(dir))
            found.append(path);
        if (level == depth)
            continue;
        for (const QFileInfo &info : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
            const QString name = info.fileName();
            if (name.startsWith(QLatin1Char('.'))
                || name.compare(QLatin1String("node_modules"), Qt::CaseInsensitive) == 0
                || nameMatchesAny(hide, name))
                continue;
            pending.append({info.absoluteFilePath(), level + 1});
        }
    }
    found.sort(Qt::CaseInsensitive);
    if (!enclosing.isEmpty())
        found.prepend(enclosing);
    return found;
}

QString gitDirectory(const QString &root)
{
    const QString dotGit = QDir(root).filePath(QStringLiteral(".git"));
    if (QFileInfo(dotGit).isDir())
        return dotGit;
    QFile file(dotGit);
    (void)file.open(QIODevice::ReadOnly);
    const QString line = QString::fromUtf8(file.readLine()).trimmed();
    const QString target = line.mid(QStringLiteral("gitdir:").size()).trimmed();
    // Git inside a distro writes a Linux path.
    const WslPath::LinuxPath linux = WslPath::toLinux(root);
    if (linux.valid() && target.startsWith(QLatin1Char('/')))
        return WslPath::toWindows(linux.distro, target);
    return QDir::cleanPath(QDir(root).absoluteFilePath(target));
}
