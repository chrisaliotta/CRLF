#include "git/GitTypes.h"

GitBadge badgeFor(const GitChange &change)
{
    if (change.conflicted)
        return {QLatin1Char('!'), GitTone::Conflicted};
    if (change.untracked())
        return {QLatin1Char('U'), GitTone::Added};
    if (change.index == QLatin1Char('D') || change.worktree == QLatin1Char('D'))
        return {QLatin1Char('D'), GitTone::Deleted};
    if (change.index == QLatin1Char('A'))
        return {QLatin1Char('A'), GitTone::Added};
    if (change.index == QLatin1Char('R'))
        return {QLatin1Char('R'), GitTone::Modified};
    return {QLatin1Char('M'), GitTone::Modified};
}
