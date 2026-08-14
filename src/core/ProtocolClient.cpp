#include "core/ProtocolClient.h"

#include <QJsonDocument>

namespace irautox {

ProtocolClient::ProtocolClient(QObject *parent)
    : QObject(parent)
{
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &ProtocolClient::reconnect);
    connect(&m_socket, &QTcpSocket::connected, this, &ProtocolClient::onConnected);
    connect(&m_socket, &QTcpSocket::readyRead, this, &ProtocolClient::onReadyRead);
    connect(&m_socket, &QTcpSocket::disconnected, this, &ProtocolClient::onDisconnected);
    connect(&m_socket, &QTcpSocket::errorOccurred, this, &ProtocolClient::onSocketError);
}

void ProtocolClient::connectToServer(const QString &host, quint16 port)
{
    m_host = host.trimmed();
    m_port = port;
    m_wantConnection = true;
    m_reconnectAttempt = 0;
    m_reconnectTimer.stop();
    if (m_socket.state() != QAbstractSocket::UnconnectedState)
        m_socket.abort();
    emit connectionStateChanged(false, tr("در حال اتصال…"));
    m_socket.connectToHost(m_host, m_port);
}

void ProtocolClient::disconnectFromServer()
{
    m_wantConnection = false;
    m_reconnectTimer.stop();
    m_pendingFrames.clear();
    m_socket.disconnectFromHost();
}

void ProtocolClient::sendCommand(const QString &command, const QJsonObject &data)
{
    const QJsonObject root{{QStringLiteral("cmd"), command}, {QStringLiteral("data"), data}};
    const QByteArray frame = MessageCodec::encode(root);
    if (isConnected()) {
        writeFrame(frame);
        return;
    }

    if (m_pendingFrames.size() >= 100)
        m_pendingFrames.dequeue();
    m_pendingFrames.enqueue(frame);
    if (m_wantConnection && !m_reconnectTimer.isActive()
        && m_socket.state() == QAbstractSocket::UnconnectedState) {
        scheduleReconnect();
    }
}

bool ProtocolClient::isConnected() const
{
    return m_socket.state() == QAbstractSocket::ConnectedState;
}

void ProtocolClient::onConnected()
{
    m_reconnectAttempt = 0;
    emit connectionStateChanged(true, tr("آنلاین"));
    while (!m_pendingFrames.isEmpty())
        writeFrame(m_pendingFrames.dequeue());
}

void ProtocolClient::onReadyRead()
{
    QString error;
    const QList<QJsonObject> messages = m_codec.append(m_socket.readAll(), &error);
    if (!error.isEmpty())
        emit protocolError(error);
    for (const QJsonObject &message : messages)
        emit messageReceived(message);
}

void ProtocolClient::onDisconnected()
{
    m_codec.clear();
    emit connectionStateChanged(false, tr("آفلاین"));
    if (m_wantConnection)
        scheduleReconnect();
}

void ProtocolClient::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    emit connectionStateChanged(false, m_socket.errorString());
}

void ProtocolClient::reconnect()
{
    if (!m_wantConnection || m_host.isEmpty() || m_port == 0)
        return;
    emit connectionStateChanged(false, tr("اتصال مجدد…"));
    m_socket.abort();
    m_socket.connectToHost(m_host, m_port);
}

void ProtocolClient::scheduleReconnect()
{
    const int delay = qMin(30000, 1000 * (1 << qMin(m_reconnectAttempt, 5)));
    ++m_reconnectAttempt;
    m_reconnectTimer.start(delay);
}

void ProtocolClient::writeFrame(const QByteArray &frame)
{
    if (m_socket.write(frame) == -1)
        emit protocolError(m_socket.errorString());
}

} // namespace irautox

