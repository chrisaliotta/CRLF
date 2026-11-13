// Confirms how a project's settings are put together from its .crlf files:
// the merge rules and `inherit`, which files are found and in what order,
// the rename of a legacy `.crlf` file, where an edited setting is saved, the
// command line and path variables for Windows and for a WSL distro, and the
// copy of the application's files into a workspace. Every case works in a
// temporary folder that stands in for the workspace, the application folder,
// and the install folder, so the real %APPDATA% is never read or written.
// See docs/crlf.md.

#include "io/CommandVariables.h"
#include "io/ProjectCommands.h"
#include "io/ProjectMerge.h"
#include "io/ProjectTrust.h"
#include "io/WorkspaceSeed.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace {

QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(text).object();
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    (void)file.open(QIODevice::ReadOnly);
    return file.readAll();
}

QJsonObject readObject(const QString &path)
{
    return QJsonDocument::fromJson(readFile(path)).object();
}

// The `name` of each entry, in order.
QStringList names(const QJsonValue &list)
{
    QStringList result;
    for (const QJsonValue &entry : list.toArray())
        result.append(entry.toObject().value(QLatin1String("name")).toString());
    return result;
}

QStringList commandNames(const Project &project)
{
    QStringList result;
    for (const ProjectCommand &command : project.commands)
        result.append(command.name);
    return result;
}

QStringList groupNames(const Project &project)
{
    QStringList result;
    for (const FileGroup &group : project.groups)
        result.append(group.name);
    return result;
}

QStringList entryFiles(const ProjectPin &pin, const QString &id)
{
    for (const TrustEntry &entry : pin.entries) {
        if (entry.id == id)
            return entry.files;
    }
    return {};
}

} // namespace

class ProjectTests : public QObject
{
    Q_OBJECT

private slots:
    // Each case gets its own workspace, application folder, and install
    // folder. The workspace always holds a settings file before a project is
    // looked up, so the search never walks out of the temporary folder.
    void init()
    {
        m_directory = std::make_unique<QTemporaryDir>();
        QVERIFY(m_directory->isValid());
        const QDir root(m_directory->path());
        m_work = root.absoluteFilePath(QStringLiteral("work"));
        m_application = root.absoluteFilePath(QStringLiteral("application"));
        m_program = root.absoluteFilePath(QStringLiteral("program"));
        for (const QString &folder : {m_work, m_application, m_program})
            QVERIFY(QDir().mkpath(folder));
    }

