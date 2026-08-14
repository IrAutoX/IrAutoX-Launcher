#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocalSocket>
#include <QLockFile>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <wincrypt.h>
#endif

namespace {
constexpr int kBackgroundCheckIntervalMs = 10 * 60 * 1000;
constexpr auto kLauncherInstance = "IrAutoXLauncher.SingleInstance.v2";

QString manifestPrimary()
{
    static const ushort s[] = {104,116,116,112,115,58,47,47,105,114,97,117,116,111,120,46,105,114,47,118,101,114,115,105,111,110,47,118,101,114,115,105,111,110,46,106,115,111,110};
    return QString::fromUtf16(s, 38);
}
QString manifestHttpFallback()
{
    static const ushort s[] = {104,116,116,112,58,47,47,105,114,97,117,116,111,120,46,105,114,47,118,101,114,115,105,111,110,47,118,101,114,115,105,111,110,46,106,115,111,110};
    return QString::fromUtf16(s, 37);
}
QString manifestIpFallback()
{
    static const ushort s[] = {104,116,116,112,58,47,47,53,46,53,55,46,51,55,46,49,50,50,47,118,101,114,115,105,111,110,47,118,101,114,115,105,111,110,46,106,115,111,110};
    return QString::fromUtf16(s, 39);
}

QString updaterRoot()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString path = QDir(base).filePath(QStringLiteral("updater"));
    QDir().mkpath(path);
    return path;
}

QByteArray protectForCurrentUser(const QByteArray &plain)
{
#ifdef Q_OS_WIN
    if (plain.isEmpty()) return {};
    DATA_BLOB input{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"IrAutoX update manifest", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output))
        return {};
    QByteArray encrypted(reinterpret_cast<const char *>(output.pbData), static_cast<int>(output.cbData));
    LocalFree(output.pbData);
    return encrypted;
#else
    return plain;
#endif
}

void saveEncryptedManifest(const QByteArray &body)
{
    const QByteArray encrypted = protectForCurrentUser(body);
    if (encrypted.isEmpty()) return;
    QSaveFile file(QDir(updaterRoot()).filePath(QStringLiteral("version.cache")));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(encrypted);
        file.commit();
    }
}

bool validSha256(const QString &sha)
{
    if (sha.size() != 64) return false;
    for (const QChar c : sha) {
        if (!c.isDigit() && (c.toLower() < QLatin1Char('a') || c.toLower() > QLatin1Char('f')))
            return false;
    }
    return true;
}

QString changelogText(const QJsonObject &root)
{
    const QJsonValue value = root.value(QStringLiteral("changelog"));
    if (value.isString()) return value.toString();
    QStringList lines;
    for (const QJsonValue &entry : value.toArray()) {
        const QString text = entry.toString().trimmed();
        if (!text.isEmpty()) lines << QStringLiteral("• %1").arg(text);
    }
    return lines.join(QLatin1Char('\n'));
}

void requestLauncherQuit()
{
    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(kLauncherInstance), QIODevice::WriteOnly);
    if (socket.waitForConnected(200)) {
        socket.write("quit-update");
        socket.flush();
        socket.waitForBytesWritten(200);
    }
}

