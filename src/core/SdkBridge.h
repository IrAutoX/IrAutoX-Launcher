#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTcpServer>

class QTcpSocket;

namespace irautox {

class SdkBridge final : public QObject {
    Q_OBJECT
public:
    explicit SdkBridge(QObject *parent = nullptr);
    bool listen(quint16 port = 6769);
    void setIdentity(qint64 userId, const QString &username);

signals:
    void presenceChanged(qint64 gameId, bool playing, qint64 elapsedSeconds,
                         const QString &details, const QString &state,
                         int partySize, int partyMax, const QString &source);

private:
    struct Session {
        QByteArray buffer;
        qint64 gameId = 0;
        QString gameName;
        QString details;
        QString state;
        qint64 startedAt = 0;
        int partySize = 0;
        int partyMax = 0;
        bool playing = false;
    };

    void onConnection();
    void onReadyRead(QTcpSocket *socket);
    void processLine(QTcpSocket *socket, const QByteArray &line);
    void send(QTcpSocket *socket, const QJsonObject &object);
    void clearSession(QTcpSocket *socket);

    QTcpServer m_server;
    QHash<QTcpSocket *, Session> m_sessions;
    qint64 m_userId = 0;
    QString m_username;
};

}
