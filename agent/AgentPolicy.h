#pragma once

#include <QSet>
#include <QString>

// How the editor answers one kind of agent request. Review applies to writes:
// the change is shown as a diff and waits to be accepted.
enum class AgentAccess { Allow, Ask, Review, Deny };

// Read: files, editor state, problems, git status, and the terminal's text.
// Write: fs/write_text_file. Terminal: running a command or typing into the
// user's terminal.
enum class AgentAction { Read, Write, Terminal };

// The `permissions` object of one `.crlf` agent entry.
struct AgentPermissions
{
    AgentAccess read = AgentAccess::Allow;
    AgentAccess write = AgentAccess::Review;
    AgentAccess terminal = AgentAccess::Ask;
};

// allow, ask, review, or deny. Anything else is `fallback`.
AgentAccess accessNamed(const QString &name, AgentAccess fallback);

// Decides each request from the agent. The paths an agent sends come from a
// model, so they are the one input the editor checks; nothing past this
// point does. See docs/agent.md.
class AgentPolicy
{
public:
    AgentPolicy() = default;
    // `root` is the session's working directory.
    AgentPolicy(const QString &root, const AgentPermissions &permissions);

    // Absolute and clean. A relative path is resolved against the root.
    QString resolve(const QString &path) const;
    bool inside(const QString &path) const;
    // The project's setting for `action`, after any grant. A path outside the
    // root is never allowed without asking.
    AgentAccess decide(AgentAction action, const QString &path) const;
    // "Always allow" for the rest of the session, inside the root.
    void grant(AgentAction action);

private:
    QString m_root;
    AgentPermissions m_permissions;
    QSet<int> m_granted;
};
