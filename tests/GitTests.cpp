// Confirms the parts of source control whose input varies: git's machine
// output, the repository walk, and the line diff. The widgets and the git
// process itself are not tested here; they only pass these results along.

#include "git/GitDiscovery.h"
#include "git/GitParse.h"
#include "git/LineDiff.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {

QByteArray records(const QList<QByteArray> &entries)
{
    QByteArray joined;
    for (const QByteArray &entry : entries)
        joined += entry + '\0';
    return joined;
}

void makeRepository(const QString &path)
{
    QDir().mkpath(QDir(path).filePath(QStringLiteral(".git")));
}

// Rebuilds `after` from `before` and the changes. Lines between changes are
// copied from `before`, so they must also equal the matching lines of `after`.
QStringList applyChanges(const QStringList &before, const QStringList &after, const QList<LineChange> &changes)
{
    QStringList result;
    int oldLine = 0;
    int newLine = 0;
    for (const LineChange &change : changes) {
        while (oldLine < change.oldStart) {
            if (before.at(oldLine) != after.at(newLine))
                return {QStringLiteral("<kept line differs>")};
            result.append(before.at(oldLine++));
            ++newLine;
        }
        for (int i = 0; i < change.newCount; ++i)
            result.append(after.at(change.newStart + i));
        oldLine += change.oldCount;
        newLine += change.newCount;
    }
    while (oldLine < before.size())
        result.append(before.at(oldLine++));
    return result;
}

QStringList letters(const QString &text)
{
    QStringList lines;
    for (QChar c : text)
        lines.append(QString(c));
    return lines;
}

} // namespace

class GitTests : public QObject
{
    Q_OBJECT

private slots:
    // The branch line, its upstream, and the ahead/behind counts feed the
    // panel header and the status bar.
    void statusReadsBranchHeaders()
    {
        const GitStatus status = parseStatus(records({"# branch.oid 1234567890abcdef", "# branch.head main",
                                                      "# branch.upstream origin/main", "# branch.ab +2 -3"}));
        QCOMPARE(status.head, QStringLiteral("1234567890abcdef"));
        QCOMPARE(status.branch, QStringLiteral("main"));
        QCOMPARE(status.upstream, QStringLiteral("origin/main"));
        QCOMPARE(status.ahead, 2);
        QCOMPARE(status.behind, 3);
    }

    // A detached HEAD has no branch name, so the UI shows the commit instead.
    void statusDetachedHeadHasNoBranch()
    {
        const GitStatus status = parseStatus(records({"# branch.oid abc", "# branch.head (detached)"}));
        QVERIFY(status.branch.isEmpty());
    }

    // Paths are the last field, so a space inside one must survive.
    void statusKeepsSpacesInPaths()
    {
        const GitStatus status =
            parseStatus(records({"1 .M N... 100644 100644 100644 aaaa bbbb docs/read me.md"}));
        QCOMPARE(status.changes.size(), 1);
        const GitChange &change = status.changes.first();
        QCOMPARE(change.path, QStringLiteral("docs/read me.md"));
        QVERIFY(!change.staged());
        QVERIFY(change.unstaged());
    }

    // A rename's original path is the record after it, not a new change.
    void statusReadsRenameSource()
    {
        const GitStatus status = parseStatus(
            records({"2 R. N... 100644 100644 100644 aaaa bbbb R100 new name.txt", "old name.txt",
                     "? notes.txt"}));
        QCOMPARE(status.changes.size(), 2);
        QCOMPARE(status.changes.at(0).path, QStringLiteral("new name.txt"));
        QCOMPARE(status.changes.at(0).originalPath, QStringLiteral("old name.txt"));
        QVERIFY(status.changes.at(0).staged());
        QVERIFY(status.changes.at(1).untracked());
        QCOMPARE(status.changes.at(1).path, QStringLiteral("notes.txt"));
    }

    // Conflicts go to their own group and are neither staged nor unstaged.
    void statusMarksConflicts()
    {
        const GitStatus status =
            parseStatus(records({"u UU N... 100644 100644 100644 100644 aaaa bbbb cccc src/main.cpp"}));
        QCOMPARE(status.changes.size(), 1);
        const GitChange &change = status.changes.first();
        QVERIFY(change.conflicted);
        QVERIFY(!change.staged());
        QVERIFY(!change.unstaged());
        QCOMPARE(change.path, QStringLiteral("src/main.cpp"));
        QCOMPARE(badgeFor(change).tone, GitTone::Conflicted);
    }

    // The branch menu lists local branches first and never offers origin/HEAD.
    void branchesListLocalFirstWithoutRemoteHead()
    {
        const QByteArray output = " \x1frefs/remotes/origin/HEAD\x1f\n"
                                  " \x1frefs/remotes/origin/main\x1f\n"
                                  "*\x1frefs/heads/main\x1forigin/main\n"
                                  " \x1frefs/heads/feature/x\x1f\n";
        const QList<GitBranch> branches = parseBranches(output);
        QCOMPARE(branches.size(), 3);
        QCOMPARE(branches.at(0).name, QStringLiteral("main"));
        QVERIFY(branches.at(0).current);
        QCOMPARE(branches.at(0).upstream, QStringLiteral("origin/main"));
        QCOMPARE(branches.at(1).name, QStringLiteral("feature/x"));
        QCOMPARE(branches.at(2).name, QStringLiteral("origin/main"));
        QVERIFY(branches.at(2).remote);
    }

