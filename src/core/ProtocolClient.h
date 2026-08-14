#pragma once

#include "core/MessageCodec.h"

#include <QAbstractSocket>
#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QTcpSocket>
#include <QTimer>

namespace irautox {

class ProtocolClient final : public QObject {
    Q_OBJECT
public:
    explicit ProtocolClient(QObject *parent = nullptr);

    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();
    void sendCommand(const QString &command, const QJsonObject &data = {});
    bool isConnected() const;

signals:
    void messageReceived(const QJsonObject &message);
    void connectionStateChanged(bool connected, const QString &detail);
    void protocolError(const QString &message);

private slots:
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void reconnect();

private:
    void scheduleReconnect();
    void writeFrame(const QByteArray &frame);

    QTcpSocket m_socket;
    MessageCodec m_codec;
    QQueue<QByteArray> m_pendingFrames;
    QTimer m_reconnectTimer;
    QString m_host;
    quint16 m_port = 0;
    int m_reconnectAttempt = 0;
    bool m_wantConnection = false;
};

} // namespace irautox

