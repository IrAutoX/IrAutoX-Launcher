#include "core/AppSettings.h"

#include <QDir>
#include <QStandardPaths>

namespace irautox {

AppSettings::AppSettings()
    : m_settings(QStringLiteral("IrAutoX"), QStringLiteral("Launcher"))
{
}

QString AppSettings::serverHost() const
{
    return m_settings.value(QStringLiteral("network/host"), QStringLiteral("irautox.ir")).toString();
}

quint16 AppSettings::serverPort() const
{
    return static_cast<quint16>(m_settings.value(QStringLiteral("network/port"), 6768).toUInt());
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
    host = host.trimmed();
    if (!host.isEmpty())
        m_settings.setValue(QStringLiteral("network/host"), host);
    m_settings.setValue(QStringLiteral("network/port"), port);
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
