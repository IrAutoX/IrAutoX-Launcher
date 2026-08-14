#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>

namespace {
constexpr auto kLatestReleaseApi = "https://api.github.com/repos/IrAutoX/IrAutoX-Launcher/releases/latest";

class Updater final : public QObject {
    Q_OBJECT
public:
    explicit Updater(bool background, QObject *parent = nullptr)
        : QObject(parent), m_background(background)
    {
        QTimer::singleShot(0, this, &Updater::check);
    }

private slots:
    void check()
    {
        QNetworkRequest request(QUrl(QString::fromLatin1(kLatestReleaseApi)));
        request.setRawHeader("Accept", "application/vnd.github+json");
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("IrAutoX-Updater/%1").arg(QString::fromLatin1(IRAUTOX_VERSION)));
        QNetworkReply *reply = m_network.get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            const QByteArray body = reply->readAll();
            const auto error = reply->error();
            reply->deleteLater();
            if (error != QNetworkReply::NoError) {
                finishSilentlyOrWarn(QStringLiteral("بررسی بروزرسانی لانچر ناموفق بود."));
                return;
            }
            const QJsonObject root = QJsonDocument::fromJson(body).object();
            QString tag = root.value(QStringLiteral("tag_name")).toString();
            if (tag.startsWith(QLatin1Char('v')))
                tag.remove(0, 1);
            if (QVersionNumber::compare(QVersionNumber::fromString(tag), QVersionNumber::fromString(QString::fromLatin1(IRAUTOX_VERSION))) <= 0) {
                if (!m_background)
                    QMessageBox::information(nullptr, QStringLiteral("IrAutoX Updater"), QStringLiteral("لانچر شما به‌روز است."));
                QCoreApplication::quit();
                return;
            }

            QString assetUrl;
            const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
            for (const QJsonValue &value : assets) {
                const QJsonObject asset = value.toObject();
                const QString name = asset.value(QStringLiteral("name")).toString();
                if (name.contains(QStringLiteral("Portable.zip"), Qt::CaseInsensitive)) {
                    assetUrl = asset.value(QStringLiteral("browser_download_url")).toString();
                    break;
                }
            }
            if (assetUrl.isEmpty()) {
                finishSilentlyOrWarn(QStringLiteral("فایل بروزرسانی Portable در Release پیدا نشد."));
                return;
            }
            if (QMessageBox::question(nullptr, QStringLiteral("بروزرسانی IrAutoX"),
                                      QStringLiteral("نسخه %1 آماده است. لانچر بسته و بروزرسانی شود؟").arg(tag)) != QMessageBox::Yes) {
                QCoreApplication::quit();
                return;
            }
            download(QUrl(assetUrl));
        });
    }

    void download(const QUrl &url)
    {
        const QString tempRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        m_zipPath = QDir(tempRoot).filePath(QStringLiteral("IrAutoX-Launcher-update.zip"));
        m_output.setFileName(m_zipPath);
        if (!m_output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            finishSilentlyOrWarn(QStringLiteral("ساخت فایل موقت بروزرسانی ممکن نبود."));
            return;
        }
        QNetworkReply *reply = m_network.get(QNetworkRequest(url));
        connect(reply, &QNetworkReply::readyRead, this, [this, reply] { m_output.write(reply->readAll()); });
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            m_output.write(reply->readAll());
            m_output.close();
            const auto error = reply->error();
            reply->deleteLater();
            if (error != QNetworkReply::NoError) {
                finishSilentlyOrWarn(QStringLiteral("دانلود بروزرسانی ناموفق بود."));
                return;
            }
            apply();
        });
    }

    void apply()
    {
#ifdef Q_OS_WIN
        const QString appDir = QCoreApplication::applicationDirPath();
        const QString launcher = QDir(appDir).filePath(QStringLiteral("IrAutoXLauncher.exe"));
        const QString script = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                   .filePath(QStringLiteral("irautox-update.ps1"));
        QSaveFile file(script);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            finishSilentlyOrWarn(QStringLiteral("ساخت اسکریپت بروزرسانی ممکن نبود."));
            return;
        }
        const QString ps = QStringLiteral(
            "$ErrorActionPreference='Stop'\n"
            "Start-Sleep -Milliseconds 700\n"
            "$dest='%1'\n"
            "$zip='%2'\n"
            "$tmp=Join-Path $env:TEMP 'IrAutoX-Launcher-unpack'\n"
            "Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue\n"
            "Expand-Archive -Path $zip -DestinationPath $tmp -Force\n"
            "Get-ChildItem $tmp -Force | ForEach-Object { Copy-Item $_.FullName -Destination $dest -Recurse -Force }\n"
            "Start-Process '%3'\n"
            "Remove-Item $zip -Force -ErrorAction SilentlyContinue\n"
            "Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue\n")
            .arg(appDir.replace("'", "''"), m_zipPath.replace("'", "''"), launcher.replace("'", "''"));
        file.write(ps.toUtf8());
        file.commit();
        QProcess::startDetached(QStringLiteral("powershell.exe"),
                                {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                                 QStringLiteral("-File"), script});
        QCoreApplication::quit();
#else
        finishSilentlyOrWarn(QStringLiteral("بروزرسانی خودکار در این نسخه فقط برای Windows فعال است."));
#endif
    }

    void finishSilentlyOrWarn(const QString &message)
    {
        if (!m_background)
            QMessageBox::warning(nullptr, QStringLiteral("IrAutoX Updater"), message);
        QCoreApplication::quit();
    }

    bool m_background = false;
    QNetworkAccessManager m_network;
    QFile m_output;
    QString m_zipPath;
};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("IrAutoX Updater"));
    const bool background = app.arguments().contains(QStringLiteral("--background"));
    Updater updater(background);
    return app.exec();
}

#include "main.moc"
