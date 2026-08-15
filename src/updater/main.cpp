#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
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
#  include <shellapi.h>
#  include <tlhelp32.h>
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

QString tempRoot()
{
    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("IrAutoX_Update"));
    QDir().mkpath(path);
    return path;
}

bool copyFileReplace(const QString &source, const QString &target)
{
    QDir().mkpath(QFileInfo(target).absolutePath());
    QFile::remove(target);
    return QFile::copy(source, target);
}

bool copyDirectory(const QString &source, const QString &target)
{
    QDir sourceDir(source);
    if (!sourceDir.exists())
        return true;
    QDir().mkpath(target);
    const QFileInfoList entries = sourceDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    for (const QFileInfo &entry : entries) {
        const QString destination = QDir(target).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyDirectory(entry.absoluteFilePath(), destination))
                return false;
        } else if (!copyFileReplace(entry.absoluteFilePath(), destination)) {
            return false;
        }
    }
    return true;
}

bool stageTempWorker(QString *error)
{
    const QString installDir = QCoreApplication::applicationDirPath();
    const QString workerDir = QDir(tempRoot()).filePath(QStringLiteral("worker"));
    QDir(workerDir).removeRecursively();
    QDir().mkpath(workerDir);

    const QString workerExe = QDir(workerDir).filePath(QStringLiteral("IrAutoXUpdater.exe"));
    if (!copyFileReplace(QCoreApplication::applicationFilePath(), workerExe)) {
        if (error) *error = QStringLiteral("کپی Updater به پوشه موقت انجام نشد.");
        return false;
    }

    QDir source(installDir);
    const QFileInfoList dlls = source.entryInfoList({QStringLiteral("*.dll")}, QDir::Files);
    for (const QFileInfo &dll : dlls) {
        if (!copyFileReplace(dll.absoluteFilePath(), QDir(workerDir).filePath(dll.fileName()))) {
            if (error) *error = QStringLiteral("کپی فایل‌های Runtime به پوشه موقت انجام نشد.");
            return false;
        }
    }

    const QStringList pluginDirs{
        QStringLiteral("platforms"), QStringLiteral("styles"), QStringLiteral("tls"),
        QStringLiteral("networkinformation"), QStringLiteral("imageformats"), QStringLiteral("iconengines")
    };
    for (const QString &name : pluginDirs) {
        const QString sourceDir = QDir(installDir).filePath(name);
        if (QFileInfo::exists(sourceDir) && !copyDirectory(sourceDir, QDir(workerDir).filePath(name))) {
            if (error) *error = QStringLiteral("کپی افزونه‌های Qt به پوشه موقت انجام نشد.");
            return false;
        }
    }

    QStringList args = QCoreApplication::arguments();
    args.removeFirst();
    args.removeAll(QStringLiteral("--temp-worker"));
    const int oldInstallIndex = args.indexOf(QStringLiteral("--install-dir"));
    if (oldInstallIndex >= 0) {
        args.removeAt(oldInstallIndex);
        if (oldInstallIndex < args.size())
            args.removeAt(oldInstallIndex);
    }
    args << QStringLiteral("--temp-worker") << QStringLiteral("--install-dir") << installDir;
    if (!QProcess::startDetached(workerExe, args, workerDir)) {
        if (error) *error = QStringLiteral("اجرای Updater موقت انجام نشد.");
        return false;
    }
    return true;
}

