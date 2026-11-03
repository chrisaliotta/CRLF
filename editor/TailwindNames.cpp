#include "editor/TailwindNames.h"

#include <QList>
#include <QRegularExpression>

#include <algorithm>

namespace {

struct Namespace {
    QString prefix;
    QStringList themes;
    bool spacing = false;
};

QList<Namespace> buildNamespaces()
{
    const QString color = QStringLiteral("--color-");
    QList<Namespace> out;
    const auto add = [&out](const QStringList &prefixes, const QStringList &themes, bool spacing = false) {
        for (const QString &prefix : prefixes)
            out.append({prefix, themes, spacing});
    };
    add({QStringLiteral("bg"), QStringLiteral("border"), QStringLiteral("border-x"), QStringLiteral("border-y"),
         QStringLiteral("border-t"), QStringLiteral("border-r"), QStringLiteral("border-b"), QStringLiteral("border-l"),
         QStringLiteral("border-s"), QStringLiteral("border-e"), QStringLiteral("outline"), QStringLiteral("ring"),
         QStringLiteral("ring-offset"), QStringLiteral("fill"), QStringLiteral("stroke"), QStringLiteral("decoration"),
         QStringLiteral("accent"), QStringLiteral("caret"), QStringLiteral("divide"), QStringLiteral("placeholder"),
         QStringLiteral("from"), QStringLiteral("via"), QStringLiteral("to")},
        {color});
    add({QStringLiteral("text")}, {QStringLiteral("--text-"), color});
    add({QStringLiteral("shadow")}, {QStringLiteral("--shadow-"), color});
    add({QStringLiteral("inset-shadow")}, {QStringLiteral("--inset-shadow-"), color});
    add({QStringLiteral("drop-shadow")}, {QStringLiteral("--drop-shadow-"), color});
    add({QStringLiteral("text-shadow")}, {QStringLiteral("--text-shadow-"), color});
    add({QStringLiteral("font")}, {QStringLiteral("--font-"), QStringLiteral("--font-weight-")});
    add({QStringLiteral("rounded"), QStringLiteral("rounded-t"), QStringLiteral("rounded-r"),
         QStringLiteral("rounded-b"), QStringLiteral("rounded-l"), QStringLiteral("rounded-tl"),
         QStringLiteral("rounded-tr"), QStringLiteral("rounded-br"), QStringLiteral("rounded-bl"),
         QStringLiteral("rounded-s"), QStringLiteral("rounded-e")},
        {QStringLiteral("--radius-")});
    add({QStringLiteral("blur"), QStringLiteral("backdrop-blur")}, {QStringLiteral("--blur-")});
    add({QStringLiteral("tracking")}, {QStringLiteral("--tracking-")});
    add({QStringLiteral("leading")}, {QStringLiteral("--leading-")}, true);
    add({QStringLiteral("ease")}, {QStringLiteral("--ease-")});
    add({QStringLiteral("animate")}, {QStringLiteral("--animate-")});
    add({QStringLiteral("perspective")}, {QStringLiteral("--perspective-")});
    add({QStringLiteral("aspect")}, {QStringLiteral("--aspect-")});
    add({QStringLiteral("p"), QStringLiteral("px"), QStringLiteral("py"), QStringLiteral("pt"), QStringLiteral("pr"),
         QStringLiteral("pb"), QStringLiteral("pl"), QStringLiteral("ps"), QStringLiteral("pe"), QStringLiteral("m"),
         QStringLiteral("mx"), QStringLiteral("my"), QStringLiteral("mt"), QStringLiteral("mr"), QStringLiteral("mb"),
         QStringLiteral("ml"), QStringLiteral("ms"), QStringLiteral("me"), QStringLiteral("gap"),
         QStringLiteral("gap-x"), QStringLiteral("gap-y"), QStringLiteral("space-x"), QStringLiteral("space-y"),
         QStringLiteral("inset"), QStringLiteral("inset-x"), QStringLiteral("inset-y"), QStringLiteral("top"),
         QStringLiteral("right"), QStringLiteral("bottom"), QStringLiteral("left"), QStringLiteral("start"),
         QStringLiteral("end"), QStringLiteral("translate-x"), QStringLiteral("translate-y"),
         QStringLiteral("scroll-m"), QStringLiteral("scroll-p"), QStringLiteral("indent"), QStringLiteral("h"),
         QStringLiteral("min-h"), QStringLiteral("max-h")},
        {QStringLiteral("--spacing-")}, true);
    add({QStringLiteral("w"), QStringLiteral("min-w"), QStringLiteral("max-w"), QStringLiteral("size"),
         QStringLiteral("basis"), QStringLiteral("columns")},
        {QStringLiteral("--container-"), QStringLiteral("--spacing-")}, true);
    std::stable_sort(out.begin(), out.end(), [](const Namespace &left, const Namespace &right) {
        return left.prefix.size() > right.prefix.size();
    });
    return out;
}

} // namespace

TailwindClass parseTailwindClass(const QString &name)
{
    static const QList<Namespace> namespaces = buildNamespaces();
    static const QRegularExpression numeric(QStringLiteral("^[\\d.]+$"));

    TailwindClass result;
    QString utility = name.mid(name.lastIndexOf(QLatin1Char(':')) + 1);
    while (utility.startsWith(QLatin1Char('!')) || utility.startsWith(QLatin1Char('-')))
        utility = utility.mid(1);
    if (utility.endsWith(QLatin1Char('!')))
        utility.chop(1);
    const int slash = utility.indexOf(QLatin1Char('/'));
    if (slash >= 0)
        utility = utility.left(slash);
    result.utility = utility;

    for (const Namespace &space : namespaces) {
        if (!utility.startsWith(space.prefix + QLatin1Char('-')))
            continue;
        result.value = utility.mid(space.prefix.size() + 1);
        for (const QString &theme : space.themes)
            result.variables.append(theme + result.value);
        if (space.spacing && numeric.match(result.value).hasMatch())
            result.variables.append(QStringLiteral("--spacing"));
        break;
    }
    return result;
}
