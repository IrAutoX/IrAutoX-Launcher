#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
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
#  include <shellapi.h>
#  include <tlhelp32.h>
#  include <wincrypt.h>
#endif

namespace {
constexpr int kBackgroundCheckIntervalMs = 10 * 60 * 1000;
constexpr auto kLauncherInstance = "IrAutoXLauncher.SingleInstance.v2";

QString manifestPrimary()
{
    return QStringLiteral("https://irautox.ir/version/version.json");
}

QString manifestHttpFallback()
{
    return QStringLiteral("http://irautox.ir/version/version.json");
}

QString manifestIpFallback()
{
    return QStringLiteral("http://5.57.37.122/version/version.json");
}

QString updaterRoot()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString path = QDir(base).filePath(QStringLiteral("updater"));
    QDir().mkpath(path);
    return path;
}

QString packageRoot()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString path = QDir(base).filePath(QStringLiteral("IrAutoX_Update/packages"));
    QDir().mkpath(path);
    return path;
}

QByteArray protectForCurrentUser(const QByteArray &plain)
{
#ifdef Q_OS_WIN
    if (plain.isEmpty())
        return {};
    DATA_BLOB input{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"IrAutoX update manifest", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
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
    if (encrypted.isEmpty())
        return;
    QSaveFile file(QDir(updaterRoot()).filePath(QStringLiteral("version.cache")));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(encrypted);
        file.commit();
    }
}

bool validSha256(const QString &sha)
{
    if (sha.size() != 64)
        return false;
    for (const QChar c : sha) {
        const QChar lower = c.toLower();
        if (!c.isDigit() && (lower < QLatin1Char('a') || lower > QLatin1Char('f')))
            return false;
    }
    return true;
}

QString changelogText(const QJsonObject &root)
{
    const QJsonValue value = root.value(QStringLiteral("changelog"));
    if (value.isString())
        return value.toString();
    QStringList lines;
    for (const QJsonValue &entry : value.toArray()) {
        const QString text = entry.toString().trimmed();
        if (!text.isEmpty())
            lines << QStringLiteral("• %1").arg(text);
    }
    return lines.join(QLatin1Char('\n'));
}

void requestLauncherQuit()
{
    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(kLauncherInstance), QIODevice::WriteOnly);
    if (!socket.waitForConnected(400))
        return;
    socket.write("quit-update");
    socket.flush();
    socket.waitForBytesWritten(400);
}

#ifdef Q_OS_WIN
bool processExists(const wchar_t *exeName)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, exeName) == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

void terminateProcesses(const wchar_t *exeName)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, exeName) != 0 || entry.th32ProcessID == GetCurrentProcessId())
                continue;
            HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID);
            if (!process)
                continue;
            TerminateProcess(process, 0);
            WaitForSingleObject(process, 1000);
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

bool waitUntilClosed(const wchar_t *exeName, DWORD timeoutMs)
{
    const ULONGLONG start = GetTickCount64();
    while (processExists(exeName)) {
        if (GetTickCount64() - start >= timeoutMs)
            return false;
        Sleep(100);
    }
    return true;
}

bool launchSetupElevated(const QString &setupPath, QString *error)
{
    if (!QFileInfo::exists(setupPath)) {
        if (error)
            *error = QStringLiteral("فایل Setup پیدا نشد: %1").arg(setupPath);
        return false;
    }
    const std::wstring file = QDir::toNativeSeparators(setupPath).toStdWString();
    const std::wstring params = L"/SILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS";
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = file.c_str();
    info.lpParameters = params.c_str();
    info.lpDirectory = nullptr;
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) {
        if (error)
            *error = QStringLiteral("اجرای Setup ناموفق بود. Windows Error: %1").arg(GetLastError());
        return false;
    }
    if (info.hProcess)
        CloseHandle(info.hProcess);
    return true;
}
#endif

