#pragma once

#include <QList>
#include <QMetaType>
#include <QString>

// One path from `git status --porcelain=v2`. `index` and `worktree` are git's
// two status letters: '.' unchanged, 'M', 'A', 'D', 'R', 'C', or 'T'. An
// untracked file is '?' in both. A conflicted path sets `conflicted` and
// keeps git's letters, such as 'U' and 'U'.
struct GitChange
{
    // Relative to the repository root, with forward slashes.
    QString path;
    // The source of a rename or copy. Empty otherwise.
    QString originalPath;
    QChar index = QLatin1Char('.');
    QChar worktree = QLatin1Char('.');
    bool conflicted = false;

    bool untracked() const { return index == QLatin1Char('?'); }
    bool staged() const { return !conflicted && !untracked() && index != QLatin1Char('.'); }
    bool unstaged() const { return !conflicted && !untracked() && worktree != QLatin1Char('.'); }
};

struct GitStatus
{
    // Empty for a detached HEAD.
    QString branch;
    // The commit id, or "(initial)" before the first commit.
    QString head;
    // Empty when the branch tracks nothing.
    QString upstream;
    int ahead = 0;
    int behind = 0;
    // MERGE_HEAD exists: a merge is waiting for its commit or an abort.
    bool merging = false;
    QList<GitChange> changes;
};

struct GitBranch
{
    // "main" for a local branch, "origin/main" for a remote one.
    QString name;
    QString upstream;
    bool current = false;
    bool remote = false;
};

struct GitCommit
{
    QString id;
    QString author;
    // YYYY-MM-DD.
    QString date;
    QString subject;
};

// The explorer and the Source Control panel draw a change as one letter in
// one of four colors.
enum class GitTone { Added, Modified, Deleted, Conflicted };

struct GitBadge
{
    QChar letter;
    GitTone tone = GitTone::Modified;
};

// U untracked, A added, M modified, R renamed, D deleted, ! conflicted.
GitBadge badgeFor(const GitChange &change);

Q_DECLARE_METATYPE(GitStatus)
Q_DECLARE_METATYPE(GitBranch)
Q_DECLARE_METATYPE(GitCommit)
