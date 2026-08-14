#include "core/AppSettings.h"

#include <QDir>
#include <QStandardPaths>

namespace irautox {
namespace {
// Production endpoint is intentionally not exposed in the settings UI. Obfuscation is not treated as a security boundary.
QString productionHost()
{
    static const ushort chars[] = {0x0069,0x0072,0x0061,0x0075,0x0074,0x006f,0x0078,0x002e,0x0069,0x0072};
    return QString::fromUtf16(chars, static_cast<qsizetype>(std::size(chars)));
}
constexpr quint16 kProductionPort = 6768;
}

AppSettings::AppSettings()
    : m_settings(QStringLiteral("IrAutoX"), QStringLiteral("Launcher"))
{
}

QString AppSettings::serverHost() const
{
    return productionHost();
}

quint16 AppSettings::serverPort() const
{
    return kProductionPort;
}

QString AppSettings::downloadRoot() const
{
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString fallback = QDir(documents.isEmpty() ? QDir::homePath() : documents)
                                 .filePath(QStringLiteral("IrAutoX Games"));
    return m_settings.value(QStringLiteral("downloads/root"), fallback).toString();
}

int AppSettings::bandwidthLimitKbps() const
{
    return m_settings.value(QStringLiteral("downloads/limitKbps"), 0).toInt();
}

bool AppSettings::minimizeToTray() const
{
    return m_settings.value(QStringLiteral("window/minimizeToTray"), true).toBool();
}

bool AppSettings::closeToTray() const
{
    return m_settings.value(QStringLiteral("window/closeToTray"), false).toBool();
}

bool AppSettings::launchOnStartup() const
{
    return m_settings.value(QStringLiteral("window/launchOnStartup"), false).toBool();
}

void AppSettings::setServer(QString host, quint16 port)
{
    Q_UNUSED(host)
    Q_UNUSED(port)
    m_settings.remove(QStringLiteral("network/host"));
    m_settings.remove(QStringLiteral("network/port"));
}

void AppSettings::setDownloadRoot(const QString &path)
{
    m_settings.setValue(QStringLiteral("downloads/root"), QDir::cleanPath(path));
}

void AppSettings::setBandwidthLimitKbps(int value)
{
    m_settings.setValue(QStringLiteral("downloads/limitKbps"), qMax(0, value));
}

void AppSettings::setMinimizeToTray(bool enabled)
{
    m_settings.setValue(QStringLiteral("window/minimizeToTray"), enabled);
}

void AppSettings::setCloseToTray(bool enabled)
{
    m_settings.setValue(QStringLiteral("window/closeToTray"), enabled);
}

void AppSettings::setLaunchOnStartup(bool enabled)
{
    m_settings.setValue(QStringLiteral("window/launchOnStartup"), enabled);
}

} // namespace irautox
