#include "agent/AgentPolicy.h"

#include <QDir>

namespace {

#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif

} // namespace

AgentAccess accessNamed(const QString &name, AgentAccess fallback)
{
    if (name == QLatin1String("allow"))
        return AgentAccess::Allow;
    if (name == QLatin1String("ask"))
        return AgentAccess::Ask;
    if (name == QLatin1String("review"))
        return AgentAccess::Review;
    if (name == QLatin1String("deny"))
        return AgentAccess::Deny;
    return fallback;
}

AgentPolicy::AgentPolicy(const QString &root, const AgentPermissions &permissions)
    : m_root(QDir::cleanPath(QDir::fromNativeSeparators(root)))
    , m_permissions(permissions)
{
}

QString AgentPolicy::resolve(const QString &path) const
{
    const QString native = QDir::fromNativeSeparators(path);
    return QDir::cleanPath(QDir::isAbsolutePath(native) ? native : m_root + QLatin1Char('/') + native);
}

bool AgentPolicy::inside(const QString &path) const
{
    const QString clean = resolve(path);
    return clean.compare(m_root, kPathCase) == 0 || clean.startsWith(m_root + QLatin1Char('/'), kPathCase);
}

AgentAccess AgentPolicy::decide(AgentAction action, const QString &path) const
{
    AgentAccess access = action == AgentAction::Read    ? m_permissions.read
                         : action == AgentAction::Write ? m_permissions.write
                                                        : m_permissions.terminal;
    if (access == AgentAccess::Deny)
        return access;
    const bool within = inside(path);
    if (within && m_granted.contains(int(action)))
        return AgentAccess::Allow;
    if (!within && access == AgentAccess::Allow)
        return AgentAccess::Ask;
    return access;
}

void AgentPolicy::grant(AgentAction action)
{
    m_granted.insert(int(action));
}