class Updater final : public QObject {
    Q_OBJECT
public:
    explicit Updater(bool background, QObject *parent = nullptr)
        : QObject(parent), m_background(background)
    {
        m_pollTimer.setInterval(kBackgroundCheckIntervalMs);
        connect(&m_pollTimer, &QTimer::timeout, this, &Updater::check);
        if (m_background)
            m_pollTimer.start();
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
                QMessageBox::warning(nullptr, QStringLiteral("IrAutoX Updater"), QStringLiteral("ارتباط با سرور بروزرسانی برقرار نشد."));
                QCoreApplication::quit();
            }
            return;
        }

        QNetworkRequest request{QUrl(endpoints.at(m_endpointIndex++))};
        request.setTransferTimeout(3500);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("IrAutoX-Updater/%1").arg(QString::fromLatin1(IRAUTOX_VERSION)));
        QNetworkReply *reply = m_network.get(request);
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
        const QUrl url(installerUrl);
        if (version.isEmpty() || !url.isValid() || url.scheme().isEmpty() || !validSha256(installerSha)) {
            requestManifest();
            return;
        }

        saveEncryptedManifest(body);
        m_checkInFlight = false;
        const QString current = QString::fromLatin1(IRAUTOX_VERSION);
        if (version == current) {
            if (!m_background) {
                QMessageBox::information(nullptr, QStringLiteral("IrAutoX Updater"), QStringLiteral("نسخه %1 نصب است و بروزرسانی جدیدی وجود ندارد.").arg(current));
                QCoreApplication::quit();
            }
            return;
        }
        showUpdatePrompt(root, version, url, installerSha);
    }

    void showUpdatePrompt(const QJsonObject &root, const QString &version, const QUrl &url, const QString &sha)
    {
        QDialog dialog;
        dialog.setWindowTitle(QStringLiteral("IrAutoX Update"));
        dialog.setMinimumWidth(540);
        dialog.setStyleSheet(QStringLiteral("QDialog{background:#171a21;color:#e7e9ec;} QLabel{color:#e7e9ec;} QPlainTextEdit{background:#11151b;color:#cfd5dc;border:1px solid #303844;border-radius:8px;padding:10px;} QPushButton{min-height:36px;padding:0 18px;border-radius:6px;background:#2a313b;color:white;border:1px solid #3d4754;} QPushButton#install{background:#5c9f20;border-color:#74b82e;font-weight:700;}"));
        auto *layout = new QVBoxLayout(&dialog);
        auto *title = new QLabel(root.value(QStringLiteral("title")).toString(QStringLiteral("بروزرسانی جدید IrAutoX آماده است")), &dialog);
        QFont titleFont = title->font();
        titleFont.setPointSize(15);
        titleFont.setBold(true);
        title->setFont(titleFont);
        auto *meta = new QLabel(QStringLiteral("نسخه فعلی: %1    نسخه جدید: %2").arg(QString::fromLatin1(IRAUTOX_VERSION), version), &dialog);
        auto *notes = new QPlainTextEdit(&dialog);
        notes->setReadOnly(true);
        notes->setPlainText(changelogText(root).isEmpty() ? QStringLiteral("بهبودهای عملکرد، امنیت و پایداری.") : changelogText(root));
        notes->setMinimumHeight(170);
        auto *buttons = new QHBoxLayout;
        auto *later = new QPushButton(QStringLiteral("بعداً"), &dialog);
        auto *install = new QPushButton(QStringLiteral("نصب بروزرسانی"), &dialog);
        install->setObjectName(QStringLiteral("install"));
        buttons->addStretch();
        buttons->addWidget(later);
        buttons->addWidget(install);
        layout->addWidget(title);
        layout->addWidget(meta);
        layout->addWidget(notes);
        layout->addLayout(buttons);
        connect(later, &QPushButton::clicked, &dialog, &QDialog::reject);
        connect(install, &QPushButton::clicked, &dialog, &QDialog::accept);
        if (dialog.exec() == QDialog::Accepted)
            downloadInstaller(version, url, sha);
        else {
            m_snoozeUntil = QDateTime::currentDateTimeUtc().addSecs(60 * 60);
            if (!m_background)
                QCoreApplication::quit();
        }
    }

    void downloadInstaller(const QString &version, const QUrl &url, const QString &expectedSha)
    {
        const QString target = QDir(packageRoot()).filePath(QStringLiteral("IrAutoX-Launcher-v%1-Setup.exe").arg(version));
        QFile::remove(target);
        m_output.setFileName(target);
        if (!m_output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::critical(nullptr, QStringLiteral("IrAutoX Updater"), QStringLiteral("ساخت فایل Setup در Temp ممکن نبود.\n%1").arg(target));
            return;
        }

        auto *progress = new QDialog;
        progress->setWindowTitle(QStringLiteral("IrAutoX Updater"));
        progress->setMinimumWidth(500);
        progress->setModal(false);
        auto *layout = new QVBoxLayout(progress);
        auto *label = new QLabel(QStringLiteral("در حال دریافت نسخه %1…").arg(version), progress);
        auto *bar = new QProgressBar(progress);
        bar->setRange(0, 100);
        auto *detail = new QLabel(QStringLiteral("در حال اتصال به سرور…"), progress);
        layout->addWidget(label);
        layout->addWidget(bar);
        layout->addWidget(detail);
        progress->show();

        QNetworkRequest request(url);
        request.setTransferTimeout(60000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("IrAutoX-Updater/%1").arg(QString::fromLatin1(IRAUTOX_VERSION)));
        QNetworkReply *reply = m_network.get(request);
        connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
            m_output.write(reply->readAll());
        });
        connect(reply, &QNetworkReply::downloadProgress, progress, [bar, detail](qint64 got, qint64 total) {
            if (total > 0)
                bar->setValue(static_cast<int>((got * 100) / total));
            detail->setText(QStringLiteral("%1 / %2 MB").arg(got / 1048576.0, 0, 'f', 1).arg(total / 1048576.0, 0, 'f', 1));
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, progress, target, expectedSha, version] {
            m_output.write(reply->readAll());
            m_output.close();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QString networkError = reply->errorString();
            const bool networkOk = reply->error() == QNetworkReply::NoError && (status == 0 || (status >= 200 && status < 300));
            reply->deleteLater();
            progress->close();
            progress->deleteLater();
            if (!networkOk) {
                QFile::remove(target);
                QMessageBox::critical(nullptr, QStringLiteral("IrAutoX Updater"), QStringLiteral("دانلود بروزرسانی ناموفق بود.\nHTTP %1\n%2").arg(status).arg(networkError));
                return;
            }

            QFile file(target);
            if (!file.open(QIODevice::ReadOnly)) {
                QFile::remove(target);
                QMessageBox::critical(nullptr, QStringLiteral("IrAutoX Updater"), QStringLiteral("فایل دانلودشده قابل خواندن نیست."));
                return;
            }
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (!hash.addData(&file)) {
                file.close();
                QFile::remove(target);
                QMessageBox::critical(nullptr, QStringLiteral("IrAutoX Updater"), QStringLiteral("محاسبه SHA-256 ناموفق بود."));
                return;
            }
            const QString actual = QString::fromLatin1(hash.result().toHex()).toLower();
            file.close();
            if (actual != expectedSha) {
                QFile::remove(target);
                QMessageBox::critical(nullptr, QStringLiteral("IrAutoX Security"), QStringLiteral("SHA-256 فایل نصب با version.json مطابقت ندارد.\nExpected: %1\nActual: %2").arg(expectedSha, actual));
                return;
            }

            QSaveFile marker(QDir(updaterRoot()).filePath(QStringLiteral("update-pending.txt")));
            if (marker.open(QIODevice::WriteOnly | QIODevice::Text)) {
                marker.write(version.toUtf8());
                marker.commit();
            }

#ifdef Q_OS_WIN
            requestLauncherQuit();
            if (!waitUntilClosed(L"IrAutoXLauncher.exe", 2500)) {
                terminateProcesses(L"IrAutoXLauncher.exe");
                waitUntilClosed(L"IrAutoXLauncher.exe", 1500);
            }
            QString setupError;
            if (!launchSetupElevated(target, &setupError)) {
                QMessageBox::critical(nullptr, QStringLiteral("IrAutoX Updater"), setupError);
                return;
            }
            QCoreApplication::exit(0);
#else
            QMessageBox::critical(nullptr, QStringLiteral("IrAutoX Updater"), QStringLiteral("این Updater برای Windows ساخته شده است."));
#endif
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
    QCoreApplication::setOrganizationDomain(QStringLiteral("irautox.ir"));
    QCoreApplication::setApplicationName(QStringLiteral("Updater"));

    QLockFile lock(QDir(updaterRoot()).filePath(QStringLiteral("updater.lock")));
    lock.setStaleLockTime(30000);
    if (!lock.tryLock(0))
        return 0;

    const bool background = app.arguments().contains(QStringLiteral("--background"));
    Updater updater(background);
    return app.exec();
}

#include "main_v5.moc"
