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
#include "io/WorkspaceSeed.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

        const Project project = projectFor(m_work, m_application);
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

        const Project project = projectFor(m_work, m_application);
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
        const Project project = projectFor(QString(), m_application);
        QCOMPARE(project.directory, QString());
        QCOMPARE(project.files, QStringList({QDir(m_application).filePath(QLatin1String(kApplicationFile))}));
        QCOMPARE(project.commands.at(0).workingDirectory, QDir(m_application).filePath(QStringLiteral("build")));

        // The application folder holds a .crlf file and is still not a
        // project: the search passes it and finds the folder above.
        const QDir above(QDir(m_directory->path()).absolutePath());
        writeFile(above.filePath(QStringLiteral("outer.crlf")), R"([{"name": "Outer", "command": "o"}])");
        const Project opened = projectFor(m_application, m_application);
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

        const Project project = projectFor(m_work, m_application);
        QVERIFY(!QFileInfo::exists(work.filePath(QStringLiteral(".crlf"))));
        QCOMPARE(project.renamedFrom, work.filePath(QStringLiteral(".crlf")));
        QCOMPARE(project.renamedTo, work.filePath(QStringLiteral("workspace.crlf")));
        QCOMPARE(project.files, QStringList({work.filePath(QStringLiteral("workspace.crlf"))}));
        QCOMPARE(commandNames(project), QStringList({QStringLiteral("Build")}));

        // The next load finds nothing to rename.
        QCOMPARE(projectFor(m_work, m_application).renamedFrom, QString());
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

        const Project project = projectFor(m_work, m_application);
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

        const Project inner = projectFor(work.filePath(QStringLiteral("api/src/deep/main.cpp")), m_application);
        QCOMPARE(inner.directory, work.filePath(QStringLiteral("api")));
        QCOMPARE(commandNames(inner), QStringList({QStringLiteral("Inner")}));

        const Project outer = projectFor(work.filePath(QStringLiteral("web")), m_application);
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

        Project project = projectFor(m_work, m_application);
        QCOMPARE(groupNames(project),
                 QStringList({QStringLiteral("Docs"), QStringLiteral("Old"), QStringLiteral("Shared")}));
        FileGroup added;
        added.name = QStringLiteral("New");
        added.files = {QStringLiteral("TODO.md")};
        project.groups.append(added);
        QVERIFY(saveProjectGroups(m_work, project.groups, m_application));

        QCOMPARE(names(readObject(first).value(QLatin1String("groups"))),
                 QStringList({QStringLiteral("Docs"), QStringLiteral("Old"), QStringLiteral("New")}));
        QCOMPARE(readObject(first).value(QLatin1String("note")).toString(), QStringLiteral("kept"));
        QCOMPARE(readObject(second), json(R"({"commands": []})"));
        QCOMPARE(readObject(work.filePath(QStringLiteral("a.crlf"))), json(R"({"inherit": true, "hide": ["build"]})"));
        QCOMPARE(readFile(application), applicationBefore);
        QCOMPARE(groupNames(projectFor(m_work, m_application)),
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
        QVERIFY(saveProjectGroups(m_work, {shared, added}, m_application));

        const QJsonObject created = readObject(QDir(m_work).filePath(QLatin1String(kWorkspaceFile)));
        QCOMPARE(created.value(QLatin1String("inherit")).toBool(), true);
        QCOMPARE(names(created.value(QLatin1String("groups"))), QStringList({QStringLiteral("New")}));
        const Project project = projectFor(m_work, m_application);
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
        const Project project = projectFor(m_work, m_application);
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

    // The first run after an upgrade must not lose the user's defaults.
    // They were the file named .crlf beside the program, so that file
    // becomes application.crlf; a new install has only the shipped
    // application.crlf to copy. The folders for the user's own scripts,
    // keys, and rules are made then, and a later run changes nothing.
    void firstRunSeedsTheApplicationFolder()
    {
        const QDir program(m_program);
        const QDir application(m_application);
        const QString settings = application.filePath(QLatin1String(kApplicationFile));
        writeFile(program.filePath(QLatin1String(kApplicationFile)), R"({"commands": []})");

        seedApplicationFolder(m_application, m_program);
        QCOMPARE(readFile(settings), QByteArray(R"({"commands": []})"));
        for (const char *folder : {".crlf/scripts", ".crlf/keys", ".crlf/rules"})
            QVERIFY(QFileInfo(application.filePath(QLatin1String(folder))).isDir());

        writeFile(settings, "edited");
        seedApplicationFolder(m_application, m_program);
        QCOMPARE(readFile(settings), QByteArray("edited"));

        QVERIFY(QFile::remove(settings));
        writeFile(program.filePath(QStringLiteral(".crlf")), R"([{"name": "Old", "command": "o"}])");
        seedApplicationFolder(m_application, m_program);
        QCOMPARE(readFile(settings), QByteArray(R"([{"name": "Old", "command": "o"}])"));
    }

    // Copy Application Files to Workspace makes a folder self-contained. In
    // an empty folder it creates workspace.crlf from application.crlf with
    // the script and rule paths pointing at the copies, copies the shipped
    // scripts and then the application's (which win on a shared path), and
    // copies the rules. Keys are never copied, because a workspace is
    // usually a git repository, so the new file keeps naming ${appDir}/keys.
    void copyIntoAnEmptyFolderCreatesAWorkspace()
    {
        makeApplication();
        const SeedCounts counts = seedWorkspace(m_application, shipped(), m_work, false);
        QCOMPARE(counts.copied, 5);
        QCOMPARE(counts.replaced, 0);
        QCOMPARE(counts.skipped, 0);
        QCOMPARE(counts.failed, 0);

        const QDir work(m_work);
        const QByteArray settings = readFile(work.filePath(QLatin1String(kWorkspaceFile)));
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

        // The created file is a working project: its agent runs the copy.
        const Project project = projectFor(m_work, m_application);
        QCOMPARE(project.files, QStringList({work.filePath(QLatin1String(kWorkspaceFile))}));
        QVERIFY(project.agents.at(0).process.command.startsWith(
            QStringLiteral("python \".crlf/scripts/agent/crlf_agent.py\"")));
    }

    // Running the command in a project that already exists must not undo
    // work done there. With Replace existing files off, the settings file
    // and a script the workspace already has are left as they are and
    // counted as skipped; only what is missing is added.
    void copyLeavesExistingFilesAlone()
    {
        makeApplication();
        const QDir work(m_work);
        writeFile(work.filePath(QStringLiteral("local.crlf")), R"({"hide": ["build"]})");
        writeFile(work.filePath(QStringLiteral(".crlf/scripts/syntax.sh")), "edited here");

        const SeedCounts counts = seedWorkspace(m_application, shipped(), m_work, false);
        QCOMPARE(counts.copied, 3);
        QCOMPARE(counts.replaced, 0);
        QCOMPARE(counts.skipped, 2);

        QCOMPARE(readFile(work.filePath(QStringLiteral("local.crlf"))), QByteArray(R"({"hide": ["build"]})"));
        QVERIFY(!QFileInfo::exists(work.filePath(QLatin1String(kWorkspaceFile))));
        QCOMPARE(readFile(work.filePath(QStringLiteral(".crlf/scripts/syntax.sh"))), QByteArray("edited here"));
        QCOMPARE(readFile(work.filePath(QStringLiteral(".crlf/scripts/mine.sh"))), QByteArray("mine"));
    }

    // Replace existing files brings a workspace's copies up to date after
    // an upgrade. It overwrites what the application also has and nothing
    // else: a script only the workspace has stays, and a settings file is
    // never replaced, with the box on or off.
    void copyWithReplaceOverwritesScriptsOnly()
    {
        makeApplication();
        const QDir work(m_work);
        writeFile(work.filePath(QStringLiteral("local.crlf")), R"({"hide": ["build"]})");
        writeFile(work.filePath(QStringLiteral(".crlf/scripts/syntax.sh")), "edited here");
        writeFile(work.filePath(QStringLiteral(".crlf/scripts/own.sh")), "only here");

        const SeedCounts counts = seedWorkspace(m_application, shipped(), m_work, true);
        QCOMPARE(counts.copied, 3);
        QCOMPARE(counts.replaced, 1);
        QCOMPARE(counts.skipped, 1);

        QCOMPARE(readFile(work.filePath(QStringLiteral(".crlf/scripts/syntax.sh"))), QByteArray("application syntax"));
        QCOMPARE(readFile(work.filePath(QStringLiteral(".crlf/scripts/own.sh"))), QByteArray("only here"));
        QCOMPARE(readFile(work.filePath(QStringLiteral("local.crlf"))), QByteArray(R"({"hide": ["build"]})"));
        QVERIFY(!QFileInfo::exists(work.filePath(QLatin1String(kWorkspaceFile))));
    }

private:
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
