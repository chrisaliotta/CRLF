#include "io/WorkspaceSeed.h"

#include "io/ProjectCommands.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>

namespace {

// Relative path to source file for every file under `folder`. A later call
// replaces an earlier entry of the same path. Python's cache is not a script.
void collect(QMap<QString, QString> &files, const QString &folder)
{
    const QDir root(folder);
    QDirIterator walk(folder, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (walk.hasNext()) {
        const QString path = walk.next();
        const QString relative = root.relativeFilePath(path);
        if (!relative.contains(QLatin1String("__pycache__")))
            files.insert(relative, path);
    }
}

void copyInto(const QMap<QString, QString> &files, const QString &folder, bool replaceExisting, SeedCounts &counts)
{
    const QDir destination(folder);
    for (auto file = files.constBegin(); file != files.constEnd(); ++file) {
        const QString path = destination.filePath(file.key());
        const bool exists = QFileInfo::exists(path);
        if (exists && !replaceExisting) {
            ++counts.skipped;
            continue;
        }
        if (exists)
            QFile::remove(path);
        destination.mkpath(QFileInfo(path).path());
        if (!QFile::copy(file.value(), path))
            ++counts.failed;
        else
            ++(exists ? counts.replaced : counts.copied);
    }
}

// application.crlf as a workspace file: it runs the copied scripts and reads
// the copied rules. Key files stay in the application folder, so
// ${appDir}/keys is left as written.
bool createWorkspaceFile(const QString &applicationFolder, const QString &target)
{
    QFile source(QDir(applicationFolder).filePath(QLatin1String(kApplicationFile)));
    (void)source.open(QIODevice::ReadOnly);
    QByteArray text = source.readAll();
    text.replace("${programDir}/scripts", ".crlf/scripts");
    text.replace("${appDir}/scripts", ".crlf/scripts");
    text.replace("${appDir}/rules", ".crlf/rules");
    QFile created(QDir(target).filePath(QLatin1String(kWorkspaceFile)));
    return created.open(QIODevice::WriteOnly) && created.write(text) == text.size();
}

} // namespace

SeedCounts seedWorkspace(const QString &applicationFolder, const QString &shippedScripts, const QString &target,
                         bool replaceExisting)
{
    SeedCounts counts;
    if (!settingsFiles(target).isEmpty())
        ++counts.skipped;
    else if (createWorkspaceFile(applicationFolder, target))
        ++counts.copied;
    else
        ++counts.failed;

    const QDir application(QDir(applicationFolder).filePath(QLatin1String(kProjectFolder)));
    const QDir workspace(QDir(target).filePath(QLatin1String(kProjectFolder)));
    QMap<QString, QString> scripts;
    collect(scripts, shippedScripts);
    collect(scripts, application.filePath(QStringLiteral("scripts")));
    copyInto(scripts, workspace.filePath(QStringLiteral("scripts")), replaceExisting, counts);

    QMap<QString, QString> rules;
    collect(rules, application.filePath(QStringLiteral("rules")));
    copyInto(rules, workspace.filePath(QStringLiteral("rules")), replaceExisting, counts);
    return counts;
}
