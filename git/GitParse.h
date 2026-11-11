#pragma once

#include "git/GitTypes.h"

#include <QByteArray>
#include <QList>
#include <QStringList>

// Each pair is the git arguments and the reader for exactly that output. They
// live together because a format string and its parser change as one.

QStringList statusArguments();
// `git status --porcelain=v2 --branch -z`. `merging` is not in that output;
// the caller sets it.
GitStatus parseStatus(const QByteArray &output);

QStringList branchArguments();
// Local branches first, then remote ones. A remote's HEAD alias is dropped.
QList<GitBranch> parseBranches(const QByteArray &output);

// The newest `count` commits, limited to `path` when it is not empty.
QStringList logArguments(int count, const QString &path);
QList<GitCommit> parseLog(const QByteArray &output);
