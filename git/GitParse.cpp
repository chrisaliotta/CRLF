#include "git/GitParse.h"

namespace {

// The first `count` space-separated fields, then everything after them as one
// last field. A path is that last field, so a space inside it survives.
QList<QByteArray> fields(const QByteArray &record, int count)
{
    QList<QByteArray> parts;
    int start = 0;
    for (int i = 0; i < count; ++i) {
        const int space = record.indexOf(' ', start);
        parts.append(record.mid(start, space - start));
        start = space + 1;
    }
    parts.append(record.mid(start));
    return parts;
}

GitChange changeFrom(const QByteArray &letters, const QByteArray &path)
{
    GitChange change;
    change.index = QLatin1Char(letters.at(0));
    change.worktree = QLatin1Char(letters.at(1));
    change.path = QString::fromUtf8(path);
    return change;
}

} // namespace

QStringList statusArguments()
{
    return {QStringLiteral("status"), QStringLiteral("--porcelain=v2"), QStringLiteral("--branch"),
            QStringLiteral("-z"), QStringLiteral("--untracked-files=all")};
}

GitStatus parseStatus(const QByteArray &output)
{
    GitStatus status;
    const QList<QByteArray> records = output.split('\0');
    for (int i = 0; i < records.size(); ++i) {
        const QByteArray &record = records.at(i);
        if (record.isEmpty())
            continue;
        switch (record.at(0)) {
        case '#': {
            const QList<QByteArray> header = fields(record, 2);
            const QByteArray &key = header.at(1);
            const QString value = QString::fromUtf8(header.at(2));
            if (key == "branch.oid") {
                status.head = value;
            } else if (key == "branch.head") {
                status.branch = value == QLatin1String("(detached)") ? QString() : value;
            } else if (key == "branch.upstream") {
                status.upstream = value;
            } else if (key == "branch.ab") {
                const QStringList counts = value.split(QLatin1Char(' '));
                status.ahead = counts.at(0).mid(1).toInt();
                status.behind = counts.at(1).mid(1).toInt();
            }
            break;
        }
        case '1': {
            const QList<QByteArray> parts = fields(record, 8);
            status.changes.append(changeFrom(parts.at(1), parts.at(8)));
            break;
        }
        case '2': {
            const QList<QByteArray> parts = fields(record, 9);
            GitChange change = changeFrom(parts.at(1), parts.at(9));
            change.originalPath = QString::fromUtf8(records.at(++i));
            status.changes.append(change);
            break;
        }
        case 'u': {
            const QList<QByteArray> parts = fields(record, 10);
            GitChange change = changeFrom(parts.at(1), parts.at(10));
            change.conflicted = true;
            status.changes.append(change);
            break;
        }
        case '?': {
            GitChange change;
            change.index = QLatin1Char('?');
            change.worktree = QLatin1Char('?');
            change.path = QString::fromUtf8(record.mid(2));
            status.changes.append(change);
            break;
        }
        default:
            break;
        }
    }
    return status;
}

QStringList branchArguments()
{
    return {QStringLiteral("for-each-ref"), QStringLiteral("--format=%(HEAD)%1f%(refname)%1f%(upstream:short)"),
            QStringLiteral("refs/heads"), QStringLiteral("refs/remotes")};
}

QList<GitBranch> parseBranches(const QByteArray &output)
{
    const QString localPrefix = QStringLiteral("refs/heads/");
    const QString remotePrefix = QStringLiteral("refs/remotes/");
    QList<GitBranch> local;
    QList<GitBranch> remote;
    for (const QByteArray &line : output.split('\n')) {
        if (line.isEmpty())
            continue;
        const QStringList parts = QString::fromUtf8(line).split(QChar(0x1f));
        GitBranch branch;
        branch.current = parts.at(0) == QLatin1String("*");
        branch.upstream = parts.at(2);
        const QString &ref = parts.at(1);
        if (ref.startsWith(localPrefix)) {
            branch.name = ref.mid(localPrefix.size());
            local.append(branch);
        } else if (!ref.endsWith(QLatin1String("/HEAD"))) {
            branch.name = ref.mid(remotePrefix.size());
            branch.remote = true;
            remote.append(branch);
        }
    }
    return local + remote;
}

QStringList logArguments(int count, const QString &path)
{
    QStringList arguments{QStringLiteral("log"), QStringLiteral("-n"), QString::number(count),
                          QStringLiteral("--date=short"), QStringLiteral("--format=%H%x1f%an%x1f%ad%x1f%s%x1e")};
    if (!path.isEmpty())
        arguments << QStringLiteral("--") << path;
    return arguments;
}

QList<GitCommit> parseLog(const QByteArray &output)
{
    QList<GitCommit> commits;
    for (const QByteArray &record : output.split('\x1e')) {
        const QByteArray trimmed = record.trimmed();
        if (trimmed.isEmpty())
            continue;
        const QStringList parts = QString::fromUtf8(trimmed).split(QChar(0x1f));
        GitCommit commit;
        commit.id = parts.at(0);
        commit.author = parts.at(1);
        commit.date = parts.at(2);
        commit.subject = parts.at(3);
        commits.append(commit);
    }
    return commits;
}
