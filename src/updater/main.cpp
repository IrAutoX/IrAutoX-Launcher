#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>

namespace {

constexpr auto kLatestReleaseApi = "https://api.github.com/repos/IrAutoX/IrAutoX-Launcher/releases/latest";

QString currentVersion()
{
    return QString::fromLatin1(IRAUTOX_VERSION);
}

QString cleanVersion(QString value)
{
    value = value.trimmed();
    if (value.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        value.remove(0, 1);
    return value;
}

bool isNewerVersion(const QString &candidate)
{
    const QVersionNumber current = QVersionNumber::fromString(cleanVersion(currentVersion()));
    const QVersionNumber latest = QVersionNumber::fromString(cleanVersion(candidate));
    return QVersionNumber::compare(latest, current) > 0;
}

QString setupMarkerPath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("setup.install"));
}

bool isSetupInstall()
{
    return QFileInfo::exists(setupMarkerPath());
}

QString safeAssetName(const QUrl &url)
{
    QString name = QFileInfo(url.path()).fileName();
    if (name.isEmpty())
        name = isSetupInstall() ? QStringLiteral("IrAutoX-Update-Setup.exe")
                                : QStringLiteral("IrAutoX-Update-Portable.zip");
    return name;
}

void ensureAutoStart()
{
#ifdef Q_OS_WIN
    QSettings startup(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                      QSettings::NativeFormat);
    const QString command = QStringLiteral("\"")
        + QDir::toNativeSeparators(QCoreApplication::applicationFilePath())
        + QStringLiteral("\" --background");
    startup.setValue(QStringLiteral("IrAutoXUpdater"), command);
#endif
}

class Updater final : public QObject {
public:
    explicit Updater(QObject *parent = nullptr)
        : QObject(parent)
    {
        m_tray.setIcon(QIcon(QStringLiteral(":/logo.svg")));
        m_tray.setToolTip(QStringLiteral("IrAutoX Updater"));
        m_tray.show();

        m_timer.setInterval(15 * 60 * 1000);
        connect(&m_timer, &QTimer::timeout, this, [this] { checkNow(); });
        m_timer.start();
        QTimer::singleShot(1200, this, [this] { checkNow(); });
    }

private:
    void checkNow()
    {
        if (m_busy)
            return;
        m_busy = true;

        QNetworkRequest request(QUrl(QString::fromLatin1(kLatestReleaseApi)));
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("IrAutoX-Updater/%1").arg(currentVersion()));
        request.setRawHeader("Accept", "application/vnd.github+json");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply *reply = m_network.get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            const QByteArray payload = reply->readAll();
            const auto error = reply->error();
            const QString errorText = reply->errorString();
            reply->deleteLater();
            m_busy = false;

            if (error != QNetworkReply::NoError) {
                m_tray.setToolTip(QStringLiteral("IrAutoX Updater - %1").arg(errorText));
                return;
            }

            const QJsonDocument doc = QJsonDocument::fromJson(payload);
            if (!doc.isObject())
                return;
            const QJsonObject release = doc.object();
            const QString tag = release.value(QStringLiteral("tag_name")).toString();
            if (tag.isEmpty() || !isNewerVersion(tag) || m_promptedVersion == tag)
                return;

            const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
            const QString wanted = isSetupInstall() ? QStringLiteral("-Setup.exe") : QStringLiteral("-Portable.zip");
            QUrl assetUrl;
            for (const QJsonValue &value : assets) {
                const QJsonObject asset = value.toObject();
                const QString name = asset.value(QStringLiteral("name")).toString();
                if (name.endsWith(wanted, Qt::CaseInsensitive)) {
                    assetUrl = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
                    break;
                }
            }
            if (!assetUrl.isValid())
                return;

            m_promptedVersion = tag;
            m_tray.showMessage(tr("بروزرسانی IrAutoX"),
                               tr("نسخه %1 آماده است.").arg(cleanVersion(tag)),
                               QSystemTrayIcon::Information, 5000);

