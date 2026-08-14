#include "core/ShortcutSync.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <objbase.h>
#  include <shobjidl.h>
#endif

namespace irautox {
namespace {

QString libraryPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("library.json"));
}

QString statePath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("desktop-shortcuts.json"));
}

QString safeShortcutName(QString name)
{
    name = name.trimmed();
    name.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*]+)")), QStringLiteral("-"));
    name.replace(QRegularExpression(QStringLiteral("[. ]+$")), QString());
    if (name.isEmpty())
        name = QStringLiteral("Game");
    return name.left(80);
}

QJsonObject loadObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() ? doc.object() : QJsonObject{};
}

void saveState(const QJsonObject &state)
{
    QSaveFile file(statePath());
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(state).toJson(QJsonDocument::Indented));
    file.commit();
}

#ifdef Q_OS_WIN
bool createWindowsShortcut(const QString &shortcutPath,
                           const QString &target,
                           const QString &arguments,
                           const QString &workingDirectory,
                           const QString &iconPath,
                           const QString &description)
{
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = SUCCEEDED(init);

    IShellLinkW *link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&link));
    if (FAILED(hr) || !link) {
        if (uninitialize)
            CoUninitialize();
        return false;
    }

    const std::wstring targetW = QDir::toNativeSeparators(target).toStdWString();
    const std::wstring argsW = arguments.toStdWString();
    const std::wstring workW = QDir::toNativeSeparators(workingDirectory).toStdWString();
    const std::wstring iconW = QDir::toNativeSeparators(iconPath).toStdWString();
    const std::wstring descW = description.toStdWString();

    link->SetPath(targetW.c_str());
    link->SetArguments(argsW.c_str());
    link->SetWorkingDirectory(workW.c_str());
    link->SetDescription(descW.c_str());
    link->SetIconLocation(iconW.c_str(), 0);

    IPersistFile *persist = nullptr;
    hr = link->QueryInterface(IID_PPV_ARGS(&persist));
    if (SUCCEEDED(hr) && persist) {
        const std::wstring shortcutW = QDir::toNativeSeparators(shortcutPath).toStdWString();
        hr = persist->Save(shortcutW.c_str(), TRUE);
        persist->Release();
    }

    link->Release();
    if (uninitialize)
        CoUninitialize();
    return SUCCEEDED(hr);
}
#endif

} // namespace

ShortcutSync::ShortcutSync(QObject *parent)
    : QObject(parent)
{
    auto *timer = new QTimer(this);
    timer->setInterval(4000);
    connect(timer, &QTimer::timeout, this, [this] { sync(); });
    timer->start();
    QTimer::singleShot(800, this, [this] { sync(); });
}

void ShortcutSync::sync()
{
#ifdef Q_OS_WIN
    const QJsonObject root = loadObject(libraryPath());
    const QJsonObject games = root.value(QStringLiteral("games")).toObject();
    QJsonObject oldState = loadObject(statePath());
    QJsonObject newState;

    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (desktop.isEmpty())
        return;

    const QString launcher = QCoreApplication::applicationFilePath();
    const QString launcherDir = QCoreApplication::applicationDirPath();

    for (auto it = games.constBegin(); it != games.constEnd(); ++it) {
        const QJsonObject game = it.value().toObject();
        const qint64 id = game.value(QStringLiteral("id")).toInteger(it.key().toLongLong());
        const QString name = game.value(QStringLiteral("name")).toString(QStringLiteral("Game"));
        const QString rootPath = game.value(QStringLiteral("rootPath")).toString();
        const QString executable = game.value(QStringLiteral("executable")).toString();
        if (id <= 0 || rootPath.isEmpty() || executable.isEmpty())
            continue;

        const QString exePath = QDir(rootPath).absoluteFilePath(executable);
        if (!QFileInfo::exists(exePath))
            continue;

        QString shortcut = oldState.value(QString::number(id)).toString();
        if (shortcut.isEmpty()) {
            shortcut = QDir(desktop).filePath(
                safeShortcutName(name) + QStringLiteral(" - IrAutoX.lnk"));
        }

        const QString args = QStringLiteral("--launch-game %1").arg(id);
        const QString description = QStringLiteral("Launch %1 with IrAutoX").arg(name);
        if (createWindowsShortcut(shortcut, launcher, args, launcherDir, exePath, description))
            newState.insert(QString::number(id), shortcut);
    }

    for (auto it = oldState.constBegin(); it != oldState.constEnd(); ++it) {
        if (!newState.contains(it.key())) {
            const QString path = it.value().toString();
            if (!path.isEmpty())
                QFile::remove(path);
        }
    }
    saveState(newState);
#else
    // Desktop shortcut synchronization is intentionally Windows-only.
#endif
}

} // namespace irautox