QByteArray protectForCurrentUser(const QByteArray &plain)
{
#ifdef Q_OS_WIN
    if (plain.isEmpty()) return {};
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
    if (socket.waitForConnected(300)) {
        socket.write("quit-update");
        socket.flush();
        socket.waitForBytesWritten(300);
    }
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
            if (process) {
                TerminateProcess(process, 0);
                WaitForSingleObject(process, 1000);
                CloseHandle(process);
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

bool waitUntilClosed(const wchar_t *exeName, DWORD timeoutMs)
{
    const DWORD start = GetTickCount();
    while (processExists(exeName)) {
        if (GetTickCount() - start >= timeoutMs)
            return false;
        Sleep(100);
    }
    return true;
}

bool runSetupAndWait(const QString &setupPath, QString *error)
{
    const std::wstring file = QDir::toNativeSeparators(setupPath).toStdWString();
    const std::wstring params = L"/SILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS";
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = file.c_str();
    info.lpParameters = params.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) {
        if (error) *error = QStringLiteral("اجرای Setup با دسترسی Administrator ناموفق بود. Windows Error: %1").arg(GetLastError());
        return false;
    }
    if (!info.hProcess) {
        if (error) *error = QStringLiteral("هندل پردازش Setup دریافت نشد.");
        return false;
    }
    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(info.hProcess, &exitCode);
    CloseHandle(info.hProcess);
    if (exitCode != 0) {
        if (error) *error = QStringLiteral("Setup با کد %1 بسته شد.").arg(exitCode);
        return false;
    }
    return true;
}

void launchInstalledLauncher(const QString &installDir)
{
    const QString launcher = QDir(installDir).filePath(QStringLiteral("IrAutoXLauncher.exe"));
    if (!QFileInfo::exists(launcher) || processExists(L"IrAutoXLauncher.exe"))
        return;
    const QString native = QDir::toNativeSeparators(launcher);
    const std::wstring argument = QStringLiteral("\"%1\"").arg(native).toStdWString();
    ShellExecuteW(nullptr, L"open", L"explorer.exe", argument.c_str(), nullptr, SW_SHOWNORMAL);
}
#endif

class Updater final : public QObject {
    Q_OBJECT
public:
    explicit Updater(bool background, QString installDir, QObject *parent = nullptr)
        : QObject(parent), m_background(background), m_installDir(std::move(installDir))
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
                QMessageBox::warning(nullptr, QStringLiteral("IrAutoX Updater"), QStringLiteral("ارتباط با سرور بروزرسانی برقرار نشد."));
                QCoreApplication::quit();
            }
            return;
        }

        QNetworkRequest req{QUrl(endpoints.at(m_endpointIndex++))};
        req.setTransferTimeout(3500);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("IrAutoX-Updater/%1").arg(QString::fromLatin1(IRAUTOX_VERSION)));
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
            if (!m_background) QCoreApplication::quit();
        }
    }

    void downloadInstaller(const QString &version, const QUrl &url, const QString &expectedSha)
    {
        const QString packageDir = QDir(tempRoot()).filePath(QStringLiteral("packages"));
        QDir().mkpath(packageDir);
        const QString target = QDir(packageDir).filePath(QStringLiteral("IrAutoX-Launcher-v%1-Setup.exe").arg(version));
        m_output.setFileName(target);
        if (!m_output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(nullptr, QStringLiteral("IrAutoX Updater"), QStringLiteral("ساخت فایل نصب موقت ممکن نبود."));
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

        QNetworkRequest req(url);
        req.setTransferTimeout(60000);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("IrAutoX-Updater/%1").arg(QString::fromLatin1(IRAUTOX_VERSION)));
        QNetworkReply *reply = m_network.get(req);
        connect(reply, &QNetworkReply::readyRead, this, [this, reply] { m_output.write(reply->readAll()); });
        connect(reply, &QNetworkReply::downloadProgress, progress, [bar, detail](qint64 got, qint64 total) {
            if (total > 0) bar->setValue(static_cast<int>((got * 100) / total));
            detail->setText(QStringLiteral("%1 / %2 MB").arg(got / 1048576.0, 0, 'f', 1).arg(total / 1048576.0, 0, 'f', 1));
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, progress, target, expectedSha, version] {
            m_output.write(reply->readAll());
            m_output.close();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool networkOk = reply->error() == QNetworkReply::NoError && (status == 0 || (status >= 200 && status < 300));
            const QString networkError = reply->errorString();
            reply->deleteLater();
            progress->close();
            progress->deleteLater();
            if (!networkOk) {
                QFile::remove(target);
                QMessageBox::warning(nullptr, QStringLiteral("IrAutoX Updater"), QStringLiteral("دانلود بروزرسانی ناموفق بود. HTTP %1\n%2").arg(status).arg(networkError));
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
            if (!runSetupAndWait(target, &setupError)) {
                QMessageBox::critical(nullptr, QStringLiteral("IrAutoX Updater"), setupError);
                return;
            }
            launchInstalledLauncher(m_installDir);
            QTimer::singleShot(300, qApp, &QCoreApplication::quit);
#else
            if (!QProcess::startDetached(target, {})) {
                QMessageBox::critical(nullptr, QStringLiteral("IrAutoX Updater"), QStringLiteral("اجرای Setup ناموفق بود."));
                return;
            }
            QCoreApplication::quit();
#endif
        });
    }

    bool m_background = false;
    bool m_checkInFlight = false;
    int m_endpointIndex = 0;
    QString m_installDir;
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

    const bool tempWorker = app.arguments().contains(QStringLiteral("--temp-worker"));
    if (!tempWorker) {
        QLockFile bootstrap(QDir(tempRoot()).filePath(QStringLiteral("bootstrap.lock")));
        bootstrap.setStaleLockTime(15000);
        if (!bootstrap.tryLock(0))
            return 0;
        QString error;
        if (!stageTempWorker(&error)) {
            QMessageBox::critical(nullptr, QStringLiteral("IrAutoX Updater"), error);
            return 2;
        }
        return 0;
    }

    QString installDir;
    const int installIndex = app.arguments().indexOf(QStringLiteral("--install-dir"));
    if (installIndex >= 0 && installIndex + 1 < app.arguments().size())
        installDir = QDir::cleanPath(app.arguments().at(installIndex + 1));
    if (installDir.isEmpty()) {
        QMessageBox::critical(nullptr, QStringLiteral("IrAutoX Updater"), QStringLiteral("مسیر نصب لانچر مشخص نیست."));
        return 3;
    }

    QLockFile lock(QDir(updaterRoot()).filePath(QStringLiteral("updater.lock")));
    lock.setStaleLockTime(30000);
    if (!lock.tryLock(0))
        return 0;

    const bool background = app.arguments().contains(QStringLiteral("--background"));
    Updater updater(background, installDir);
    return app.exec();
}

#include "main.moc"
