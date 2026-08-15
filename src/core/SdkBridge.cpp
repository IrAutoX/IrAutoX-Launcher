#include "core/SdkBridge.h"

#include <QDateTime>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

namespace irautox {

SdkBridge::SdkBridge(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, &SdkBridge::onConnection);
}

bool SdkBridge::listen(quint16 port)
{
    if (m_server.isListening())
        return true;
    return m_server.listen(QHostAddress::LocalHost, port);
}

void SdkBridge::setIdentity(qint64 userId, const QString &username)
{
    m_userId = userId;
    m_username = username;
}

void SdkBridge::onConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        if (socket->peerAddress() != QHostAddress::LocalHost && socket->peerAddress() != QHostAddress::LocalHostIPv6) {
            socket->disconnectFromHost();
            socket->deleteLater();
            continue;
        }
        m_sessions.insert(socket, Session{});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            clearSession(socket);
            m_sessions.remove(socket);
            socket->deleteLater();
        });
    }
}

void SdkBridge::onReadyRead(QTcpSocket *socket)
{
    if (!m_sessions.contains(socket))
        return;
    Session &session = m_sessions[socket];
    session.buffer += socket->readAll();
    while (true) {
        const qsizetype newline = session.buffer.indexOf('\n');
        if (newline < 0)
            break;
        const QByteArray line = session.buffer.left(newline).trimmed();
        session.buffer.remove(0, newline + 1);
        if (!line.isEmpty())
            processLine(socket, line);
    }
}

void SdkBridge::processLine(QTcpSocket *socket, const QByteArray &line)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        send(socket, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("invalid_json")}});
        return;
    }

    const QJsonObject object = document.object();
    const QString command = object.value(QStringLiteral("cmd")).toString().trimmed().toLower();
    Session &session = m_sessions[socket];

    if (command == QStringLiteral("hello")) {
        session.gameId = object.value(QStringLiteral("game_id")).toVariant().toLongLong();
        session.gameName = object.value(QStringLiteral("game_name")).toString().trimmed();
        if (session.gameId <= 0) {
            send(socket, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("invalid_game_id")}});
            return;
        }
        send(socket, {
            {QStringLiteral("ok"), true},
            {QStringLiteral("type"), QStringLiteral("hello")},
            {QStringLiteral("sdk_protocol"), 1},
            {QStringLiteral("user_id"), m_userId},
            {QStringLiteral("username"), m_username}
        });
        return;
    }

    if (command == QStringLiteral("get_user")) {
        send(socket, {
            {QStringLiteral("ok"), m_userId > 0},
            {QStringLiteral("user_id"), m_userId},
            {QStringLiteral("username"), m_username}
        });
        return;
    }

    if (command == QStringLiteral("get_status")) {
        send(socket, {
            {QStringLiteral("ok"), true},
            {QStringLiteral("game_id"), session.gameId},
            {QStringLiteral("playing"), session.playing},
            {QStringLiteral("details"), session.details},
            {QStringLiteral("state"), session.state},
            {QStringLiteral("party_size"), session.partySize},
            {QStringLiteral("party_max"), session.partyMax}
        });
        return;
    }

    if (command == QStringLiteral("presence")) {
        if (session.gameId <= 0)
            session.gameId = object.value(QStringLiteral("game_id")).toVariant().toLongLong();
        if (session.gameId <= 0) {
            send(socket, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("hello_required")}});
            return;
        }
        const bool playing = object.value(QStringLiteral("playing")).toBool(true);
        session.details = object.value(QStringLiteral("details")).toString();
        session.state = object.value(QStringLiteral("state")).toString();
        session.partySize = qMax(0, object.value(QStringLiteral("party_size")).toInt());
        session.partyMax = qMax(session.partySize, object.value(QStringLiteral("party_max")).toInt());
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        if (playing && !session.playing)
            session.startedAt = now;
        const qint64 elapsed = session.startedAt > 0 ? qMax<qint64>(0, now - session.startedAt) : 0;
        session.playing = playing;
        emit presenceChanged(session.gameId, playing, elapsed, session.details, session.state,
                             session.partySize, session.partyMax, QStringLiteral("sdk"));
        if (!playing)
            session.startedAt = 0;
        send(socket, {{QStringLiteral("ok"), true}, {QStringLiteral("type"), QStringLiteral("presence")}});
        return;
    }

    if (command == QStringLiteral("heartbeat")) {
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const qint64 elapsed = session.startedAt > 0 ? qMax<qint64>(0, now - session.startedAt) : 0;
        if (session.playing && session.gameId > 0)
            emit presenceChanged(session.gameId, true, elapsed, session.details, session.state,
                                 session.partySize, session.partyMax, QStringLiteral("sdk"));
        send(socket, {{QStringLiteral("ok"), true}, {QStringLiteral("type"), QStringLiteral("heartbeat")}});
        return;
    }

    if (command == QStringLiteral("clear_presence") || command == QStringLiteral("shutdown")) {
        clearSession(socket);
        send(socket, {{QStringLiteral("ok"), true}, {QStringLiteral("type"), command}});
        if (command == QStringLiteral("shutdown"))
            socket->disconnectFromHost();
        return;
    }

    send(socket, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("unknown_command")}});
}

void SdkBridge::send(QTcpSocket *socket, const QJsonObject &object)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
        return;
    socket->write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}

void SdkBridge::clearSession(QTcpSocket *socket)
{
    if (!m_sessions.contains(socket))
        return;
    Session &session = m_sessions[socket];
    if (session.playing && session.gameId > 0) {
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const qint64 elapsed = session.startedAt > 0 ? qMax<qint64>(0, now - session.startedAt) : 0;
        emit presenceChanged(session.gameId, false, elapsed, session.details, session.state,
                             session.partySize, session.partyMax, QStringLiteral("sdk"));
    }
    session.playing = false;
    session.startedAt = 0;
}

}
