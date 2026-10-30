#include "io/SessionStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

SessionStore::SessionStore()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    m_filePath = dir + QLatin1String("/session.json");
}

void SessionStore::save(const Session &session) const
{
    QJsonArray tabs;
    for (const SessionTab &tab : session.tabs) {
        QJsonObject obj;
        obj.insert(QLatin1String("path"), tab.path);
        obj.insert(QLatin1String("title"), tab.title);
        obj.insert(QLatin1String("language"), tab.language);
        obj.insert(QLatin1String("hex"), tab.hex);
        obj.insert(QLatin1String("pinned"), tab.pinned);
        obj.insert(QLatin1String("modified"), tab.modified);
        obj.insert(QLatin1String("cursor"), tab.cursorPosition);
        if (tab.modified && tab.hex)
            obj.insert(QLatin1String("bytes"), QString::fromLatin1(tab.bytes.toBase64()));
        else if (tab.modified)
            obj.insert(QLatin1String("text"), tab.text);
        tabs.append(obj);
    }
    QJsonObject root;
    root.insert(QLatin1String("activeIndex"), session.activeIndex);
    root.insert(QLatin1String("tabs"), tabs);

    // QSaveFile writes to a temp file and renames atomically, so a crash or
    // kill mid-save can never corrupt the previous good session.
    QSaveFile file(m_filePath);
    (void)file.open(QIODevice::WriteOnly);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.commit();
}

Session SessionStore::load() const
{
    QFile file(m_filePath);
    (void)file.open(QIODevice::ReadOnly);
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();

    Session session;
    session.activeIndex = root.value(QLatin1String("activeIndex")).toInt();
    for (const QJsonValue &value : root.value(QLatin1String("tabs")).toArray()) {
        const QJsonObject obj = value.toObject();
        SessionTab tab;
        tab.path = obj.value(QLatin1String("path")).toString();
        tab.title = obj.value(QLatin1String("title")).toString();
        tab.language = obj.value(QLatin1String("language")).toString();
        tab.hex = obj.value(QLatin1String("hex")).toBool();
        tab.pinned = obj.value(QLatin1String("pinned")).toBool();
        tab.modified = obj.value(QLatin1String("modified")).toBool();
        tab.cursorPosition = obj.value(QLatin1String("cursor")).toInt();
        tab.text = obj.value(QLatin1String("text")).toString();
        tab.bytes = QByteArray::fromBase64(obj.value(QLatin1String("bytes")).toString().toLatin1());
        session.tabs.append(tab);
    }
    return session;
}

void SessionStore::clear() const
{
    QFile::remove(m_filePath);
}
