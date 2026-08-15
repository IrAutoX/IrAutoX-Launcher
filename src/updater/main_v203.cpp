#include "ui/StyledMessageBox.h"
#include "ui/WindowChrome.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocalSocket>
#include <QLockFile>
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
constexpr int kParentPollIntervalMs = 800;
constexpr int kMaxDownloadRetries = 2;
constexpr auto kLauncherInstance = "IrAutoXLauncher.SingleInstance.v2";

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

qint64 argumentPid(const QStringList &arguments)
{
    const int index = arguments.indexOf(QStringLiteral("--parent-pid"));
    if (index < 0 || index + 1 >= arguments.size())
        return 0;
    bool ok = false;
    const qint64 value = arguments.at(index + 1).toLongLong(&ok);
    return ok && value > 0 ? value : 0;
}

void requestLauncherQuit()
{
    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(kLauncherInstance), QIODevice::WriteOnly);
    if (!socket.waitForConnected(600))
        return;
    socket.write("quit-update");
    socket.flush();
    socket.waitForBytesWritten(600);
    socket.disconnectFromServer();
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

bool processIdAlive(qint64 pid)
{
    if (pid <= 0)
        return false;
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process)
        return false;
    DWORD exitCode = 0;
    const bool alive = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
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
            WaitForSingleObject(process, 1500);
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

QString psQuote(QString value)
{
    value.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(value);
}

