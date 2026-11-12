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

bool writeFile(const QString &path, const QByteArray &text)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(text) == text.size();
}

// The application file as the workspace's own: it runs the copied scripts
// and reads the copied rules. Key files stay in the application folder, so
// ${appDir}/keys is left as written.
QByteArray workspaceCopy(const QString &applicationFile)
{
    QFile source(applicationFile);
    (void)source.open(QIODevice::ReadOnly);
    QByteArray text = source.readAll();
    text.replace("${programDir}/scripts", ".crlf/scripts");
    text.replace("${appDir}/scripts", ".crlf/scripts");
    text.replace("${appDir}/rules", ".crlf/rules");
    return text;
}

// The two settings files in the root. application.crlf is the copy, and is
// replaced on request like a script. workspace.crlf is the user's: it is
// created empty when the folder has no settings of its own, and an existing
// one is left as it is.
void copySettings(const QString &applicationFile, const QString &target, bool replaceExisting, SeedCounts &counts)
{
    const QDir root(target);
    const QString copy = root.filePath(QLatin1String(kApplicationFile));
    const bool exists = QFileInfo::exists(copy);
    // settingsFiles puts application.crlf last, so anything else is the workspace's own.
    const QStringList files = settingsFiles(target);
    const bool hasOwn = files.size() > (exists ? 1 : 0);

    if (exists && !replaceExisting)
        ++counts.skipped;
    else if (!writeFile(copy, workspaceCopy(applicationFile)))
        ++counts.failed;
    else
        ++(exists ? counts.replaced : counts.copied);

    if (hasOwn)
        ++counts.skipped;
    else if (!writeFile(root.filePath(QLatin1String(kWorkspaceFile)), QByteArrayLiteral("{\n}\n")))
        ++counts.failed;
    else
        ++counts.copied;
}

} // namespace

SeedCounts seedWorkspace(const QString &applicationFile, const QString &shippedScripts, const QString &target,
                         bool replaceExisting)
{
    SeedCounts counts;
    copySettings(applicationFile, target, replaceExisting, counts);

    const QString applicationFolder = QFileInfo(applicationFile).absolutePath();
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
