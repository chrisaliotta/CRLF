#include "git/LineDiff.h"

#include <QHash>
#include <QVector>

namespace {

// Lines are compared as ids, one per distinct text, so the inner loops
// compare integers.
class Differ
{
public:
    Differ(const QVector<int> &before, const QVector<int> &after)
        : a(before)
        , b(after)
    {
    }

    // The two ranges [aLo, aHi) and [bLo, bHi) after their common head and
    // tail are dropped.
    void run(int aLo, int aHi, int bLo, int bHi)
    {
        while (aLo < aHi && bLo < bHi && a[aLo] == b[bLo]) {
            ++aLo;
            ++bLo;
        }
        while (aLo < aHi && bLo < bHi && a[aHi - 1] == b[bHi - 1]) {
            --aHi;
            --bHi;
        }
        if (aLo == aHi && bLo == bHi)
            return;
        if (aLo == aHi || bLo == bHi) {
            changes.append({aLo, aHi - aLo, bLo, bHi - bLo});
            return;
        }
        bisect(aLo, aHi, bLo, bHi);
    }

    QList<LineChange> changes;

private:
    // Finds the middle snake by walking forward from the start and backward
    // from the end until the two paths overlap, then diffs each half.
    void bisect(int aLo, int aHi, int bLo, int bHi)
    {
        const int n = aHi - aLo;
        const int m = bHi - bLo;
        const int maxD = (n + m + 1) / 2;
        const int offset = maxD;
        const int length = 2 * maxD + 2;
        QVector<int> forward(length, -1);
        QVector<int> backward(length, -1);
        forward[offset + 1] = 0;
        backward[offset + 1] = 0;
        const int delta = n - m;
        // An odd delta means the forward path is the one that can reach the
        // overlap first.
        const bool front = delta % 2 != 0;
        int k1Start = 0;
        int k1End = 0;
        int k2Start = 0;
        int k2End = 0;
        for (int d = 0; d < maxD; ++d) {
            for (int k1 = -d + k1Start; k1 <= d - k1End; k1 += 2) {
                const int k1Offset = offset + k1;
                int x1 = (k1 == -d || (k1 != d && forward[k1Offset - 1] < forward[k1Offset + 1]))
                             ? forward[k1Offset + 1]
                             : forward[k1Offset - 1] + 1;
                int y1 = x1 - k1;
                while (x1 < n && y1 < m && a[aLo + x1] == b[bLo + y1]) {
                    ++x1;
                    ++y1;
                }
                forward[k1Offset] = x1;
                if (x1 > n) {
                    k1End += 2;
                } else if (y1 > m) {
                    k1Start += 2;
                } else if (front) {
                    const int k2Offset = offset + delta - k1;
                    if (k2Offset >= 0 && k2Offset < length && backward[k2Offset] != -1
                        && x1 >= n - backward[k2Offset]) {
                        split(aLo, aHi, bLo, bHi, x1, y1);
                        return;
                    }
                }
            }
            for (int k2 = -d + k2Start; k2 <= d - k2End; k2 += 2) {
                const int k2Offset = offset + k2;
                int x2 = (k2 == -d || (k2 != d && backward[k2Offset - 1] < backward[k2Offset + 1]))
                             ? backward[k2Offset + 1]
                             : backward[k2Offset - 1] + 1;
                int y2 = x2 - k2;
                while (x2 < n && y2 < m && a[aHi - 1 - x2] == b[bHi - 1 - y2]) {
                    ++x2;
                    ++y2;
                }
                backward[k2Offset] = x2;
                if (x2 > n) {
                    k2End += 2;
                } else if (y2 > m) {
                    k2Start += 2;
                } else if (!front) {
                    const int k1Offset = offset + delta - k2;
                    if (k1Offset >= 0 && k1Offset < length && forward[k1Offset] != -1) {
                        const int x1 = forward[k1Offset];
                        const int y1 = offset + x1 - k1Offset;
                        if (x1 >= n - x2) {
                            split(aLo, aHi, bLo, bHi, x1, y1);
                            return;
                        }
                    }
                }
            }
        }
        changes.append({aLo, n, bLo, m});
    }

    void split(int aLo, int aHi, int bLo, int bHi, int x, int y)
    {
        run(aLo, aLo + x, bLo, bLo + y);
        run(aLo + x, aHi, bLo + y, bHi);
    }

    const QVector<int> &a;
    const QVector<int> &b;
};

} // namespace

QList<LineChange> diffLines(const QStringList &before, const QStringList &after)
{
    QHash<QString, int> ids;
    const auto encode = [&ids](const QStringList &lines) {
        QVector<int> encoded;
        encoded.reserve(lines.size());
        for (const QString &line : lines) {
            auto id = ids.constFind(line);
            if (id == ids.cend())
                id = ids.insert(line, int(ids.size()));
            encoded.append(id.value());
        }
        return encoded;
    };
    const QVector<int> a = encode(before);
    const QVector<int> b = encode(after);
    Differ differ(a, b);
    differ.run(0, int(a.size()), 0, int(b.size()));

    // The halves of a split are reported separately. Runs that touch are one
    // change to the reader.
    QList<LineChange> merged;
    for (const LineChange &change : differ.changes) {
        if (!merged.isEmpty()) {
            LineChange &last = merged.last();
            if (last.oldStart + last.oldCount == change.oldStart
                && last.newStart + last.newCount == change.newStart) {
                last.oldCount += change.oldCount;
                last.newCount += change.newCount;
                continue;
            }
        }
        merged.append(change);
    }
    return merged;
}

QStringList splitLines(const QString &text)
{
    if (text.isEmpty())
        return {};
    QStringList lines = text.split(QLatin1Char('\n'));
    if (lines.last().isEmpty())
        lines.removeLast();
    return lines;
}