bool launchSetupDelayedElevated(const QString &setupPath, QString *error)
{
    if (!QFileInfo::exists(setupPath)) {
        if (error)
            *error = QStringLiteral("فایل Setup پیدا نشد: %1").arg(setupPath);
        return false;
    }

    const QString nativeSetup = QDir::toNativeSeparators(QFileInfo(setupPath).absoluteFilePath());
    QString script = QStringLiteral(
        "$p=%1; Start-Sleep -Milliseconds 900; "
        "$a=@('/SILENT','/SUPPRESSMSGBOXES','/NORESTART','/CLOSEAPPLICATIONS','/FORCECLOSEAPPLICATIONS'); "
        "Start-Process -FilePath $p -ArgumentList $a -Wait")
        .arg(psQuote(nativeSetup));

    const std::wstring parameters = QStringLiteral("-NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -Command \"%1\"")
                                        .arg(script.replace(QLatin1Char('"'), QStringLiteral("\\\"")))
                                        .toStdWString();

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = L"powershell.exe";
    info.lpParameters = parameters.c_str();
    info.nShow = SW_HIDE;
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
    Updater(bool background, qint64 parentPid, QObject *parent = nullptr)
        : QObject(parent), m_background(background), m_parentPid(parentPid)
    {
        m_pollTimer.setInterval(kBackgroundCheckIntervalMs);
        connect(&m_pollTimer, &QTimer::timeout, this, &Updater::check);
        if (m_background)
            m_pollTimer.start();

        m_parentTimer.setInterval(kParentPollIntervalMs);
        connect(&m_parentTimer, &QTimer::timeout, this, &Updater::checkParent);
        if (m_background)
            m_parentTimer.start();

        QTimer::singleShot(0, this, &Updater::check);
    }

private slots:
    void check()
    {
        if (m_checkInFlight || m_downloadReply || QDateTime::currentDateTimeUtc() < m_snoozeUntil)
            return;
        m_checkInFlight = true;
        m_endpointIndex = 0;
        requestManifest();
    }

    void checkParent()
    {
        if (!m_background || m_installFlow)
            return;
#ifdef Q_OS_WIN
        const bool alive = m_parentPid > 0 ? processIdAlive(m_parentPid) : processExists(L"IrAutoXLauncher.exe");
        if (!alive)
            QCoreApplication::quit();
#endif
    }

private:
    QStringList endpoints() const
    {
        return {
            QStringLiteral("https://irautox.ir/version/version.json"),
            QStringLiteral("http://irautox.ir/version/version.json"),
            QStringLiteral("http://5.57.37.122/version/version.json")
        };
    }

    void requestManifest()
    {
        const QStringList urls = endpoints();
        if (m_endpointIndex >= urls.size()) {
            m_checkInFlight = false;
            if (!m_background) {
                irautox::StyledMessageBox::warning(nullptr, QStringLiteral("IrAutoX Updater"),
                                                   QStringLiteral("ارتباط با سرور بروزرسانی برقرار نشد."));
                QCoreApplication::quit();
            }
            return;
        }

        QNetworkRequest request{QUrl(urls.at(m_endpointIndex++))};
        request.setTransferTimeout(5000);
        request.setRawHeader("Accept", "application/json, */*");
        request.setRawHeader("Accept-Encoding", "identity");
        request.setRawHeader("Cache-Control", "no-cache");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("IrAutoX-Updater/%1").arg(QString::fromLatin1(IRAUTOX_VERSION)));
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
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            requestManifest();
            return;
        }

        const QJsonObject root = document.object();
        const QString version = root.value(QStringLiteral("version")).toString().trimmed();
        QJsonObject installer = root.value(QStringLiteral("installer")).toObject();
        if (installer.isEmpty()) {
            installer.insert(QStringLiteral("url"), root.value(QStringLiteral("installer_url")));
            installer.insert(QStringLiteral("sha256"), root.value(QStringLiteral("installer_sha256")));
        }

        const QUrl url(installer.value(QStringLiteral("url")).toString().trimmed());
        const QString sha = installer.value(QStringLiteral("sha256")).toString().trimmed().toLower();
        if (version.isEmpty() || !url.isValid()
            || (url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0
                && url.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) != 0)
            || !validSha256(sha)) {
            requestManifest();
            return;
        }

        saveEncryptedManifest(body);
        m_checkInFlight = false;
        const QString current = QString::fromLatin1(IRAUTOX_VERSION);
        if (version == current) {
            if (!m_background) {
                irautox::StyledMessageBox::information(nullptr, QStringLiteral("IrAutoX Updater"),
                                                       QStringLiteral("نسخه %1 نصب است و بروزرسانی جدیدی وجود ندارد.").arg(current));
                QCoreApplication::quit();
            }
            return;
        }
        showUpdatePrompt(root, version, url, sha);
    }

    void showUpdatePrompt(const QJsonObject &root, const QString &version, const QUrl &url, const QString &sha)
    {
        QDialog dialog;
        dialog.setObjectName(QStringLiteral("updaterDialog"));
        dialog.setWindowTitle(QStringLiteral("IrAutoX Update"));
        dialog.setMinimumSize(560, 420);
        irautox::WindowChrome::makeFrameless(&dialog);
        auto *outer = new QVBoxLayout(&dialog);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        outer->addWidget(irautox::WindowChrome::createTitleBar(&dialog, QStringLiteral("IrAutoX Updater"), true));
        auto *body = new QWidget(&dialog);
        auto *layout = new QVBoxLayout(body);
        layout->setContentsMargins(28, 24, 28, 24);
        layout->setSpacing(14);
        auto *title = new QLabel(root.value(QStringLiteral("title")).toString(QStringLiteral("بروزرسانی جدید IrAutoX آماده است")), body);
        title->setObjectName(QStringLiteral("pageTitle"));
        auto *meta = new QLabel(QStringLiteral("نسخه فعلی: %1    نسخه جدید: %2").arg(QString::fromLatin1(IRAUTOX_VERSION), version), body);
        meta->setObjectName(QStringLiteral("accent"));
        auto *notes = new QPlainTextEdit(body);
        notes->setReadOnly(true);
        const QString notesText = changelogText(root);
        notes->setPlainText(notesText.isEmpty() ? QStringLiteral("بهبودهای عملکرد، امنیت و پایداری.") : notesText);
        auto *buttons = new QHBoxLayout;
        auto *later = new QPushButton(QIcon(QStringLiteral(":/icons/minimize.svg")), QStringLiteral("بعداً"), body);
        auto *install = new QPushButton(QIcon(QStringLiteral(":/logo.svg")), QStringLiteral("نصب بروزرسانی"), body);
        install->setObjectName(QStringLiteral("primary"));
        buttons->addStretch();
        buttons->addWidget(later);
        buttons->addWidget(install);
        layout->addWidget(title);
        layout->addWidget(meta);
        layout->addWidget(notes, 1);
        layout->addLayout(buttons);
        outer->addWidget(body, 1);
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
        m_downloadVersion = version;
        m_downloadUrl = url;
        m_expectedSha = expectedSha;
        m_downloadAttempt = 0;
        m_target = QDir(packageRoot()).filePath(QStringLiteral("IrAutoX-Launcher-v%1-Setup.exe").arg(version));

        if (m_progressDialog)
            m_progressDialog->deleteLater();
        m_progressDialog = new QDialog;
        m_progressDialog->setObjectName(QStringLiteral("updaterProgress"));
        m_progressDialog->setWindowTitle(QStringLiteral("IrAutoX Updater"));
        m_progressDialog->setMinimumSize(520, 230);
        irautox::WindowChrome::makeFrameless(m_progressDialog);
        auto *outer = new QVBoxLayout(m_progressDialog);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        outer->addWidget(irautox::WindowChrome::createTitleBar(m_progressDialog, QStringLiteral("دریافت بروزرسانی"), false));
        auto *body = new QWidget(m_progressDialog);
        auto *layout = new QVBoxLayout(body);
        layout->setContentsMargins(26, 24, 26, 24);
        auto *label = new QLabel(QStringLiteral("در حال دریافت نسخه %1…").arg(version), body);
        label->setObjectName(QStringLiteral("sectionTitle"));
        m_progressBar = new QProgressBar(body);
        m_progressBar->setRange(0, 100);
        m_progressDetail = new QLabel(QStringLiteral("در حال اتصال به سرور…"), body);
        m_progressDetail->setObjectName(QStringLiteral("muted"));
        layout->addWidget(label);
        layout->addWidget(m_progressBar);
        layout->addWidget(m_progressDetail);
        outer->addWidget(body, 1);
        m_progressDialog->show();
        startInstallerDownload();
    }

    void startInstallerDownload()
    {
        if (m_downloadReply) {
            m_downloadReply->abort();
            m_downloadReply->deleteLater();
            m_downloadReply = nullptr;
        }
        if (m_output.isOpen())
            m_output.close();
        QFile::remove(m_target);

        m_output.setFileName(m_target);
        if (!m_output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            finishDownloadWithError(QStringLiteral("ساخت فایل Setup در Temp ممکن نبود.\n%1").arg(m_target), false);
            return;
        }

        if (m_progressBar)
            m_progressBar->setValue(0);
        if (m_progressDetail)
            m_progressDetail->setText(m_downloadAttempt == 0
                ? QStringLiteral("در حال اتصال به سرور…")
                : QStringLiteral("شروع مجدد دانلود از صفر - تلاش %1").arg(m_downloadAttempt + 1));

        QNetworkRequest request(m_downloadUrl);
        request.setTransferTimeout(60000);
        request.setRawHeader("Accept", "*/*");
        request.setRawHeader("Accept-Encoding", "identity");
        request.setRawHeader("Cache-Control", "no-cache");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("IrAutoX-Updater/%1").arg(QString::fromLatin1(IRAUTOX_VERSION)));

        m_downloadReply = m_network.get(request);
        connect(m_downloadReply, &QNetworkReply::readyRead, this, [this] {
            if (!m_downloadReply || !m_output.isOpen())
                return;
            const QByteArray bytes = m_downloadReply->readAll();
            if (!bytes.isEmpty() && m_output.write(bytes) != bytes.size()) {
                const QString error = m_output.errorString();
                m_downloadReply->abort();
                finishDownloadWithError(error, false);
            }
        });
        connect(m_downloadReply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
            if (m_progressBar && total > 0)
                m_progressBar->setValue(static_cast<int>((got * 100) / total));
            if (m_progressDetail)
                m_progressDetail->setText(QStringLiteral("%1 / %2 MB").arg(got / 1048576.0, 0, 'f', 1).arg(total / 1048576.0, 0, 'f', 1));
        });
        connect(m_downloadReply, &QNetworkReply::finished, this, [this] { onInstallerDownloadFinished(); });
    }

    void onInstallerDownloadFinished()
    {
        if (!m_downloadReply)
            return;

        const int status = m_downloadReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError errorCode = m_downloadReply->error();
        const QString errorText = m_downloadReply->errorString();
        const QByteArray remaining = m_downloadReply->readAll();
        if (m_output.isOpen() && !remaining.isEmpty())
            m_output.write(remaining);
        if (m_output.isOpen()) {
            m_output.flush();
            m_output.close();
        }
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;

        const bool ok = errorCode == QNetworkReply::NoError && (status == 0 || (status >= 200 && status < 300));
        if (!ok) {
            QFile::remove(m_target);
            const QString reason = status > 0
                ? QStringLiteral("HTTP %1 - %2").arg(status).arg(errorText)
                : errorText;
            finishDownloadWithError(reason, true);
            return;
        }

        QFile file(m_target);
        if (!file.open(QIODevice::ReadOnly)) {
            QFile::remove(m_target);
            finishDownloadWithError(QStringLiteral("فایل دانلودشده قابل خواندن نیست."), true);
            return;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&file)) {
            file.close();
            QFile::remove(m_target);
            finishDownloadWithError(QStringLiteral("محاسبه SHA-256 ناموفق بود."), true);
            return;
        }
        const QString actual = QString::fromLatin1(hash.result().toHex()).toLower();
        file.close();
        if (actual != m_expectedSha) {
            QFile::remove(m_target);
            finishDownloadWithError(QStringLiteral("SHA-256 فایل دریافتی صحیح نبود و فایل ناقص حذف شد."), true);
            return;
        }

        beginInstall();
    }

    void finishDownloadWithError(const QString &error, bool allowRetry)
    {
        if (m_output.isOpen())
            m_output.close();
        QFile::remove(m_target);

        if (allowRetry && m_downloadAttempt < kMaxDownloadRetries) {
            ++m_downloadAttempt;
            if (m_progressDetail)
                m_progressDetail->setText(QStringLiteral("%1\nدانلود خراب حذف شد؛ شروع مجدد از صفر…").arg(error));
            QTimer::singleShot(700 * m_downloadAttempt, this, [this] { startInstallerDownload(); });
            return;
        }

        if (m_progressDialog) {
            m_progressDialog->close();
            m_progressDialog->deleteLater();
            m_progressDialog = nullptr;
            m_progressBar = nullptr;
            m_progressDetail = nullptr;
        }
        irautox::StyledMessageBox::warning(nullptr, QStringLiteral("IrAutoX Updater"),
                                           QStringLiteral("دانلود بروزرسانی ناموفق بود.\n%1").arg(error));
        if (!m_background)
            QCoreApplication::quit();
    }

    void beginInstall()
    {
        QSaveFile marker(QDir(updaterRoot()).filePath(QStringLiteral("update-pending.txt")));
        if (marker.open(QIODevice::WriteOnly | QIODevice::Text)) {
            marker.write(m_downloadVersion.toUtf8());
            marker.commit();
        }

#ifdef Q_OS_WIN
        m_installFlow = true;
        m_parentTimer.stop();
        requestLauncherQuit();
        if (!waitUntilClosed(L"IrAutoXLauncher.exe", 3500)) {
            terminateProcesses(L"IrAutoXLauncher.exe");
            waitUntilClosed(L"IrAutoXLauncher.exe", 2000);
        }

        if (processExists(L"IrAutoXLauncher.exe")) {
            m_installFlow = false;
            m_parentTimer.start();
            irautox::StyledMessageBox::critical(nullptr, QStringLiteral("IrAutoX Updater"),
                                                QStringLiteral("لانچر بسته نشد. بروزرسانی برای جلوگیری از خرابی فایل‌ها متوقف شد."));
            return;
        }

        QString setupError;
        if (!launchSetupDelayedElevated(m_target, &setupError)) {
            m_installFlow = false;
            m_parentTimer.start();
            irautox::StyledMessageBox::critical(nullptr, QStringLiteral("IrAutoX Updater"), setupError);
            return;
        }

        if (m_progressDialog)
            m_progressDialog->close();
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
#else
        irautox::StyledMessageBox::critical(nullptr, QStringLiteral("IrAutoX Updater"),
                                            QStringLiteral("این بروزرسان برای ویندوز ساخته شده است."));
#endif
    }

    bool m_background = false;
    bool m_checkInFlight = false;
    bool m_installFlow = false;
    qint64 m_parentPid = 0;
    int m_endpointIndex = 0;
    int m_downloadAttempt = 0;
    QDateTime m_snoozeUntil;
    QNetworkAccessManager m_network;
    QFile m_output;
    QTimer m_pollTimer;
    QTimer m_parentTimer;
    QNetworkReply *m_downloadReply = nullptr;
    QDialog *m_progressDialog = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_progressDetail = nullptr;
    QString m_downloadVersion;
    QUrl m_downloadUrl;
    QString m_expectedSha;
    QString m_target;
};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("IrAutoX"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("irautox.ir"));
    QCoreApplication::setApplicationName(QStringLiteral("Updater"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(IRAUTOX_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setLayoutDirection(Qt::RightToLeft);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/logo.svg")));
    irautox::WindowChrome::installBundledFont(app, 10);
    QFile style(QStringLiteral(":/theme.qss"));
    if (style.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(style.readAll()));

    QLockFile lock(QDir(updaterRoot()).filePath(QStringLiteral("updater.lock")));
    lock.setStaleLockTime(5000);
    if (!lock.tryLock(0))
        return 0;

    const bool background = app.arguments().contains(QStringLiteral("--background"));
    const qint64 parentPid = argumentPid(app.arguments());
    Updater updater(background, parentPid);
    return app.exec();
}

#include "main_v203.moc"
