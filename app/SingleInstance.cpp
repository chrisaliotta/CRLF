#include "app/SingleInstance.h"

#include <QCoreApplication>
#include <QLocalSocket>

namespace {
// A function rather than a static constant: the application name is only
// known once main() has configured QCoreApplication.
QString serverName()
{
    return QCoreApplication::applicationName() + QStringLiteral("-single-instance");
}
}

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
{
    const QString name = serverName();
    QLocalSocket probe;
    probe.connectToServer(name);
    if (probe.waitForConnected(200)) {
        probe.disconnectFromServer();
        return;
    }

    // No live primary. Remove any socket left behind by a crashed instance,
    // then claim the name.
    QLocalServer::removeServer(name);
    m_server.listen(name);
    m_primary = true;

    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        QLocalSocket *socket = m_server.nextPendingConnection();
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            const QStringList paths = QString::fromUtf8(socket->readAll())
                                          .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            socket->deleteLater();
            emit pathsReceived(paths);
        });
    });
}

void SingleInstance::forwardToPrimary(const QStringList &paths) const
{
    QLocalSocket socket;
    socket.connectToServer(serverName());
    socket.waitForConnected(1000);
    // Always ends with a newline so the primary's readyRead fires even for a
    // bare "raise the window" launch with no files.
    socket.write((paths.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
    socket.waitForBytesWritten(1000);
    socket.disconnectFromServer();
}