            const auto answer = QMessageBox::question(nullptr, tr("بروزرسانی IrAutoX"),
                tr("نسخه جدید %1 آماده است.\nنسخه فعلی: %2\n\nبروزرسانی دانلود و نصب شود؟")
                    .arg(cleanVersion(tag), currentVersion()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (answer == QMessageBox::Yes)
                downloadAsset(assetUrl, cleanVersion(tag));
        });
    }

    void downloadAsset(const QUrl &url, const QString &version)
    {
        if (m_downloadReply)
            return;

        const QString tempDir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("IrAutoXUpdater"));
        QDir().mkpath(tempDir);
        m_downloadPath = QDir(tempDir).filePath(safeAssetName(url));
        m_downloadFile.setFileName(m_downloadPath);
        if (!m_downloadFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(nullptr, tr("بروزرسانی"), tr("ساخت فایل موقت بروزرسانی ناموفق بود."));
            return;
        }

        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("IrAutoX-Updater/%1").arg(currentVersion()));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        m_downloadReply = m_network.get(request);

        connect(m_downloadReply, &QNetworkReply::readyRead, this, [this] {
            if (m_downloadReply)
                m_downloadFile.write(m_downloadReply->readAll());
        });
        connect(m_downloadReply, &QNetworkReply::downloadProgress, this,
                [this, version](qint64 received, qint64 total) {
            if (total <= 0)
                return;
            const int percent = static_cast<int>((received * 100) / total);
            m_tray.setToolTip(tr("IrAutoX %1 - دانلود %2%").arg(version).arg(percent));
        });
        connect(m_downloadReply, &QNetworkReply::finished, this, [this, version] {
            QNetworkReply *reply = m_downloadReply;
            m_downloadReply = nullptr;
            m_downloadFile.write(reply->readAll());
            m_downloadFile.close();
            const auto error = reply->error();
            const QString errorText = reply->errorString();
            reply->deleteLater();

            if (error != QNetworkReply::NoError) {
                QFile::remove(m_downloadPath);
                QMessageBox::warning(nullptr, tr("بروزرسانی"), tr("دانلود بروزرسانی ناموفق بود:\n%1").arg(errorText));
                return;
            }

            m_tray.showMessage(tr("بروزرسانی آماده است"),
                               tr("نسخه %1 دانلود شد و اکنون اعمال می‌شود.").arg(version),
                               QSystemTrayIcon::Information, 3500);
            if (isSetupInstall())
                applySetupUpdate();
            else
                applyPortableUpdate();
        });
    }

    void applySetupUpdate()
    {
        const QStringList args{
            QStringLiteral("/VERYSILENT"),
            QStringLiteral("/SUPPRESSMSGBOXES"),
            QStringLiteral("/CLOSEAPPLICATIONS"),
            QStringLiteral("/RESTARTAPPLICATIONS"),
            QStringLiteral("/NORESTART")
        };
        if (!QProcess::startDetached(m_downloadPath, args)) {
            QMessageBox::warning(nullptr, tr("بروزرسانی"), tr("اجرای نصب‌کننده جدید ناموفق بود."));
            return;
        }
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
    }

    void applyPortableUpdate()
    {
#ifdef Q_OS_WIN
        const QString tempDir = QFileInfo(m_downloadPath).absolutePath();
        const QString scriptPath = QDir(tempDir).filePath(QStringLiteral("apply-irautox-update.ps1"));
        QSaveFile script(scriptPath);
        if (!script.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(nullptr, tr("بروزرسانی"), tr("ساخت اسکریپت اعمال بروزرسانی ناموفق بود."));
            return;
        }
        static const char scriptText[] = R"PS(param(
    [Parameter(Mandatory=$true)][string]$Zip,
    [Parameter(Mandatory=$true)][string]$InstallDir,
    [Parameter(Mandatory=$true)][string]$Launcher,
    [Parameter(Mandatory=$true)][int]$UpdaterPid
)
$ErrorActionPreference = 'Stop'
Get-Process IrAutoXLauncher -ErrorAction SilentlyContinue | ForEach-Object { $_.CloseMainWindow() | Out-Null }
Start-Sleep -Seconds 2
Get-Process IrAutoXLauncher -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Wait-Process -Id $UpdaterPid -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 700
$stage = Join-Path $env:TEMP ('IrAutoX-stage-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Expand-Archive -LiteralPath $Zip -DestinationPath $stage -Force
Copy-Item -Path (Join-Path $stage '*') -Destination $InstallDir -Recurse -Force
Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $Zip -Force -ErrorAction SilentlyContinue
Start-Process -FilePath $Launcher
)PS";
        script.write(scriptText);
        if (!script.commit()) {
            QMessageBox::warning(nullptr, tr("بروزرسانی"), tr("ذخیره اسکریپت بروزرسانی ناموفق بود."));
            return;
        }

        const QString launcher = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("IrAutoXLauncher.exe"));
        const QStringList args{
            QStringLiteral("-NoProfile"),
            QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
            QStringLiteral("-File"), scriptPath,
            QStringLiteral("-Zip"), QDir::toNativeSeparators(m_downloadPath),
            QStringLiteral("-InstallDir"), QDir::toNativeSeparators(QCoreApplication::applicationDirPath()),
            QStringLiteral("-Launcher"), QDir::toNativeSeparators(launcher),
            QStringLiteral("-UpdaterPid"), QString::number(QCoreApplication::applicationPid())
        };
        if (!QProcess::startDetached(QStringLiteral("powershell.exe"), args)) {
            QMessageBox::warning(nullptr, tr("بروزرسانی"), tr("اجرای مرحله اعمال بروزرسانی ناموفق بود."));
            return;
        }
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
#else
        QMessageBox::information(nullptr, tr("بروزرسانی"), tr("بروزرسانی Portable خودکار در این سیستم‌عامل فعال نیست."));
#endif
    }

    QNetworkAccessManager m_network;
    QTimer m_timer;
    QSystemTrayIcon m_tray;
    QFile m_downloadFile;
    QNetworkReply *m_downloadReply = nullptr;
    QString m_downloadPath;
    QString m_promptedVersion;
    bool m_busy = false;
};

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("IrAutoX"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("irautox.ir"));
    QCoreApplication::setApplicationName(QStringLiteral("Updater"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(IRAUTOX_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/logo.svg")));

    const QString lockPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QStringLiteral("IrAutoXUpdater.lock"));
    QLockFile lock(lockPath);
    lock.setStaleLockTime(0);
    if (!lock.tryLock(100))
        return 0;

    ensureAutoStart();
    Updater updater;
    return app.exec();
}