    // Two files may define the same command, agent, or virtual folder. The
    // name is the menu label and the process name, so only one can exist:
    // the earlier file's. A group is one folder in one place, so the same
    // name under another path is a different group.
    void mergeKeepsTheFirstDefinitionOfAName()
    {
        const QJsonObject merged = mergeProjects(
            {json(R"({"commands": [{"name": "Build", "command": "first"}, {"name": "Test", "command": "t"}],
                      "agents": [{"name": "Claude", "command": "first"}],
                      "groups": [{"name": "Docs", "files": ["a.md"]}]})"),
             json(R"({"commands": [{"name": "Build", "command": "second"}, {"name": "Run", "command": "r"}],
                      "agents": [{"name": "Claude", "command": "second"}, {"name": "Local", "command": "l"}],
                      "groups": [{"name": "Docs", "files": ["b.md"]},
                                 {"name": "Docs", "path": "src", "files": ["c.md"]}]})")},
            {});
        QCOMPARE(names(merged.value(QLatin1String("commands"))),
                 QStringList({QStringLiteral("Build"), QStringLiteral("Test"), QStringLiteral("Run")}));
        QCOMPARE(merged.value(QLatin1String("commands")).toArray().at(0).toObject().value(QLatin1String("command")),
                 QJsonValue(QStringLiteral("first")));
        QCOMPARE(names(merged.value(QLatin1String("agents"))),
                 QStringList({QStringLiteral("Claude"), QStringLiteral("Local")}));
        QCOMPARE(merged.value(QLatin1String("agents")).toArray().at(0).toObject().value(QLatin1String("command")),
                 QJsonValue(QStringLiteral("first")));
        const QJsonArray groups = merged.value(QLatin1String("groups")).toArray();
        QCOMPARE(groups.size(), 2);
        QCOMPARE(groups.at(0).toObject().value(QLatin1String("files")), QJsonValue(QJsonArray{QStringLiteral("a.md")}));
        QCOMPARE(groups.at(1).toObject().value(QLatin1String("path")), QJsonValue(QStringLiteral("src")));
    }

    // `hide`, `indent`, and `icons` are built up across files instead of
    // being replaced: a local file can hide one more folder or change the
    // tab size without repeating the shared file. Each single field comes
    // from the first file that sets it, and the lists are joined.
    void mergeCombinesHideIndentAndIcons()
    {
        const QJsonObject merged = mergeProjects(
            {json(R"({"hide": ["build", "dist"],
                      "indent": {"size": 2, "types": [{"extensions": ["py"], "size": 4}]},
                      "icons": {"rules": [{"kind": "file", "pattern": "a", "from": "Seti", "icon": "x"}]}})"),
             json(R"({"hide": ["dist", "*.log"],
                      "indent": {"style": "spaces", "size": 8, "keep": true, "types": [{"extensions": ["go"]}]},
                      "icons": {"theme": "Seti",
                                "rules": [{"kind": "file", "pattern": "b", "from": "Seti", "icon": "y"}]}})")},
            {});
        QCOMPARE(merged.value(QLatin1String("hide")),
                 QJsonValue(QJsonArray{QStringLiteral("build"), QStringLiteral("dist"), QStringLiteral("*.log")}));
        const QJsonObject indent = merged.value(QLatin1String("indent")).toObject();
        QCOMPARE(indent.value(QLatin1String("size")).toInt(), 2);
        QCOMPARE(indent.value(QLatin1String("style")).toString(), QStringLiteral("spaces"));
        QCOMPARE(indent.value(QLatin1String("keep")).toBool(), true);
        QCOMPARE(indent.value(QLatin1String("types")).toArray().size(), 2);
        const QJsonObject icons = merged.value(QLatin1String("icons")).toObject();
        QCOMPARE(icons.value(QLatin1String("theme")).toString(), QStringLiteral("Seti"));
        QCOMPARE(icons.value(QLatin1String("rules")).toArray().size(), 2);
    }

    // A service or a switch is one decision, so it is not blended: the
    // first file that has the key supplies all of it, and a later file's
    // fields for that key are ignored.
    void mergeTakesEveryOtherKeyWholeFromTheFirstFile()
    {
        const QJsonObject merged = mergeProjects(
            {json(R"({"syntax": {"enabled": true, "command": "first"}, "navigation": false})"),
             json(R"({"syntax": {"enabled": false, "command": "second", "cwd": "tools"},
                      "navigation": true, "unlimitedLog": true, "git": {"depth": 2}})")},
            {});
        QCOMPARE(merged.value(QLatin1String("syntax")), QJsonValue(json(R"({"enabled": true, "command": "first"})")));
        QCOMPARE(merged.value(QLatin1String("navigation")).toBool(), false);
        QCOMPARE(merged.value(QLatin1String("unlimitedLog")).toBool(), true);
        QCOMPARE(merged.value(QLatin1String("git")), QJsonValue(json(R"({"depth": 2})")));
    }

    // application.crlf is the user's setup for folders that have none of
    // their own. A workspace replaces it entirely unless a workspace file
    // asks for it, so a project is not changed by what one user keeps there.
    void applicationAppliesAloneOrNotAtAll()
    {
        const QJsonObject application = json(R"({"commands": [{"name": "Mine", "command": "m"}], "hide": ["tmp"]})");
        QCOMPARE(mergeProjects({}, application), application);
        const QJsonObject workspace = json(R"({"commands": [{"name": "Build", "command": "b"}]})");
        QCOMPARE(mergeProjects({workspace}, application), workspace);
        QVERIFY(!inheritsApplication({workspace}));
    }

    // `inherit: true` puts application.crlf last in the merge: it supplies
    // the keys the workspace leaves out, its lists follow the workspace's,
    // and the workspace wins a shared name. `inherit` is read from the first
    // workspace file that has it and is not a setting itself.
    void inheritTrueMergesTheApplicationLast()
    {
        const QJsonObject application = json(
            R"({"commands": [{"name": "Build", "command": "application"}, {"name": "Mine", "command": "m"}],
                "indent": {"style": "tab", "size": 8}, "git": true, "inherit": false})");
        const QList<QJsonObject> workspace = {
            json(R"({"inherit": true, "commands": [{"name": "Build", "command": "workspace"}],
                     "indent": {"size": 2}})"),
            json(R"({"inherit": false})")};
        QVERIFY(inheritsApplication(workspace));
        const QJsonObject merged = mergeProjects(workspace, application);
        QCOMPARE(names(merged.value(QLatin1String("commands"))),
                 QStringList({QStringLiteral("Build"), QStringLiteral("Mine")}));
        QCOMPARE(merged.value(QLatin1String("commands")).toArray().at(0).toObject().value(QLatin1String("command")),
                 QJsonValue(QStringLiteral("workspace")));
        QCOMPARE(merged.value(QLatin1String("indent")), QJsonValue(json(R"({"size": 2, "style": "tab"})")));
        QCOMPARE(merged.value(QLatin1String("git")).toBool(), true);
        QVERIFY(!merged.contains(QLatin1String("inherit")));
    }

    // A list of keys takes only those from application.crlf, so a project
    // can share the user's agents without also taking the user's commands.
    void inheritListTakesOnlyTheListedKeys()
    {
        const QJsonObject application = json(
            R"({"commands": [{"name": "Mine", "command": "m"}], "agents": [{"name": "GPT", "command": "g"}],
                "indent": {"size": 8}, "git": true})");
        const QJsonObject merged = mergeProjects(
            {json(R"({"inherit": ["agents", "indent"], "agents": [{"name": "Local", "command": "l"}]})")},
            application);
        QCOMPARE(names(merged.value(QLatin1String("agents"))),
                 QStringList({QStringLiteral("Local"), QStringLiteral("GPT")}));
        QCOMPARE(merged.value(QLatin1String("indent")), QJsonValue(json(R"({"size": 8})")));
        QVERIFY(!merged.contains(QLatin1String("commands")));
        QVERIFY(!merged.contains(QLatin1String("git")));
    }

    // Which file wins must not depend on the order the file system lists
    // them in. The files are taken by name, ignoring case, so local.crlf is
    // read before workspace.crlf on every machine. A file that is not JSON
    // is named in `error`, stays in `files` so a fix is noticed, and does not
    // stop the others from applying.
    void filesAreMergedInNameOrderAndABrokenFileIsSkipped()
    {
        const QDir work(m_work);
        writeFile(work.filePath(QStringLiteral("Workspace.crlf")),
                  R"({"commands": [{"name": "Build", "command": "shared"}, {"name": "Test", "command": "t"}]})");
        writeFile(work.filePath(QStringLiteral("local.crlf")), R"([{"name": "Build", "command": "mine"}])");
        writeFile(work.filePath(QStringLiteral("broken.crlf")), "{ not json");

        const Project project = projectFor(m_work, appFile());
        QCOMPARE(project.directory, m_work);
        QCOMPARE(project.files, QStringList({work.filePath(QStringLiteral("broken.crlf")),
                                             work.filePath(QStringLiteral("local.crlf")),
                                             work.filePath(QStringLiteral("Workspace.crlf"))}));
        QCOMPARE(project.error, QStringLiteral("Couldn't read broken.crlf"));
        QCOMPARE(commandNames(project), QStringList({QStringLiteral("Build"), QStringLiteral("Test")}));
        QCOMPARE(project.commands.at(0).command, QStringLiteral("mine"));
    }

    // An entry in application.crlf is written once and used in every
    // workspace, so its relative `cwd` and `watch` mean the workspace it is
    // running for, not the application folder. The inherited file is listed
    // in `files` so an edit to it reloads the project.
    void inheritedEntryResolvesAgainstTheWorkspaceRoot()
    {
        writeFile(QDir(m_work).filePath(QLatin1String(kWorkspaceFile)), R"({"inherit": true})");
        writeFile(QDir(m_application).filePath(QLatin1String(kApplicationFile)),
                  R"({"commands": [{"name": "Build", "command": "make", "cwd": "build"}],
                      "agents": [{"name": "GPT", "command": "agent", "watch": ["tools/agent.py"]}],
                      "groups": [{"name": "Docs", "path": "docs", "files": ["*.md"]}]})");

        const Project project = projectFor(m_work, appFile());
        QCOMPARE(project.files, QStringList({QDir(m_work).filePath(QLatin1String(kWorkspaceFile)),
                                             QDir(m_application).filePath(QLatin1String(kApplicationFile))}));
        QCOMPARE(project.commands.at(0).workingDirectory, QDir(m_work).filePath(QStringLiteral("build")));
        QCOMPARE(project.agents.at(0).process.workingDirectory, m_work);
        QCOMPARE(project.agents.at(0).process.watch,
                 QStringList({QDir(m_work).filePath(QStringLiteral("tools/agent.py"))}));
        QCOMPARE(project.groups.at(0).path, QStringLiteral("docs"));
    }

    // With no workspace file, application.crlf is the whole project. There
    // is no project root then, and a relative path has to mean something:
    // the folder that was opened, or the application folder when the search
    // did not start at a folder.
    void applicationAloneResolvesAgainstTheOpenedFolder()
    {
        writeFile(QDir(m_application).filePath(QLatin1String(kApplicationFile)),
                  R"({"commands": [{"name": "Build", "command": "make", "cwd": "build"}]})");
        const Project project = projectFor(QString(), appFile());
        QCOMPARE(project.directory, QString());
        QCOMPARE(project.files, QStringList({QDir(m_application).filePath(QLatin1String(kApplicationFile))}));
        QCOMPARE(project.commands.at(0).workingDirectory, QDir(m_application).filePath(QStringLiteral("build")));

        // The application folder holds a .crlf file and is still not a
        // project: the search passes it and finds the folder above.
        const QDir above(QDir(m_directory->path()).absolutePath());
        writeFile(above.filePath(QStringLiteral("outer.crlf")), R"([{"name": "Outer", "command": "o"}])");
        const Project opened = projectFor(m_application, appFile());
        QCOMPARE(opened.directory, above.path());
        QCOMPARE(commandNames(opened), QStringList({QStringLiteral("Outer")}));
    }

    // `.crlf` was the project file's whole name. That name is now the
    // project's folder, where an agent keeps its index, and a folder cannot
    // be created while a file has the name. The file is renamed the first
    // time the project loads, and the load reports both names so an open tab
    // can follow it.
    void legacyFileIsRenamedToWorkspace()
    {
        const QDir work(m_work);
        writeFile(work.filePath(QStringLiteral(".crlf")), R"({"commands": [{"name": "Build", "command": "b"}]})");

        const Project project = projectFor(m_work, appFile());
        QVERIFY(!QFileInfo::exists(work.filePath(QStringLiteral(".crlf"))));
        QCOMPARE(project.renamedFrom, work.filePath(QStringLiteral(".crlf")));
        QCOMPARE(project.renamedTo, work.filePath(QStringLiteral("workspace.crlf")));
        QCOMPARE(project.files, QStringList({work.filePath(QStringLiteral("workspace.crlf"))}));
        QCOMPARE(commandNames(project), QStringList({QStringLiteral("Build")}));

        // The next load finds nothing to rename.
        QCOMPARE(projectFor(m_work, appFile()).renamedFrom, QString());
    }

    // A project that already has workspace.crlf keeps it. The legacy file
    // still has to give up its name, so it becomes legacy.crlf, which sorts
    // first and therefore keeps winning as it did before.
    void legacyFileIsRenamedToLegacyWhenWorkspaceExists()
    {
        const QDir work(m_work);
        writeFile(work.filePath(QStringLiteral(".crlf")), R"({"commands": [{"name": "Build", "command": "old"}]})");
        writeFile(work.filePath(QStringLiteral("workspace.crlf")),
                  R"({"commands": [{"name": "Build", "command": "new"}]})");

        const Project project = projectFor(m_work, appFile());
        QCOMPARE(project.renamedTo, work.filePath(QStringLiteral("legacy.crlf")));
        QCOMPARE(project.files, QStringList({work.filePath(QStringLiteral("legacy.crlf")),
                                             work.filePath(QStringLiteral("workspace.crlf"))}));
        QCOMPARE(project.commands.at(0).command, QStringLiteral("old"));
        QCOMPARE(readObject(work.filePath(QStringLiteral("workspace.crlf")))
                     .value(QLatin1String("commands")).toArray().at(0).toObject().value(QLatin1String("command")),
                 QJsonValue(QStringLiteral("new")));
    }

    // A file deep in a project belongs to that project. The search walks up
    // from the file and stops at the first folder with a settings file, so a
    // nested project is found before the one that contains it. The folder
    // named .crlf holds no settings and is not what the search looks for.
    void nearestParentFolderWithASettingsFileIsTheProject()
    {
        const QDir work(m_work);
        writeFile(work.filePath(QStringLiteral("workspace.crlf")), R"([{"name": "Outer", "command": "o"}])");
        writeFile(work.filePath(QStringLiteral("api/api.crlf")), R"([{"name": "Inner", "command": "i"}])");
        writeFile(work.filePath(QStringLiteral("api/src/deep/main.cpp")), "int main() {}");
        writeFile(work.filePath(QStringLiteral("web/.crlf/rules/style.md")), "rule");
        writeFile(work.filePath(QStringLiteral("web/index.html")), "<html>");

        const Project inner = projectFor(work.filePath(QStringLiteral("api/src/deep/main.cpp")), appFile());
        QCOMPARE(inner.directory, work.filePath(QStringLiteral("api")));
        QCOMPARE(commandNames(inner), QStringList({QStringLiteral("Inner")}));

        const Project outer = projectFor(work.filePath(QStringLiteral("web")), appFile());
        QCOMPARE(outer.directory, m_work);
        QCOMPARE(commandNames(outer), QStringList({QStringLiteral("Outer")}));
    }

    // The explorer edits the merged list of virtual folders. Saving it must
    // leave one file owning `groups`: the first that had it. The later files
    // lose the key, or their old entries would come back in the next merge.
    // An entry that only came from application.crlf is not written into the
    // workspace, and application.crlf itself is never changed.
    void savingGroupsWritesTheOwningFileOnly()
    {
        const QDir work(m_work);
        const QString first = work.filePath(QStringLiteral("local.crlf"));
        const QString second = work.filePath(QStringLiteral("workspace.crlf"));
        const QString application = QDir(m_application).filePath(QLatin1String(kApplicationFile));
        writeFile(work.filePath(QStringLiteral("a.crlf")), R"({"inherit": true, "hide": ["build"]})");
        writeFile(first, R"({"groups": [{"name": "Docs", "files": ["*.md"]}], "note": "kept"})");
        writeFile(second, R"({"groups": [{"name": "Old", "files": ["x"]}], "commands": []})");
        writeFile(application, R"({"groups": [{"name": "Shared", "files": ["LICENSE"]}]})");
        const QByteArray applicationBefore = readFile(application);

        Project project = projectFor(m_work, appFile());
        QCOMPARE(groupNames(project),
                 QStringList({QStringLiteral("Docs"), QStringLiteral("Old"), QStringLiteral("Shared")}));
        FileGroup added;
        added.name = QStringLiteral("New");
        added.files = {QStringLiteral("TODO.md")};
        project.groups.append(added);
        QVERIFY(saveProjectGroups(m_work, project.groups, appFile()));

        QCOMPARE(names(readObject(first).value(QLatin1String("groups"))),
                 QStringList({QStringLiteral("Docs"), QStringLiteral("Old"), QStringLiteral("New")}));
        QCOMPARE(readObject(first).value(QLatin1String("note")).toString(), QStringLiteral("kept"));
        QCOMPARE(readObject(second), json(R"({"commands": []})"));
        QCOMPARE(readObject(work.filePath(QStringLiteral("a.crlf"))), json(R"({"inherit": true, "hide": ["build"]})"));
        QCOMPARE(readFile(application), applicationBefore);
        QCOMPARE(groupNames(projectFor(m_work, appFile())),
                 QStringList({QStringLiteral("Docs"), QStringLiteral("Old"), QStringLiteral("New"),
                              QStringLiteral("Shared")}));
    }

    // A folder with no settings file runs on application.crlf alone. The
    // first virtual folder saved there creates workspace.crlf, and a
    // workspace file replaces the application's settings. The new file sets
    // `inherit` so the commands and agents that applied a moment ago still
    // do, and the application's own groups are not copied into it.
    void savingIntoAFolderWithoutSettingsKeepsTheApplication()
    {
        writeFile(QDir(m_application).filePath(QLatin1String(kApplicationFile)),
                  R"({"commands": [{"name": "Mine", "command": "m"}],
                      "groups": [{"name": "Shared", "files": ["LICENSE"]}]})");
        FileGroup shared;
        shared.name = QStringLiteral("Shared");
        shared.files = {QStringLiteral("LICENSE")};
        FileGroup added;
        added.name = QStringLiteral("New");
        added.files = {QStringLiteral("TODO.md")};
        QVERIFY(saveProjectGroups(m_work, {shared, added}, appFile()));

        const QJsonObject created = readObject(QDir(m_work).filePath(QLatin1String(kWorkspaceFile)));
        QCOMPARE(created.value(QLatin1String("inherit")).toBool(), true);
        QCOMPARE(names(created.value(QLatin1String("groups"))), QStringList({QStringLiteral("New")}));
        const Project project = projectFor(m_work, appFile());
        QCOMPARE(commandNames(project), QStringList({QStringLiteral("Mine")}));
        QCOMPARE(groupNames(project), QStringList({QStringLiteral("New"), QStringLiteral("Shared")}));
    }

    // One application entry has to start on Windows and inside a WSL
    // distro, where the interpreter has another name. The working directory
    // decides which line runs, because that is what decides where the
    // process runs. A plain string serves both.
    void commandObjectFollowsTheWorkingDirectory()
    {
        const QJsonValue command = json(R"({"command": {"windows": "python agent.py", "wsl": "python3 agent.py"}})")
                                       .value(QLatin1String("command"));
        QCOMPARE(commandFor(command, QStringLiteral("C:/work/app")), QStringLiteral("python agent.py"));
        QCOMPARE(commandFor(command, QStringLiteral("//wsl.localhost/Ubuntu/home/me/app")),
                 QStringLiteral("python3 agent.py"));
        QCOMPARE(commandFor(command, QStringLiteral("\\\\wsl$\\Ubuntu\\home\\me")), QStringLiteral("python3 agent.py"));
        QCOMPARE(commandFor(QJsonValue(QStringLiteral("make")), QStringLiteral("//wsl.localhost/Ubuntu/home/me")),
                 QStringLiteral("make"));

        // The same choice, made where a project is read: a `cwd` inside a
        // distro takes the `wsl` line.
        writeFile(QDir(m_work).filePath(QLatin1String(kWorkspaceFile)),
                  R"({"commands": [
                        {"name": "Here", "command": {"windows": "dir", "wsl": "ls"}},
                        {"name": "There", "command": {"windows": "dir", "wsl": "ls"},
                         "cwd": "\\\\wsl.localhost\\Ubuntu\\home\\me"}],
                      "syntax": {"enabled": true, "command": {"windows": "check.cmd", "wsl": "./check.sh"}}})");
        const Project project = projectFor(m_work, appFile());
        QCOMPARE(project.commands.at(0).command, QStringLiteral("dir"));
        QCOMPARE(project.commands.at(1).command, QStringLiteral("ls"));
        QCOMPARE(project.syntax.command, QStringLiteral("check.cmd"));
    }

    // The application's scripts and keys live on a Windows drive. A command
    // that runs in a distro can only reach them through the drive's mount,
    // so ${appDir} and ${programDir} are handed over as /mnt paths, as the
    // ${buffer} file already was. A path inside the distro is its Linux
    // path, and a Windows working directory changes nothing.
    void pathVariablesBecomeLinuxPathsForADistro()
    {
        CommandVariables vars;
        vars.appDir = QStringLiteral("C:/Users/me/AppData/Roaming/CRLF/CRLF/.crlf");
        vars.programDir = QStringLiteral("C:\\Program Files\\CRLF");
        vars.buffer = QStringLiteral("D:/Temp/crlf-buffer.txt");
        vars.project = QStringLiteral("//wsl.localhost/Ubuntu/home/me/app");
        vars.file = QStringLiteral("//wsl.localhost/Ubuntu/home/me/app/main.py");
        vars.fileName = QStringLiteral("main.py");

        const CommandVariables distro =
            launchVariables(vars, QStringLiteral("//wsl.localhost/Ubuntu/home/me/app/tools"));
        QCOMPARE(distro.appDir, QStringLiteral("/mnt/c/Users/me/AppData/Roaming/CRLF/CRLF/.crlf"));
        QCOMPARE(distro.programDir, QStringLiteral("/mnt/c/Program Files/CRLF"));
        QCOMPARE(distro.buffer, QStringLiteral("/mnt/d/Temp/crlf-buffer.txt"));
        QCOMPARE(distro.project, QStringLiteral("/home/me/app"));
        QCOMPARE(distro.file, QStringLiteral("/home/me/app/main.py"));
        QCOMPARE(distro.cwd, QStringLiteral("/home/me/app/tools"));
        QCOMPARE(expandCommand(QStringLiteral("python3 \"${appDir}/scripts/a.py\" ${fileName}"), distro),
                 QStringLiteral("python3 \"/mnt/c/Users/me/AppData/Roaming/CRLF/CRLF/.crlf/scripts/a.py\" main.py"));

        const CommandVariables windows = launchVariables(vars, QStringLiteral("C:/work/app"));
        QCOMPARE(windows.appDir, vars.appDir);
        QCOMPARE(windows.programDir, vars.programDir);
        QCOMPARE(windows.cwd, QStringLiteral("C:/work/app"));
    }

    // The application folder gets each shipped file it lacks: the configured
    // application.crlf, blank.crlf, and the price list the shipped agents
    // read, with the folders for the user's own scripts, keys, and rules. A
    // file the user has is theirs and is never replaced, which matters most
    // for the price list they keep current, and a folder seeded by an
    // earlier version still gains the files added since.
    void seedingAddsTheShippedFilesTheFolderLacks()
    {
        const QDir program(m_program);
        const QDir application(m_application);
        const QString prices = application.filePath(QStringLiteral(".crlf/prices.json"));
        writeFile(program.filePath(QLatin1String(kApplicationFile)), "configured");
        writeFile(program.filePath(QLatin1String(kBlankFile)), "{}");
        writeFile(program.filePath(QLatin1String(kPricesFile)), "shipped prices");

        QCOMPARE(seedApplicationFolder(m_application, m_program), QString());
        QCOMPARE(readFile(appFile()), QByteArray("configured"));
        QCOMPARE(readFile(application.filePath(QLatin1String(kBlankFile))), QByteArray("{}"));
        QCOMPARE(readFile(prices), QByteArray("shipped prices"));
        for (const char *folder : {".crlf/scripts", ".crlf/keys", ".crlf/rules"})
            QVERIFY(QFileInfo(application.filePath(QLatin1String(folder))).isDir());

        writeFile(appFile(), "edited");
        writeFile(prices, "my prices");
        QVERIFY(QFile::remove(application.filePath(QLatin1String(kBlankFile))));
        QCOMPARE(seedApplicationFolder(m_application, m_program), QString());
        QCOMPARE(readFile(appFile()), QByteArray("edited"));
        QCOMPARE(readFile(prices), QByteArray("my prices"));
        QCOMPARE(readFile(application.filePath(QLatin1String(kBlankFile))), QByteArray("{}"));
    }

    // The first run after an upgrade must not lose the user's defaults.
    // They were the file named .crlf beside the program. It is kept as
    // previous.crlf and named as the file to make active, so those settings
    // keep applying while the configured application.crlf is there to switch
    // to. A later run names nothing, so the user's own choice stands.
    void firstRunKeepsThePreviousDefaults()
    {
        const QDir program(m_program);
        writeFile(program.filePath(QLatin1String(kApplicationFile)), "configured");
        writeFile(program.filePath(QStringLiteral(".crlf")), R"([{"name": "Old", "command": "o"}])");

        QCOMPARE(seedApplicationFolder(m_application, m_program), QString::fromLatin1(kPreviousFile));
        QCOMPARE(readFile(appFile()), QByteArray("configured"));
        const QString previous = QDir(m_application).filePath(QLatin1String(kPreviousFile));
        QCOMPARE(commandNames(projectFor(QString(), previous)), QStringList({QStringLiteral("Old")}));

        QCOMPARE(seedApplicationFolder(m_application, m_program), QString());
    }

    // An old defaults file nobody edited still holds what shipped with it:
    // an empty command list. Keeping that active would hide the configured
    // file from someone who never chose to have no settings.
    void firstRunIgnoresUntouchedPreviousDefaults()
    {
        const QDir program(m_program);
        writeFile(program.filePath(QLatin1String(kApplicationFile)), "configured");
        writeFile(program.filePath(QStringLiteral(".crlf")), R"({ "commands": [] })");

        QCOMPARE(seedApplicationFolder(m_application, m_program), QString());
        QVERIFY(!QFileInfo::exists(QDir(m_application).filePath(QLatin1String(kPreviousFile))));
        QCOMPARE(readFile(appFile()), QByteArray("configured"));
    }

    // An earlier version seeded the application folder by copying the old
    // defaults file as application.crlf. Where that file was never edited,
    // the folder holds an empty command list under the name the configured
    // file needs, and "never replace a file that is there" would keep the
    // shipped agents away for good. That one content is replaced. A file
    // with anything else in it is someone's settings and stays.
    void anEmptyApplicationFileFromAnEarlierVersionIsReplaced()
    {
        const QDir program(m_program);
        writeFile(program.filePath(QLatin1String(kApplicationFile)), "configured");
        writeFile(program.filePath(QStringLiteral(".crlf")), R"({ "commands": [] })");
        writeFile(appFile(), R"({ "commands": [] })");

        QCOMPARE(seedApplicationFolder(m_application, m_program), QString());
        QCOMPARE(readFile(appFile()), QByteArray("configured"));

        const QByteArray mine = R"({ "commands": [], "hide": ["build"] })";
        writeFile(appFile(), mine);
        QCOMPARE(seedApplicationFolder(m_application, m_program), QString());
        QCOMPARE(readFile(appFile()), mine);
    }

    // The user picks which application file is in use. The choice is a file
    // name; when that file has been deleted, application.crlf is used, so a
    // stale choice never leaves the editor without settings. The chosen
    // file is the one a project reads.
    void theChosenApplicationFileIsTheOneRead()
    {
        const QDir application(m_application);
        writeFile(appFile(), R"({"commands": [{"name": "Configured", "command": "c"}]})");
        writeFile(application.filePath(QLatin1String(kBlankFile)), "{}");

        const QString blank = activeApplicationFile(m_application, QString::fromLatin1(kBlankFile));
        QCOMPARE(blank, application.filePath(QLatin1String(kBlankFile)));
        QCOMPARE(activeApplicationFile(m_application, QStringLiteral("gone.crlf")), appFile());
        QCOMPARE(activeApplicationFile(m_application, QString()), appFile());

        const Project project = projectFor(QString(), blank);
        QCOMPARE(project.files, QStringList({blank}));
        QVERIFY(project.commands.isEmpty());
        QCOMPARE(commandNames(projectFor(QString(), appFile())), QStringList({QStringLiteral("Configured")}));
    }

    // A workspace's application.crlf is its copy of the application
    // settings: the base the workspace's own files adjust. By name it would
    // be read first and win, so it is read after the others instead.
    void workspaceApplicationFileIsMergedLast()
    {
        const QDir work(m_work);
        writeFile(work.filePath(QLatin1String(kApplicationFile)),
                  R"({"commands": [{"name": "Build", "command": "base"}, {"name": "Run", "command": "r"}]})");
        writeFile(work.filePath(QLatin1String(kWorkspaceFile)),
                  R"({"commands": [{"name": "Build", "command": "mine"}]})");
        writeFile(work.filePath(QStringLiteral("zeta.crlf")), R"({"commands": [{"name": "Zeta", "command": "z"}]})");

        const Project project = projectFor(m_work, appFile());
        QCOMPARE(project.files, QStringList({work.filePath(QLatin1String(kWorkspaceFile)),
                                             work.filePath(QStringLiteral("zeta.crlf")),
                                             work.filePath(QLatin1String(kApplicationFile))}));
        QCOMPARE(commandNames(project),
                 QStringList({QStringLiteral("Build"), QStringLiteral("Zeta"), QStringLiteral("Run")}));
        QCOMPARE(project.commands.at(0).command, QStringLiteral("mine"));
    }

    // The application.crlf that ships is what a new install runs with, so
    // it has to parse and set up what it promises: the four agents on both
    // Windows and WSL, each reading its key and both rules folders, and the
    // syntax and autocomplete scripts, which are bash and so start only in a
    // WSL folder.
    void shippedApplicationFileSetsUpTheScripts()
    {
        QVERIFY(QFile::copy(QStringLiteral(SHIPPED_APPLICATION_FILE), appFile()));
        writeFile(QDir(m_work).filePath(QLatin1String(kWorkspaceFile)), R"({"inherit": true})");

        const Project project = projectFor(m_work, appFile());
        QCOMPARE(project.error, QString());
        QStringList agents;
        for (const AgentSettings &agent : project.agents) {
            agents.append(agent.name);
            QVERIFY(agent.process.command.startsWith(
                QStringLiteral("python \"${programDir}/scripts/agent/crlf_agent.py\"")));
            QVERIFY(agent.process.command.contains(QStringLiteral("--rules .crlf/rules --rules \"${appDir}/rules\"")));
        }
        QCOMPARE(agents, QStringList({QStringLiteral("Claude"), QStringLiteral("GPT"), QStringLiteral("Grok"),
                                      QStringLiteral("Local")}));
        QVERIFY(project.agents.at(1).process.command.contains(
            QStringLiteral("--api-key-file \"${appDir}/keys/openai\"")));
        QVERIFY(project.syntax.enabled);
        QCOMPARE(project.syntax.command, QString());
        QCOMPARE(project.autocomplete.command, QString());

        const QJsonObject shipped = readObject(appFile());
        const QString distro = QStringLiteral("//wsl.localhost/Ubuntu/home/me/app");
        QCOMPARE(commandFor(shipped.value(QLatin1String("syntax")).toObject().value(QLatin1String("command")), distro),
                 QStringLiteral("bash \"${programDir}/scripts/syntax.sh\" \"${file}\""));
        QCOMPARE(commandFor(shipped.value(QLatin1String("autocomplete")).toObject().value(QLatin1String("command")),
                            distro),
                 QStringLiteral("bash \"${programDir}/scripts/autocomplete.sh\""));
        for (const QJsonValue &agent : shipped.value(QLatin1String("agents")).toArray()) {
            QVERIFY(commandFor(agent.toObject().value(QLatin1String("command")), distro)
                        .startsWith(QStringLiteral("python3 \"${programDir}/scripts/agent/crlf_agent.py\"")));
        }
    }

    // Copy Application Files to Workspace makes a folder self-contained. It
    // puts the active application file in the root as application.crlf,
    // whatever that file is called, with the script and rule paths pointing
    // at the copies. An empty workspace.crlf is made beside it for the
    // workspace's own settings, which are read first. The shipped scripts
    // are copied, then the application's (which win on a shared path), and
    // the rules. Keys are never copied, because a workspace is usually a git
    // repository, so the copy keeps naming ${appDir}/keys.
    void copyIntoAnEmptyFolderCreatesAWorkspace()
    {
        makeApplication();
        const QString chosen = QDir(m_application).filePath(QStringLiteral("team.crlf"));
        QVERIFY(QFile::rename(appFile(), chosen));
        const SeedCounts counts = seedWorkspace(chosen, shipped(), m_work, false);
        QCOMPARE(counts.copied, 6);
        QCOMPARE(counts.replaced, 0);
        QCOMPARE(counts.skipped, 0);
        QCOMPARE(counts.failed, 0);

        const QDir work(m_work);
        QCOMPARE(readObject(work.filePath(QLatin1String(kWorkspaceFile))), QJsonObject());
        const QByteArray settings = readFile(work.filePath(QLatin1String(kApplicationFile)));
        // The file is JSON text, so a quote inside a command is \".
        QVERIFY(settings.contains("python \\\".crlf/scripts/agent/crlf_agent.py\\\""));
        QVERIFY(settings.contains("--rules \\\".crlf/rules\\\""));
        QVERIFY(settings.contains("\\\".crlf/scripts/mine.sh\\\""));
        QVERIFY(settings.contains("--api-key-file \\\"${appDir}/keys/openai\\\""));
        QVERIFY(!settings.contains("${programDir}"));

        QCOMPARE(readFile(work.filePath(QStringLiteral(".crlf/scripts/agent/crlf_agent.py"))), QByteArray("shipped agent"));
        QCOMPARE(readFile(work.filePath(QStringLiteral(".crlf/scripts/syntax.sh"))), QByteArray("application syntax"));
        QCOMPARE(readFile(work.filePath(QStringLiteral(".crlf/scripts/mine.sh"))), QByteArray("mine"));
        QCOMPARE(readFile(work.filePath(QStringLiteral(".crlf/rules/style.md"))), QByteArray("rule"));
        QVERIFY(!QFileInfo::exists(work.filePath(QStringLiteral(".crlf/keys"))));

        // The two files are a working project: the agent comes from the copy
        // and runs the copied script.
        const Project project = projectFor(m_work, chosen);
        QCOMPARE(project.files, QStringList({work.filePath(QLatin1String(kWorkspaceFile)),
                                             work.filePath(QLatin1String(kApplicationFile))}));
        QVERIFY(project.agents.at(0).process.command.startsWith(
            QStringLiteral("python \".crlf/scripts/agent/crlf_agent.py\"")));
    }

    // Running the command in a project that already exists must not undo
    // work done there. With Replace existing files off, the workspace's
    // settings, its copy of the application file, and a script it already
    // has are left as they are and counted as skipped; only what is missing
    // is added.
    void copyLeavesExistingFilesAlone()
    {
        makeApplication();
        const QDir work(m_work);
        writeFile(work.filePath(QLatin1String(kWorkspaceFile)), R"({"hide": ["build"]})");
        writeFile(work.filePath(QLatin1String(kApplicationFile)), "earlier copy");
        writeFile(work.filePath(QStringLiteral(".crlf/scripts/syntax.sh")), "edited here");

        const SeedCounts counts = seedWorkspace(appFile(), shipped(), m_work, false);
        QCOMPARE(counts.copied, 3);
        QCOMPARE(counts.replaced, 0);
        QCOMPARE(counts.skipped, 3);

        QCOMPARE(readFile(work.filePath(QLatin1String(kWorkspaceFile))), QByteArray(R"({"hide": ["build"]})"));
        QCOMPARE(readFile(work.filePath(QLatin1String(kApplicationFile))), QByteArray("earlier copy"));
        QCOMPARE(readFile(work.filePath(QStringLiteral(".crlf/scripts/syntax.sh"))), QByteArray("edited here"));
        QCOMPARE(readFile(work.filePath(QStringLiteral(".crlf/scripts/mine.sh"))), QByteArray("mine"));
    }

    // Replace existing files brings a workspace's copies up to date after
    // an upgrade. It overwrites what came from the application: the copy of
    // the application file and the scripts the application also has. A
    // script only the workspace has stays, and the workspace's own settings
    // file is never replaced, with the box on or off.
    void copyWithReplaceOverwritesTheCopiesOnly()
    {
        makeApplication();
        const QDir work(m_work);
        writeFile(work.filePath(QLatin1String(kWorkspaceFile)), R"({"hide": ["build"]})");
        writeFile(work.filePath(QLatin1String(kApplicationFile)), "earlier copy");
        writeFile(work.filePath(QStringLiteral(".crlf/scripts/syntax.sh")), "edited here");
        writeFile(work.filePath(QStringLiteral(".crlf/scripts/own.sh")), "only here");

        const SeedCounts counts = seedWorkspace(appFile(), shipped(), m_work, true);
        QCOMPARE(counts.copied, 3);
        QCOMPARE(counts.replaced, 2);
        QCOMPARE(counts.skipped, 1);

        QVERIFY(readFile(work.filePath(QLatin1String(kApplicationFile))).contains(".crlf/scripts/agent/crlf_agent.py"));
        QCOMPARE(readFile(work.filePath(QStringLiteral(".crlf/scripts/syntax.sh"))), QByteArray("application syntax"));
        QCOMPARE(readFile(work.filePath(QStringLiteral(".crlf/scripts/own.sh"))), QByteArray("only here"));
        QCOMPARE(readFile(work.filePath(QLatin1String(kWorkspaceFile))), QByteArray(R"({"hide": ["build"]})"));
    }

    // A command pins the scripts it names and the scripts in that directory,
    // including one the command line does not mention. ${file} is not a path.
    // A key file and a bytecode cache are not scripts the editor will run.
    void pinsTheScriptTreeAroundEachCommand()
    {
        writeFile(QDir(m_work).filePath(QStringLiteral("scripts/build.cmd")), "cmd");
        writeFile(QDir(m_work).filePath(QStringLiteral("scripts/build.ps1")), "ps1");
        writeFile(QDir(m_work).filePath(QStringLiteral("scripts/autocomplete.sh")), "sh");
        writeFile(QDir(m_work).filePath(QStringLiteral("scripts/autocomplete.py")), "py");
        writeFile(QDir(m_work).filePath(QStringLiteral("scripts/readme.txt")), "notes");
        writeFile(QDir(m_work).filePath(QStringLiteral("scripts/agent/crlf_agent.py")), "agent");
        writeFile(QDir(m_work).filePath(QStringLiteral("scripts/agent/usage.py")), "usage");
        writeFile(QDir(m_work).filePath(QStringLiteral("scripts/agent/tools/files.py")), "files");
        writeFile(QDir(m_work).filePath(QStringLiteral("scripts/__pycache__/cached.py")), "cached");
        writeFile(QDir(m_work).filePath(QStringLiteral(".crlf/keys/anthropic")), "key");
        writeFile(QDir(m_work).filePath(QStringLiteral(".crlf/keys/evil.py")), "evil");

        Project project;
        project.directory = m_work;
        ProjectCommand package;
        package.name = QStringLiteral("Package");
        package.command = QStringLiteral("scripts\\build.cmd");
        package.workingDirectory = m_work;
        package.workspaceOrigin = true;
        ProjectCommand build;
        build.name = QStringLiteral("Build");
        build.command = QStringLiteral("cmake --build build");
        build.workingDirectory = m_work;
        build.workspaceOrigin = true;
        project.commands = {package, build};
        AgentSettings agent;
        agent.name = QStringLiteral("Claude");
        agent.workspaceOrigin = true;
        agent.process.command =
            QStringLiteral("python scripts/agent/crlf_agent.py --api-key-file .crlf/keys/anthropic");
        agent.process.workingDirectory = m_work;
        project.agents = {agent};
        project.autocomplete.enabled = true;
        project.autocomplete.workspaceOrigin = true;
        project.autocomplete.command = QStringLiteral("./scripts/autocomplete.sh \"${file}\"");
        project.autocomplete.workingDirectory = m_work;

        const ProjectPin pin = pinProject(project);
        const QStringList agentFiles = entryFiles(pin, QStringLiteral("agent:Claude"));
        QVERIFY(agentFiles.contains(QStringLiteral("scripts/agent/crlf_agent.py")));
        QVERIFY(agentFiles.contains(QStringLiteral("scripts/agent/usage.py")));
        QVERIFY(agentFiles.contains(QStringLiteral("scripts/agent/tools/files.py")));
        QVERIFY(!agentFiles.contains(QStringLiteral("scripts/autocomplete.py")));
        const QStringList completionFiles = entryFiles(pin, QStringLiteral("autocomplete"));
        QVERIFY(completionFiles.contains(QStringLiteral("scripts/autocomplete.sh")));
        QVERIFY(completionFiles.contains(QStringLiteral("scripts/autocomplete.py")));
        QVERIFY(entryFiles(pin, QStringLiteral("command:Package")).contains(QStringLiteral("scripts/build.ps1")));
        QVERIFY(entryFiles(pin, QStringLiteral("command:Build")).isEmpty());
        QVERIFY(trustReviewText(pin, {}).contains(QStringLiteral("No project script is named.")));
        for (const PinnedFile &file : pin.files) {
            QVERIFY(!file.relative.contains(QStringLiteral("__pycache__")));
            QVERIFY(!file.relative.contains(QStringLiteral(".crlf/keys")));
            QVERIFY(file.relative != QStringLiteral("scripts/readme.txt"));
        }
    }

    // Trust is the stored pin. The same bytes are allowed. A changed script
    // or a new one in a pinned directory is not, and a folder the user left
    // restricted stays held.
    void trustedPinAllowsTheSameBytesOnly()
    {
        writeFile(QDir(m_work).filePath(QStringLiteral("scripts/agent/crlf_agent.py")), "agent");
        writeFile(QDir(m_work).filePath(QStringLiteral("scripts/agent/usage.py")), "usage");
        Project project;
        project.directory = m_work;
        AgentSettings agent;
        agent.name = QStringLiteral("Claude");
        agent.workspaceOrigin = true;
        agent.process.command = QStringLiteral("python scripts/agent/crlf_agent.py");
        agent.process.workingDirectory = m_work;
        project.agents = {agent};

        const ProjectPin pin = pinProject(project);
        TrustRecord trusted;
        trusted.state = TrustState::Trusted;
        for (const TrustEntry &entry : pin.entries)
            trusted.templates.append({entry.id, entry.templateText});
        trusted.files = pin.files;
        TrustDifference changes;
        QCOMPARE(decideTrust(pin, trusted, &changes), TrustDecision::Allow);
        QCOMPARE(decideTrust(pin, {}, &changes), TrustDecision::Untrusted);
        TrustRecord restricted;
        restricted.state = TrustState::Restricted;
        QCOMPARE(decideTrust(pin, restricted, &changes), TrustDecision::Restricted);

        writeFile(QDir(m_work).filePath(QStringLiteral("scripts/agent/usage.py")), "changed");
        QCOMPARE(decideTrust(pinProject(project), trusted, &changes), TrustDecision::Mismatch);
        QVERIFY(changes.paths.contains(QStringLiteral("scripts/agent/usage.py")));

        writeFile(QDir(m_work).filePath(QStringLiteral("scripts/agent/usage.py")), "usage");
        writeFile(QDir(m_work).filePath(QStringLiteral("scripts/agent/extra.py")), "extra");
        QCOMPARE(decideTrust(pinProject(project), trusted, &changes), TrustDecision::Mismatch);
        QVERIFY(changes.paths.contains(QStringLiteral("scripts/agent/extra.py")));
    }

    // The record is outside the project. A later read of the same folder
    // returns the pin that was stored.
    void trustRecordRoundTrips()
    {
        QSettings settings(QDir(m_directory->path()).filePath(QStringLiteral("trust.ini")), QSettings::IniFormat);
        TrustRecord record;
        record.state = TrustState::Trusted;
        record.templates.append({QStringLiteral("command:Build"), QStringLiteral("cmake --build build")});
        record.files.append({QStringLiteral("scripts/build.cmd"), QStringLiteral("abc")});
        saveTrust(settings, m_work, record);
        settings.sync();
        QSettings loaded(QDir(m_directory->path()).filePath(QStringLiteral("trust.ini")), QSettings::IniFormat);
        const TrustRecord again = loadTrust(loaded, m_work);
        QCOMPARE(again.state, TrustState::Trusted);
        QCOMPARE(again.templates.size(), 1);
        QCOMPARE(again.templates.at(0).id, QStringLiteral("command:Build"));
        QCOMPARE(again.files.size(), 1);
        QCOMPARE(again.files.at(0).relative, QStringLiteral("scripts/build.cmd"));
        QCOMPARE(again.files.at(0).hash, QStringLiteral("abc"));
    }

    // A workspace command is the project's. A command inherited from the
    // application file, which runs a script next to the program, is not.
    void workspaceCommandIsWorkspaceOriginAndInheritedProgramDirCommandIsNot()
    {
        QJsonObject build;
        build.insert(QLatin1String("name"), QStringLiteral("Build"));
        build.insert(QLatin1String("command"), QStringLiteral("cmake --build build"));
        QJsonObject workspace;
        workspace.insert(QLatin1String("inherit"), QJsonArray{QStringLiteral("commands"), QStringLiteral("agents")});
        workspace.insert(QLatin1String("commands"), QJsonArray{build});
        writeFile(QDir(m_work).filePath(QStringLiteral("workspace.crlf")), QJsonDocument(workspace).toJson());

        QJsonObject mine;
        mine.insert(QLatin1String("name"), QStringLiteral("Mine"));
        mine.insert(QLatin1String("command"),
                    QStringLiteral("python \"${programDir}/scripts/agent/crlf_agent.py\""));
        QJsonObject claude;
        claude.insert(QLatin1String("name"), QStringLiteral("Claude"));
        claude.insert(QLatin1String("command"),
                      QStringLiteral("python \"${programDir}/scripts/agent/crlf_agent.py\""));
        QJsonObject application;
        application.insert(QLatin1String("commands"), QJsonArray{mine});
        application.insert(QLatin1String("agents"), QJsonArray{claude});
        writeFile(appFile(), QJsonDocument(application).toJson());

        const Project project = projectFor(m_work, appFile());
        bool buildOrigin = false;
        bool mineOrigin = true;
        bool sawBuild = false;
        bool sawMine = false;
        for (const ProjectCommand &command : project.commands) {
            if (command.name == QLatin1String("Build")) {
                sawBuild = true;
                buildOrigin = command.workspaceOrigin;
            }
            if (command.name == QLatin1String("Mine")) {
                sawMine = true;
                mineOrigin = command.workspaceOrigin;
            }
        }
        QVERIFY(sawBuild);
        QVERIFY(buildOrigin);
        QVERIFY(sawMine);
        QVERIFY(!mineOrigin);
        QCOMPARE(project.agents.size(), 1);
        QVERIFY(!project.agents.at(0).workspaceOrigin);
    }

    // An application command that names a script in the project is gated.
    // One that only names ${programDir} is not. Rule files are not pinned.
    void applicationScriptIsGatedAndRulesAreNotPinned()
    {
        writeFile(QDir(m_work).filePath(QStringLiteral("scripts/autocomplete.sh")), "sh");
        writeFile(QDir(m_work).filePath(QStringLiteral("scripts/autocomplete.py")), "py");
        writeFile(QDir(m_work).filePath(QStringLiteral(".crlf/rules/style.md")), "rule");
        Project project;
        project.directory = m_work;
        project.autocomplete.enabled = true;
        project.autocomplete.command = QStringLiteral("./scripts/autocomplete.sh");
        project.autocomplete.workingDirectory = m_work;
        AgentSettings agent;
        agent.name = QStringLiteral("Claude");
        agent.process.command =
            QStringLiteral("python \"${programDir}/scripts/agent/crlf_agent.py\" --rules .crlf/rules");
        agent.process.workingDirectory = m_work;
        project.agents = {agent};
        ProjectCommand mine;
        mine.name = QStringLiteral("Mine");
        mine.command = QStringLiteral("python \"${programDir}/scripts/agent/crlf_agent.py\"");
        mine.workingDirectory = m_work;
        project.commands = {mine};

        const ProjectPin pin = pinProject(project);
        QVERIFY(pin.gates(QStringLiteral("autocomplete")));
        QVERIFY(pin.gates(QStringLiteral("agent:Claude")));
        QVERIFY(!pin.gates(QStringLiteral("command:Mine")));
        QVERIFY(entryFiles(pin, QStringLiteral("autocomplete")).contains(QStringLiteral("scripts/autocomplete.py")));
        for (const PinnedFile &file : pin.files)
            QVERIFY(!file.relative.endsWith(QStringLiteral("style.md")));
        QVERIFY(trustReviewText(pin, {}).contains(QStringLiteral("Reads .crlf/rules.")));
    }

    // A script in the project root must not pull the rest of the tree into
    // the pin. That walk is what stalls startup on a WSL project whose
    // launch script sits next to .pnpm-store.
    void rootScriptDoesNotPinSubdirectories()
    {
        writeFile(QDir(m_work).filePath(QStringLiteral("launch.sh")), "launch");
        writeFile(QDir(m_work).filePath(QStringLiteral("format.sh")), "format");
        writeFile(QDir(m_work).filePath(QStringLiteral(".pnpm-store/pkg/evil.py")), "evil");
        writeFile(QDir(m_work).filePath(QStringLiteral("src/app.py")), "app");
        Project project;
        project.directory = m_work;
        ProjectCommand launch;
        launch.name = QStringLiteral("Launch");
        launch.command = QStringLiteral("./launch.sh");
        launch.workingDirectory = m_work;
        launch.workspaceOrigin = true;
        project.commands = {launch};

        const QStringList files = entryFiles(pinProject(project), QStringLiteral("command:Launch"));
        QVERIFY(files.contains(QStringLiteral("launch.sh")));
        QVERIFY(files.contains(QStringLiteral("format.sh")));
        QVERIFY(!files.contains(QStringLiteral(".pnpm-store/pkg/evil.py")));
        QVERIFY(!files.contains(QStringLiteral("src/app.py")));
    }

