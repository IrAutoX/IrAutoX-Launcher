#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

namespace irautox {

class GameLibrary;

class PresenceMonitor final : public QObject {
    Q_OBJECT
public:
    explicit PresenceMonitor(GameLibrary *library, QObject *parent = nullptr);
    void start();
    void stop();

signals:
    void presenceChanged(qint64 gameId, bool playing, qint64 elapsedSeconds, const QString &source);

private slots:
    void scan();

private:
    GameLibrary *m_library = nullptr;
    QTimer m_timer;
    qint64 m_activeGameId = 0;
    qint64 m_startedAt = 0;
    qint64 m_lastHeartbeatAt = 0;
};

}
