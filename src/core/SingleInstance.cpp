#include "core/SingleInstance.h"

#include <QLocalServer>
#include <QLocalSocket>

#include <utility>

namespace irautox {

SingleInstance::SingleInstance(QString name, QObject *parent)
    : QObject(parent), m_name(std::move(name))
{
}

bool SingleInstance::acquire()
{
    QLocalSocket probe;
    probe.connectToServer(m_name, QIODevice::WriteOnly);
    if (probe.waitForConnected(180)) {
        probe.disconnectFromServer();
        return false;
    }

    QLocalServer::removeServer(m_name);
    m_server = new QLocalServer(this);
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (m_server && m_server->hasPendingConnections()) {
            QLocalSocket *socket = m_server->nextPendingConnection();
            if (!socket)
                continue;
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                const QString message = QString::fromUtf8(socket->readAll()).trimmed();
                if (!message.isEmpty())
                    emit messageReceived(message);
                socket->disconnectFromServer();
            });
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    return m_server->listen(m_name);
}

bool SingleInstance::sendMessage(const QString &message, int timeoutMs) const
{
    QLocalSocket socket;
    socket.connectToServer(m_name, QIODevice::WriteOnly);
    if (!socket.waitForConnected(timeoutMs))
        return false;
    const QByteArray payload = message.toUtf8();
    if (socket.write(payload) != payload.size())
        return false;
    if (!socket.waitForBytesWritten(timeoutMs))
        return false;
    socket.disconnectFromServer();
    return true;
}

} // namespace irautox
