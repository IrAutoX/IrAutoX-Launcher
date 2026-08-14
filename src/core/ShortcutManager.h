#pragma once

#include <QString>

namespace irautox {

class ShortcutManager final {
public:
    static bool createGameShortcut(qint64 gameId, const QString &gameName, const QString &iconPath, QString *error = nullptr);
    static QString shortcutPath(qint64 gameId, const QString &gameName);
};

} // namespace irautox
