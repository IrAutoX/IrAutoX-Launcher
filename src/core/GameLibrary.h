#pragma once

#include <QDateTime>
#include <QMap>
#include <QObject>
#include <QString>

namespace irautox {

struct InstalledGame {
    qint64 id = 0;
    QString name;
    QString version;
    QString rootPath;
    QString executable;
    QString archiveSha256;
    QString executableSha256;
    QString launchArguments;
    QDateTime installedAt;
    QDateTime lastPlayedAt;
    qint64 playtimeSeconds = 0;
    bool favorite = false;
};

class GameLibrary final : public QObject {
    Q_OBJECT
public:
    explicit GameLibrary(QObject *parent = nullptr);

    bool load(QString *error = nullptr);
    bool save(QString *error = nullptr) const;
    const QMap<qint64, InstalledGame> &games() const;
    const InstalledGame *find(qint64 gameId) const;
    void upsert(InstalledGame game);
    bool forget(qint64 gameId);
    bool setFavorite(qint64 gameId, bool favorite);
    bool updatePlaytime(qint64 gameId, qint64 elapsedSeconds);
    QString storagePath() const;

signals:
    void changed();

private:
    bool importLegacy(QString *error);

    QMap<qint64, InstalledGame> m_games;
};

} // namespace irautox

Q_DECLARE_METATYPE(irautox::InstalledGame)

