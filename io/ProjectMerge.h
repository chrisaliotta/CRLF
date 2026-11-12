#pragma once

#include <QJsonObject>
#include <QList>

// A project's settings come from several files: every <name>.crlf in the
// project root, and application.crlf when a workspace file asks for it with
// `inherit`. These functions work on the parsed objects and touch no file.
// See docs/crlf.md.

// True when the first workspace file that has `inherit` sets it to true or
// to a list of keys. `inherit` inside application.crlf means nothing.
bool inheritsApplication(const QList<QJsonObject> &workspace);

// The part of `application` the workspace takes in. Every key when there is
// no workspace file or `inherit` is true, the listed keys when it is a list,
// and nothing otherwise.
QJsonObject inheritedSettings(const QList<QJsonObject> &workspace, const QJsonObject &application);

// One object from the workspace files, in the order given, followed by the
// inherited part of `application`. The earlier file wins:
//   commands, agents   joined; an entry whose `name` was already seen is dropped
//   groups             joined; compared by `name` and `path` together
//   hide               the union
//   icons              `theme` from the first file that sets it; `rules` joined
//   indent             `style`, `size`, and `keep` from the first file that sets
//                      each; `types` joined
//   any other key      taken whole from the first file that has it
// `inherit` is not part of the result.
QJsonObject mergeProjects(const QList<QJsonObject> &workspace, const QJsonObject &application);
