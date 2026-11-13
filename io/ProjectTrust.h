#pragma once

#include "io/ProjectCommands.h"

#include <QHash>
#include <QList>
#include <QSettings>
#include <QString>
#include <QStringList>

// What a folder is allowed to run. The record lives in QSettings, never in
// the project, so a commit cannot trust itself. See docs/crlf.md.
enum class TrustState { Unknown, Restricted, Trusted };

enum class TrustDecision { Allow, Untrusted, Restricted, Mismatch };

// One executable entry that trust can hold. `templateText` is the unexpanded
// command plus the fields a pull can change without renaming a file: cwd,
// the function key, editor output, and an agent's permissions. `files` are
// the project scripts pinned for this entry, relative to the project root.
struct TrustEntry
{
    QString id;
    QString name;
    QString command;
    QString templateText;
    QStringList files;
    bool readsRules = false;
    bool gated = false;
};

struct PinnedFile
{
    QString relative;
    QString hash;
};

// The gated entries of one project and the scripts they pin. Entries that
// are not gated are left out.
struct ProjectPin
{
    QList<TrustEntry> entries;
    QList<PinnedFile> files;

    bool gates(const QString &id) const;
};

// What was accepted for one project root. `templates` are `id` then the
// template text. `files` are the relative paths and their SHA-256.
struct TrustTemplate
{
    QString id;
    QString text;
};

struct TrustRecord
{
    TrustState state = TrustState::Unknown;
    QList<TrustTemplate> templates;
    QList<PinnedFile> files;
};

// Commands whose template changed, and relative paths that were added,
// removed, or whose bytes changed.
struct TrustDifference
{
    QStringList commands;
    QStringList paths;
};

// Size and mtime of a file already hashed, so a syntax run does not read
// unchanged scripts again. Keyed by the absolute path.
struct TrustHashCache
{
    struct Stamp
    {
        qint64 size = -1;
        qint64 mtime = -1;
        QString hash;
    };
    QHash<QString, Stamp> files;
};

// The pin for `project`. A workspace entry is always gated. An application
// entry is gated when a path token names a project script, or `.crlf/rules`
// while that directory exists. `${...}` is not a path. Scripts are the
// named file's directory tree of .py, .sh, .cmd, .bat, and .ps1, skipping
// __pycache__, .crlf/keys, .crlf/data, and .crlf/rules.
ProjectPin pinProject(const Project &project, TrustHashCache *cache = nullptr);

// `record` is what was stored for this folder. Nothing gated is Allow.
// Restricted stays held. Trusted allows only an identical pin.
TrustDecision decideTrust(const ProjectPin &pin, const TrustRecord &record, TrustDifference *changes = nullptr);

// The text of the review dialog: each gated command, the scripts it pins,
// and an empty script set when the command names none.
QString trustReviewText(const ProjectPin &pin, const TrustDifference &changes);

// `workspaceTrust` in `settings`, keyed by the canonical project root.
TrustRecord loadTrust(QSettings &settings, const QString &root);
void saveTrust(QSettings &settings, const QString &root, const TrustRecord &record);
