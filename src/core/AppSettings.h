#pragma once

#include <QSettings>
#include <QString>

namespace irautox {

class AppSettings final {
public:
    AppSettings();

    QString serverHost() const;
    quint16 serverPort() const;
    QString downloadRoot() const;
    int bandwidthLimitKbps() const;
    bool minimizeToTray() const;
    bool closeToTray() const;
    bool launchOnStartup() const;

    void setServer(QString host, quint16 port);
    void setDownloadRoot(const QString &path);
    void setBandwidthLimitKbps(int value);
    void setMinimizeToTray(bool enabled);
    void setCloseToTray(bool enabled);
    void setLaunchOnStartup(bool enabled);

private:
    mutable QSettings m_settings;
};

} // namespace irautox

