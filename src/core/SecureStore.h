#pragma once

#include <QByteArray>
#include <QString>
#include <utility>

namespace irautox {

class SecureStore final {
public:
    static bool saveCredentials(const QString &username, const QString &password);
    static std::pair<QString, QString> loadCredentials();
    static void clearCredentials();

private:
    static QByteArray protect(const QByteArray &plainText);
    static QByteArray unprotect(const QByteArray &cipherText);
};

} // namespace irautox