    // File history rows come from the log format; a subject may be empty.
    void logReadsEachCommit()
    {
        const QByteArray output = "abc123\x1f" "Ada\x1f" "2026-11-10\x1f" "Fix: parse\x1e\n"
                                  "def456\x1f" "Bob\x1f" "2026-11-09\x1f\x1e\n";
        const QList<GitCommit> commits = parseLog(output);
        QCOMPARE(commits.size(), 2);
        QCOMPARE(commits.at(0).id, QStringLiteral("abc123"));
        QCOMPARE(commits.at(0).author, QStringLiteral("Ada"));
        QCOMPARE(commits.at(0).date, QStringLiteral("2026-11-10"));
        QCOMPARE(commits.at(0).subject, QStringLiteral("Fix: parse"));
        QVERIFY(commits.at(1).subject.isEmpty());
    }

    // Identical text produces no change bars.
    void diffOfEqualTextIsEmpty()
    {
        QVERIFY(diffLines(letters(QStringLiteral("abc")), letters(QStringLiteral("abc"))).isEmpty());
    }

    // Each kind of change the gutter draws: added, removed, and rewritten rows.
    void diffReportsEachKind()
    {
        QCOMPARE(diffLines(letters(QStringLiteral("ac")), letters(QStringLiteral("abc"))),
                 (QList<LineChange>{{1, 0, 1, 1}}));
        QCOMPARE(diffLines(letters(QStringLiteral("abc")), letters(QStringLiteral("ab"))),
                 (QList<LineChange>{{2, 1, 2, 0}}));
        QCOMPARE(diffLines(letters(QStringLiteral("abc")), letters(QStringLiteral("axc"))),
                 (QList<LineChange>{{1, 1, 1, 1}}));
    }

    // Whatever runs the algorithm picks, the changes must turn the old text
    // into the new one and keep only lines both sides share.
    void diffRebuildsTheNewText()
    {
        const QList<QPair<QString, QString>> cases{
            {QStringLiteral("abcabba"), QStringLiteral("cbabac")},
            {QStringLiteral("abcdefghij"), QStringLiteral("xbcdyfghiz")},
            {QString(), QStringLiteral("abc")},
            {QStringLiteral("abc"), QString()},
            {QStringLiteral("aaaaab"), QStringLiteral("baaaaa")},
            {QStringLiteral("the quick brown fox"), QStringLiteral("a quick red fox jumps")},
        };
        for (const auto &[before, after] : cases) {
            const QStringList oldLines = letters(before);
            const QStringList newLines = letters(after);
            QCOMPARE(applyChanges(oldLines, newLines, diffLines(oldLines, newLines)), newLines);
        }
    }

    // A trailing newline does not add a row, so saving a file that ends in one
    // does not mark a phantom last line.
    void splitIgnoresTrailingNewline()
    {
        QCOMPARE(splitLines(QStringLiteral("a\nb\n")), (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
        QCOMPARE(splitLines(QStringLiteral("a\nb")), (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
        QVERIFY(splitLines(QString()).isEmpty());
    }

    // `depth` is the .crlf setting: how far below the scoped folder nested
    // repositories are found. Dot folders, node_modules, and `hide` names are
    // not walked.
    void discoveryHonorsDepthAndSkips()
    {
        QTemporaryDir scope;
        const QDir root(scope.path());
        makeRepository(root.path());
        makeRepository(root.filePath(QStringLiteral("app")));
        makeRepository(root.filePath(QStringLiteral("app/plugins/lib")));
        makeRepository(root.filePath(QStringLiteral("node_modules/pkg")));
        makeRepository(root.filePath(QStringLiteral(".cache/repo")));
        makeRepository(root.filePath(QStringLiteral("vendor")));

        const QString top = root.absolutePath();
        const QString app = root.absoluteFilePath(QStringLiteral("app"));
        const QString lib = root.absoluteFilePath(QStringLiteral("app/plugins/lib"));
        QCOMPARE(findRepositories(top, 0, {}), (QStringList{top}));
        QCOMPARE(findRepositories(top, 1, {QStringLiteral("vendor")}), (QStringList{top, app}));
        QCOMPARE(findRepositories(top, 3, {QStringLiteral("vendor")}), (QStringList{top, app, lib}));
    }

    // Scoping a folder inside a repository still tracks that repository.
    void discoveryFindsTheEnclosingRepository()
    {
        QTemporaryDir scope;
        const QDir root(scope.path());
        makeRepository(root.filePath(QStringLiteral("project")));
        QDir().mkpath(root.filePath(QStringLiteral("project/src/deep")));
        QCOMPARE(findRepositories(root.absoluteFilePath(QStringLiteral("project/src/deep")), 0, {}),
                 (QStringList{root.absoluteFilePath(QStringLiteral("project"))}));
    }

    // A submodule's .git is a file naming the real directory, which holds
    // MERGE_HEAD during a merge.
    void gitDirectoryFollowsGitFile()
    {
        QTemporaryDir scope;
        const QDir root(scope.path());
        QDir().mkpath(root.filePath(QStringLiteral("sub")));
        QFile file(root.filePath(QStringLiteral("sub/.git")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("gitdir: ../.git/modules/sub\n");
        file.close();
        QCOMPARE(gitDirectory(root.absoluteFilePath(QStringLiteral("sub"))),
                 root.absoluteFilePath(QStringLiteral(".git/modules/sub")));
    }
};

QTEST_GUILESS_MAIN(GitTests)

#include "GitTests.moc"
