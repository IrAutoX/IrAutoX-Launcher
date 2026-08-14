#include "core/GameLibrary.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <utility>

namespace irautox {
namespace {
QJsonObject toJson(const InstalledGame &game)
{
    return {
        {QStringLiteral("id"), game.id},
        {QStringLiteral("name"), game.name},
        {QStringLiteral("version"), game.version},
        {QStringLiteral("rootPath"), game.rootPath},
        {QStringLiteral("executable"), game.executable},
        {QStringLiteral("archiveSha256"), game.archiveSha256},
        {QStringLiteral("executableSha256"), game.executableSha256},
        {QStringLiteral("launchArguments"), game.launchArguments},
        {QStringLiteral("installedAt"), game.installedAt.toString(Qt::ISODate)},
        {QStringLiteral("lastPlayedAt"), game.lastPlayedAt.toString(Qt::ISODate)},
        {QStringLiteral("playtimeSeconds"), game.playtimeSeconds},
        {QStringLiteral("favorite"), game.favorite}
    };
}

InstalledGame fromJson(const QJsonObject &object)
{
    InstalledGame game;
    game.id = object.value(QStringLiteral("id")).toInteger();
    game.name = object.value(QStringLiteral("name")).toString();
    game.version = object.value(QStringLiteral("version")).toString(QStringLiteral("1.0"));
    game.rootPath = QDir::cleanPath(object.value(QStringLiteral("rootPath")).toString());
    game.executable = object.value(QStringLiteral("executable")).toString();
    game.archiveSha256 = object.value(QStringLiteral("archiveSha256")).toString();
    game.executableSha256 = object.value(QStringLiteral("executableSha256")).toString();
    game.launchArguments = object.value(QStringLiteral("launchArguments")).toString();
    game.installedAt = QDateTime::fromString(object.value(QStringLiteral("installedAt")).toString(), Qt::ISODate);
    game.lastPlayedAt = QDateTime::fromString(object.value(QStringLiteral("lastPlayedAt")).toString(), Qt::ISODate);
    game.playtimeSeconds = object.value(QStringLiteral("playtimeSeconds")).toInteger();
    game.favorite = object.value(QStringLiteral("favorite")).toBool();
    return game;
}
}

GameLibrary::GameLibrary(QObject *parent)
    : QObject(parent)
{
}

bool GameLibrary::load(QString *error)
{
    m_games.clear();
    QFile file(storagePath());
    if (!file.exists())
        return importLegacy(error);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = parseError.errorString();
        return false;
    }

    const QJsonObject entries = document.object().value(QStringLiteral("games")).toObject();
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        InstalledGame game = fromJson(it.value().toObject());
        if (game.id == 0)
            game.id = it.key().toLongLong();
        if (game.id > 0 && !game.rootPath.isEmpty())
            m_games.insert(game.id, game);
    }
    emit changed();
    return true;
}

bool GameLibrary::save(QString *error) const
{
    const QFileInfo destination(storagePath());
    if (!QDir().mkpath(destination.absolutePath())) {
        if (error)
            *error = tr("ساخت پوشهٔ اطلاعات لانچر ناموفق بود.");
        return false;
    }

    QJsonObject entries;
    for (auto it = m_games.constBegin(); it != m_games.constEnd(); ++it)
        entries.insert(QString::number(it.key()), toJson(it.value()));

    const QJsonObject root{
        {QStringLiteral("schemaVersion"), 2},
        {QStringLiteral("games"), entries}
    };
    QSaveFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

const QMap<qint64, InstalledGame> &GameLibrary::games() const
{
    return m_games;
}

const InstalledGame *GameLibrary::find(qint64 gameId) const
{
    const auto it = m_games.constFind(gameId);
    return it == m_games.constEnd() ? nullptr : &it.value();
}

void GameLibrary::upsert(InstalledGame game)
{
    if (!game.installedAt.isValid())
        game.installedAt = QDateTime::currentDateTimeUtc();
    m_games.insert(game.id, std::move(game));
    save();
    emit changed();
}

bool GameLibrary::forget(qint64 gameId)
{
    if (m_games.remove(gameId) == 0)
        return false;
    save();
    emit changed();
    return true;
}

bool GameLibrary::setFavorite(qint64 gameId, bool favorite)
{
    auto it = m_games.find(gameId);
    if (it == m_games.end())
        return false;
    it->favorite = favorite;
    save();
    emit changed();
    return true;
}

bool GameLibrary::updatePlaytime(qint64 gameId, qint64 elapsedSeconds)
{
    auto it = m_games.find(gameId);
    if (it == m_games.end())
        return false;
    it->playtimeSeconds += qMax<qint64>(0, elapsedSeconds);
    it->lastPlayedAt = QDateTime::currentDateTimeUtc();
    save();
    emit changed();
    return true;
}

QString GameLibrary::storagePath() const
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(root).filePath(QStringLiteral("library.json"));
}

bool GameLibrary::importLegacy(QString *error)
{
    const QString legacyPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("library.json"));
    QFile file(legacyPath);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return true;

    const QJsonObject legacy = document.object();
    for (auto it = legacy.constBegin(); it != legacy.constEnd(); ++it) {
        const QJsonObject oldGame = it.value().toObject();
        InstalledGame game;
        game.id = it.key().toLongLong();
        game.name = oldGame.value(QStringLiteral("name")).toString();
        game.version = oldGame.value(QStringLiteral("version")).toString(QStringLiteral("1.0"));
        game.rootPath = oldGame.value(QStringLiteral("path")).toString();
        game.executable = oldGame.value(QStringLiteral("exe")).toString();
        game.installedAt = QDateTime::currentDateTimeUtc();
        if (game.id > 0 && !game.rootPath.isEmpty())
            m_games.insert(game.id, game);
    }
    if (!m_games.isEmpty())
        return save(error);
    return true;
}

} // namespace irautox