class Updater final : public QObject {
    Q_OBJECT
public:
    explicit Updater(bool background, QObject *parent = nullptr)
        : QObject(parent), m_background(background)
    {
        m_pollTimer.setInterval(kBackgroundCheckIntervalMs);
        connect(&m_pollTimer, &QTimer::timeout, this, &Updater::check);
        if (background) m_pollTimer.start();
        QTimer::singleShot(0, this, &Updater::check);
    }

private slots:
    void check()
    {
        if (m_checkInFlight || QDateTime::currentDateTimeUtc() < m_snoozeUntil)
            return;
        m_checkInFlight = true;
        m_endpointIndex = 0;
        requestManifest();
    }

private:
    void requestManifest()
    {
        const QStringList endpoints{manifestPrimary(), manifestHttpFallback(), manifestIpFallback()};
        if (m_endpointIndex >= endpoints.size()) {
            m_checkInFlight = false;
            if (!m_background) {
                QMessageBox::warning(nullptr, QStringLiteral("IrAutoX Updater"),
                                     QStringLiteral("ارتباط با سرور بروزرسانی برقرار نشد."));
                QCoreApplication::quit();
            }
            return;
        }

        QNetworkRequest req(QUrl(endpoints.at(m_endpointIndex++)));
        req.setTransferTimeout(3500);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("IrAutoX-Updater/%1").arg(QString::fromLatin1(IRAUTOX_VERSION)));
        QNetworkReply *reply = m_network.get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            const QByteArray body = reply->readAll();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
            reply->deleteLater();
            if (!ok) {
                requestManifest();
                return;
            }
            processManifest(body);
        });
    }

    void processManifest(const QByteArray &body)
    {
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            requestManifest();
            return;
        }

        const QJsonObject root = doc.object();
        const QString version = root.value(QStringLiteral("version")).toString().trimmed();
        QJsonObject installer = root.value(QStringLiteral("installer")).toObject();
        if (installer.isEmpty()) {
            installer.insert(QStringLiteral("url"), root.value(QStringLiteral("installer_url")));
            installer.insert(QStringLiteral("sha256"), root.value(QStringLiteral("installer_sha256")));
        }
        const QString installerUrl = installer.value(QStringLiteral("url")).toString().trimmed();
        const QString installerSha = installer.value(QStringLiteral("sha256")).toString().trimmed().toLower();

        if (version.isEmpty() || !QUrl(installerUrl).isValid() || !validSha256(installerSha)) {
            requestManifest();
            return;
        }

        saveEncryptedManifest(body);
        m_checkInFlight = false;

        const QString current = QString::fromLatin1(IRAUTOX_VERSION);
        // Server policy requested by IrAutoX: any semantic version mismatch is treated as an update.
        if (version == current) {
            if (!m_background) {
                QMessageBox::information(nullptr, QStringLiteral("IrAutoX Updater"),
                                         QStringLiteral("نسخه %1 نصب است و بروزرسانی جدیدی وجود ندارد.").arg(current));
                QCoreApplication::quit();
            }
            return;
        }

        showUpdatePrompt(root, version, QUrl(installerUrl), installerSha);
    }

    void showUpdatePrompt(const QJsonObject &root, const QString &version, const QUrl &url, const QString &sha)
    {
        QDialog dialog;
        dialog.setWindowTitle(QStringLiteral("IrAutoX Update"));
        dialog.setMinimumWidth(520);
        dialog.setStyleSheet(QStringLiteral(
            "QDialog{background:#171a21;color:#e7e9ec;} QLabel{color:#e7e9ec;} "
            "QPlainTextEdit{background:#11151b;color:#cfd5dc;border:1px solid #303844;border-radius:8px;padding:10px;} "
            "QPushButton{min-height:34px;padding:0 18px;border-radius:6px;background:#2a313b;color:white;border:1px solid #3d4754;} "
            "QPushButton#install{background:#5c9f20;border-color:#74b82e;font-weight:700;}"));

        auto *layout = new QVBoxLayout(&dialog);
        auto *title = new QLabel(root.value(QStringLiteral("title")).toString(QStringLiteral("بروزرسانی جدید IrAutoX آماده است")), &dialog);
        QFont titleFont = title->font(); titleFont.setPointSize(15); titleFont.setBold(true); title->setFont(titleFont);
        auto *meta = new QLabel(QStringLiteral("نسخه فعلی: %1    نسخه جدید: %2").arg(QString::fromLatin1(IRAUTOX_VERSION), version), &dialog);
        auto *notes = new QPlainTextEdit(&dialog);
        notes->setReadOnly(true);
        notes->setPlainText(changelogText(root).isEmpty() ? QStringLiteral("بهبودهای عملکرد، امنیت و پایداری.") : changelogText(root));
        notes->setMinimumHeight(160);
        auto *buttons = new QHBoxLayout;
        auto *later = new QPushButton(QStringLiteral("بعداً"), &dialog);
        auto *install = new QPushButton(QStringLiteral("نصب بروزرسانی"), &dialog);
        install->setObjectName(QStringLiteral("install"));
        buttons->addStretch(); buttons->addWidget(later); buttons->addWidget(install);
        layout->addWidget(title); layout->addWidget(meta); layout->addWidget(notes); layout->addLayout(buttons);

        connect(later, &QPushButton::clicked, &dialog, &QDialog::reject);
        connect(install, &QPushButton::clicked, &dialog, &QDialog::accept);
        if (dialog.exec() == QDialog::Accepted) {
            downloadInstaller(version, url, sha);
        } else {
            m_snoozeUntil = QDateTime::currentDateTimeUtc().addSecs(60 * 60);
            if (!m_background) QCoreApplication::quit();
        }
    }

    void downloadInstaller(const QString &version, const QUrl &url, const QString &expectedSha)
    {
        const QString target = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                   .filePath(QStringLiteral("IrAutoX-Launcher-v%1-Setup.exe").arg(version));
        m_output.setFileName(target);
        if (!m_output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(nullptr, QStringLiteral("IrAutoX Updater"), QStringLiteral("ساخت فایل نصب موقت ممکن نبود."));
            return;
        }

        auto *progress = new QDialog;
        progress->setWindowTitle(QStringLiteral("IrAutoX Updater"));
        progress->setMinimumWidth(480);
        auto *layout = new QVBoxLayout(progress);
        auto *label = new QLabel(QStringLiteral("در حال دریافت نسخه %1…").arg(version), progress);
        auto *bar = new QProgressBar(progress); bar->setRange(0, 100);
        auto *detail = new QLabel(QStringLiteral("در حال اتصال به سرور…"), progress);
        layout->addWidget(label); layout->addWidget(bar); layout->addWidget(detail);
        progress->show();

        QNetworkRequest req(url);
        req.setTransferTimeout(30000);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply *reply = m_network.get(req);
        connect(reply, &QNetworkReply::readyRead, this, [this, reply] { m_output.write(reply->readAll()); });
        connect(reply, &QNetworkReply::downloadProgress, progress, [bar, detail](qint64 got, qint64 total) {
            if (total > 0) bar->setValue(static_cast<int>((got * 100) / total));
            detail->setText(QStringLiteral("%1 / %2 MB").arg(got / 1048576.0, 0, 'f', 1).arg(total / 1048576.0, 0, 'f', 1));
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, progress, target, expectedSha, version] {
            m_output.write(reply->readAll());
            m_output.close();
            const bool networkOk = reply->error() == QNetworkReply::NoError;
            reply->deleteLater();
            progress->close();
            progress->deleteLater();
            if (!networkOk) {
                QFile::remove(target);
                QMessageBox::warning(nullptr, QStringLiteral("IrAutoX Updater"), QStringLiteral("دانلود بروزرسانی ناموفق بود."));
                return;
            }

            QFile file(target);
            if (!file.open(QIODevice::ReadOnly)) {
                QFile::remove(target);
                return;
            }
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (!hash.addData(&file)) {
                file.close(); QFile::remove(target); return;
            }
            const QString actual = QString::fromLatin1(hash.result().toHex()).toLower();
            file.close();
            if (actual != expectedSha) {
                QFile::remove(target);
                QMessageBox::critical(nullptr, QStringLiteral("IrAutoX Security"),
                                      QStringLiteral("SHA-256 فایل نصب با version.json مطابقت ندارد. نصب متوقف شد."));
                return;
            }

            QSaveFile marker(QDir(updaterRoot()).filePath(QStringLiteral("update-pending.txt")));
            if (marker.open(QIODevice::WriteOnly | QIODevice::Text)) {
                marker.write(version.toUtf8());
                marker.commit();
            }
            requestLauncherQuit();
            QTimer::singleShot(350, this, [target] {
                QProcess::startDetached(target, {QStringLiteral("/CLOSEAPPLICATIONS")});
                QCoreApplication::quit();
            });
        });
    }

    bool m_background = false;
    bool m_checkInFlight = false;
    int m_endpointIndex = 0;
    QDateTime m_snoozeUntil;
    QNetworkAccessManager m_network;
    QFile m_output;
    QTimer m_pollTimer;
};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("IrAutoX"));
    QCoreApplication::setApplicationName(QStringLiteral("Updater"));

    QLockFile lock(QDir(updaterRoot()).filePath(QStringLiteral("updater.lock")));
    lock.setStaleLockTime(30000);
    if (!lock.tryLock(0))
        return 0;

    const bool background = app.arguments().contains(QStringLiteral("--background"));
    Updater updater(background);
    return app.exec();
}

#include "main.moc"
