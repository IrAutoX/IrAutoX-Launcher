#include "core/SecureStore.h"

#include <QByteArray>
#include <QSettings>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <wincrypt.h>
#endif

namespace irautox {
namespace {
QSettings settings()
{
    return QSettings(QStringLiteral("IrAutoX"), QStringLiteral("Launcher"));
}
}

QByteArray SecureStore::protect(const QByteArray &plainText)
{
#ifdef Q_OS_WIN
    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plainText.constData()));
    input.cbData = static_cast<DWORD>(plainText.size());
    const QByteArray entropyBytes("IrAutoX.Launcher.Credentials.v2");
    DATA_BLOB entropy{};
    entropy.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(entropyBytes.constData()));
    entropy.cbData = static_cast<DWORD>(entropyBytes.size());
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"IrAutoX Launcher", &entropy, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return {};
    }
    const QByteArray protectedData(reinterpret_cast<const char *>(output.pbData),
                                   static_cast<qsizetype>(output.cbData));
    LocalFree(output.pbData);
    return protectedData;
#else
    return plainText.toBase64();
#endif
}

QByteArray SecureStore::unprotect(const QByteArray &cipherText)
{
#ifdef Q_OS_WIN
    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(cipherText.constData()));
    input.cbData = static_cast<DWORD>(cipherText.size());
    const QByteArray entropyBytes("IrAutoX.Launcher.Credentials.v2");
    DATA_BLOB entropy{};
    entropy.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(entropyBytes.constData()));
    entropy.cbData = static_cast<DWORD>(entropyBytes.size());
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return {};
    }
    const QByteArray plainText(reinterpret_cast<const char *>(output.pbData),
                               static_cast<qsizetype>(output.cbData));
    LocalFree(output.pbData);
    return plainText;
#else
    return QByteArray::fromBase64(cipherText);
#endif
}

bool SecureStore::saveCredentials(const QString &username, const QString &password)
{
    const QByteArray encrypted = protect(password.toUtf8());
    if (!password.isEmpty() && encrypted.isEmpty())
        return false;

    auto store = settings();
    store.setValue(QStringLiteral("auth/username"), username);
    store.setValue(QStringLiteral("auth/password"), encrypted.toBase64());
    store.sync();
    return store.status() == QSettings::NoError;
}

std::pair<QString, QString> SecureStore::loadCredentials()
{
    auto store = settings();
    const QString username = store.value(QStringLiteral("auth/username")).toString();
    const QByteArray encoded = store.value(QStringLiteral("auth/password")).toByteArray();
    if (username.isEmpty() || encoded.isEmpty())
        return {};
    const QByteArray password = unprotect(QByteArray::fromBase64(encoded));
    return {username, QString::fromUtf8(password)};
}

void SecureStore::clearCredentials()
{
    auto store = settings();
    store.remove(QStringLiteral("auth"));
    store.sync();
}

} // namespace irautox

