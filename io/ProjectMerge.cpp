#include "io/ProjectMerge.h"

#include <QJsonArray>
#include <QSet>
#include <QString>

namespace {

const QLatin1String kInherit("inherit");

// What tells two entries of a named list apart. A group is one folder in
// one place, so its path is part of it.
QString identity(const QString &key, const QJsonObject &entry)
{
    const QString name = entry.value(QLatin1String("name")).toString();
    if (key == QLatin1String("groups"))
        return name + QLatin1Char('\n') + entry.value(QLatin1String("path")).toString();
    return name;
}

QJsonArray joinNamed(const QString &key, const QJsonArray &first, const QJsonArray &later)
{
    QJsonArray joined;
    QSet<QString> seen;
    for (const QJsonArray &list : {first, later}) {
        for (const QJsonValue &entry : list) {
            const QString id = identity(key, entry.toObject());
            if (seen.contains(id))
                continue;
            seen.insert(id);
            joined.append(entry);
        }
    }
    return joined;
}

QJsonArray joinUnique(const QJsonArray &first, const QJsonArray &later)
{
    QJsonArray joined = first;
    for (const QJsonValue &entry : later) {
        if (!joined.contains(entry))
            joined.append(entry);
    }
    return joined;
}

// `icons` and `indent`: each field comes from the first file that sets it,
// and the one list inside (`rules`, `types`) is joined.
QJsonObject joinSection(const QJsonObject &first, const QJsonObject &later, const QString &listKey)
{
    QJsonObject joined = first;
    for (auto field = later.constBegin(); field != later.constEnd(); ++field) {
        if (field.key() == listKey) {
            QJsonArray list = joined.value(listKey).toArray();
            for (const QJsonValue &entry : field.value().toArray())
                list.append(entry);
            joined.insert(listKey, list);
        } else if (!joined.contains(field.key())) {
            joined.insert(field.key(), field.value());
        }
    }
    return joined;
}

void mergeInto(QJsonObject &merged, const QJsonObject &file)
{
    for (auto setting = file.constBegin(); setting != file.constEnd(); ++setting) {
        const QString key = setting.key();
        const QJsonValue value = setting.value();
        if (key == kInherit)
            continue;
        if (key == QLatin1String("commands") || key == QLatin1String("agents") || key == QLatin1String("groups"))
            merged.insert(key, joinNamed(key, merged.value(key).toArray(), value.toArray()));
        else if (key == QLatin1String("hide"))
            merged.insert(key, joinUnique(merged.value(key).toArray(), value.toArray()));
        else if (key == QLatin1String("icons"))
            merged.insert(key, joinSection(merged.value(key).toObject(), value.toObject(), QStringLiteral("rules")));
        else if (key == QLatin1String("indent"))
            merged.insert(key, joinSection(merged.value(key).toObject(), value.toObject(), QStringLiteral("types")));
        else if (!merged.contains(key))
            merged.insert(key, value);
    }
}

// `inherit` as written in the first workspace file that has it.
QJsonValue inheritValue(const QList<QJsonObject> &workspace)
{
    for (const QJsonObject &file : workspace) {
        if (file.contains(kInherit))
            return file.value(kInherit);
    }
    return {};
}

} // namespace

bool inheritsApplication(const QList<QJsonObject> &workspace)
{
    const QJsonValue inherit = inheritValue(workspace);
    return inherit.toBool() || !inherit.toArray().isEmpty();
}

QJsonObject inheritedSettings(const QList<QJsonObject> &workspace, const QJsonObject &application)
{
    const QJsonValue inherit = inheritValue(workspace);
    if (workspace.isEmpty() || inherit.toBool())
        return application;
    QJsonObject inherited;
    for (const QJsonValue &key : inherit.toArray()) {
        const QString name = key.toString();
        if (application.contains(name))
            inherited.insert(name, application.value(name));
    }
    return inherited;
}

QJsonObject mergeProjects(const QList<QJsonObject> &workspace, const QJsonObject &application)
{
    QJsonObject merged;
    for (const QJsonObject &file : workspace)
        mergeInto(merged, file);
    mergeInto(merged, inheritedSettings(workspace, application));
    return merged;
}