private:
    // The application file in use, unless a case names another.
    QString appFile() const { return QDir(m_application).filePath(QLatin1String(kApplicationFile)); }
    QString shipped() const { return QDir(m_program).filePath(QStringLiteral("scripts")); }

    // An application folder with a script of its own, a script that
    // replaces a shipped one, a rule, and a key, and an install folder with
    // the shipped scripts.
    void makeApplication()
    {
        const QDir application(m_application);
        QJsonObject agent;
        agent.insert(QLatin1String("name"), QStringLiteral("GPT"));
        agent.insert(QLatin1String("command"),
                     QStringLiteral("python \"${programDir}/scripts/agent/crlf_agent.py\""
                                    " --api-key-file \"${appDir}/keys/openai\" --rules \"${appDir}/rules\""));
        QJsonObject command;
        command.insert(QLatin1String("name"), QStringLiteral("Mine"));
        command.insert(QLatin1String("command"), QStringLiteral("\"${appDir}/scripts/mine.sh\""));
        QJsonObject settings;
        settings.insert(QLatin1String("agents"), QJsonArray{agent});
        settings.insert(QLatin1String("commands"), QJsonArray{command});
        writeFile(application.filePath(QLatin1String(kApplicationFile)), QJsonDocument(settings).toJson());
        writeFile(application.filePath(QStringLiteral(".crlf/scripts/mine.sh")), "mine");
        writeFile(application.filePath(QStringLiteral(".crlf/scripts/syntax.sh")), "application syntax");
        writeFile(application.filePath(QStringLiteral(".crlf/rules/style.md")), "rule");
        writeFile(application.filePath(QStringLiteral(".crlf/keys/openai")), "secret");
        writeFile(QDir(shipped()).filePath(QStringLiteral("syntax.sh")), "shipped syntax");
        writeFile(QDir(shipped()).filePath(QStringLiteral("agent/crlf_agent.py")), "shipped agent");
    }

    std::unique_ptr<QTemporaryDir> m_directory;
    QString m_work;
    QString m_application;
    QString m_program;
};

QTEST_GUILESS_MAIN(ProjectTests)

#include "ProjectTests.moc"
