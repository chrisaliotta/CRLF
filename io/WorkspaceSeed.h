#pragma once

#include <QString>

// How many files one copy wrote for the first time, overwrote, and left as
// they were. The two settings files count: workspace.crlf is copied when it
// was created and skipped when the folder already had settings of its own.
// `failed` counts the files the file system refused to write.
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
//   application.crlf a copy of `applicationFile`, the active application
//                    file, whatever its name. In the copy,
//                    ${programDir}/scripts and ${appDir}/scripts become
//                    .crlf/scripts, and ${appDir}/rules becomes .crlf/rules.
//                    The workspace's other settings files are read before
//                    it, so they win over it.
//   workspace.crlf   created empty when `target` has no other <name>.crlf
//                    file. An existing one is never replaced or edited.
//   .crlf/scripts    the files under `shippedScripts`, then the application's
//                    .crlf/scripts, which wins on a shared path.
//   .crlf/rules      the application's .crlf/rules.
//
// application.crlf, a script, or a rule already in the workspace is skipped,
// or overwritten when `replaceExisting` is set. Nothing is deleted, and key
// files are never copied: a workspace is usually a git repository.
SeedCounts seedWorkspace(const QString &applicationFile, const QString &shippedScripts, const QString &target,
                         bool replaceExisting);
