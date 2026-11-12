#pragma once

#include <QString>

// How many files one copy wrote for the first time, overwrote, and left as
// they were. The settings file counts: copied when it was created, skipped
// when the folder already had one. `failed` counts the files the file
// system refused to write.
struct SeedCounts
{
    int copied = 0;
    int replaced = 0;
    int skipped = 0;
    int failed = 0;
};

// File > Copy Application Files to Workspace. Gives `target` its own copy of
// the application setup, so the workspace no longer depends on the
// application folder or the install folder for its scripts and rules.
//
//   workspace.crlf   created from application.crlf when `target` has no
//                    <name>.crlf file. In the copy, ${programDir}/scripts and
//                    ${appDir}/scripts become .crlf/scripts, and
//                    ${appDir}/rules becomes .crlf/rules. An existing
//                    settings file is never replaced or edited.
//   .crlf/scripts    the files under `shippedScripts`, then the application's
//                    .crlf/scripts, which wins on a shared path.
//   .crlf/rules      the application's .crlf/rules.
//
// A script or rule already in the workspace is skipped, or overwritten when
// `replaceExisting` is set. Nothing is deleted, and key files are never
// copied: a workspace is usually a git repository.
SeedCounts seedWorkspace(const QString &applicationFolder, const QString &shippedScripts, const QString &target,
                         bool replaceExisting);
