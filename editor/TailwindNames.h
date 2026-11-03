#pragma once

#include <QString>
#include <QStringList>

// A Tailwind class split into the parts that name its theme values.
// `hover:-mt-4/50` has utility `mt-4` and value `4`. `variables` are the
// theme custom properties the class reads, most specific first, such as
// `--color-brand-500` for `bg-brand-500` or `--spacing` for `p-4`.
struct TailwindClass {
    QString utility;
    QString value;
    QStringList variables;
};

TailwindClass parseTailwindClass(const QString &name);
