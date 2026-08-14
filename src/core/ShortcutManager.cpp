#include "core/ShortcutManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <shlobj.h>
#  include <shobjidl.h>
#endif

namespace irautox {
namespace {
QString safeFileName(QString name)
{
    name = name.trimmed();
    name.replace(QRegularExpression(QStringLiteral(R"([<>:\"/\\|?*]+)")), QStringLiteral("-"));
    name.replace(QRegularExpression(QStringLiteral("[. ]+$")), QString());
    return name.isEmpty() ? QStringLiteral("IrAutoX Game") : name.left(96);
}
}

QString ShortcutManager::shortcutPath(qint64 gameId, const QString &gameName)
{
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    return QDir(desktop).filePath(QStringLiteral("%1 - IrAutoX [%2].lnk").arg(safeFileName(gameName)).arg(gameId));
}

bool ShortcutManager::createGameShortcut(qint64 gameId, const QString &gameName, const QString &iconPath, QString *error)
{
#ifdef Q_OS_WIN
    const QString launcher = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString linkPath = QDir::toNativeSeparators(shortcutPath(gameId, gameName));
    const QString args = QStringLiteral("--launch-game %1").arg(gameId);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninit = SUCCEEDED(hr);
    if (hr == RPC_E_CHANGED_MODE)
        hr = S_OK;
    if (FAILED(hr)) {
        if (error) *error = QStringLiteral("COM initialization failed: 0x%1").arg(static_cast<qulonglong>(hr), 0, 16);
        return false;
    }

    IShellLinkW *shellLink = nullptr;
    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                          reinterpret_cast<void **>(&shellLink));
    if (SUCCEEDED(hr)) {
        shellLink->SetPath(reinterpret_cast<LPCWSTR>(launcher.utf16()));
        shellLink->SetArguments(reinterpret_cast<LPCWSTR>(args.utf16()));
        shellLink->SetWorkingDirectory(reinterpret_cast<LPCWSTR>(QCoreApplication::applicationDirPath().utf16()));
        shellLink->SetDescription(reinterpret_cast<LPCWSTR>(gameName.utf16()));
        if (!iconPath.isEmpty() && QFileInfo::exists(iconPath))
            shellLink->SetIconLocation(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(iconPath).utf16()), 0);
        else
            shellLink->SetIconLocation(reinterpret_cast<LPCWSTR>(launcher.utf16()), 0);

        IPersistFile *persist = nullptr;
        hr = shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&persist));
        if (SUCCEEDED(hr)) {
            hr = persist->Save(reinterpret_cast<LPCWSTR>(linkPath.utf16()), TRUE);
            persist->Release();
        }
        shellLink->Release();
    }

    if (uninit)
        CoUninitialize();
    if (FAILED(hr) && error)
        *error = QStringLiteral("Shortcut creation failed: 0x%1").arg(static_cast<qulonglong>(hr), 0, 16);
    return SUCCEEDED(hr);
#else
    Q_UNUSED(gameId)
    Q_UNUSED(gameName)
    Q_UNUSED(iconPath)
    if (error) *error = QStringLiteral("Desktop shortcuts are implemented for Windows builds.");
    return false;
#endif
}

} // namespace irautox
