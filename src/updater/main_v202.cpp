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
    const std::wstring params = L"/SILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS /FORCECLOSEAPPLICATIONS";
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = file.c_str();
    info.lpParameters = params.c_str();
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
        request.setTransferTimeout(3500);
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
        const QString urlText = installer.value(QStringLiteral("url")).toString().trimmed();
        const QString sha = installer.value(QStringLiteral("sha256")).toString().trimmed().toLower();
        const QUrl url(urlText);
        if (version.isEmpty() || !url.isValid() || url.scheme().isEmpty() || !validSha256(sha)) {
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
        notes->setPlainText(changelogText(root).isEmpty() ? QStringLiteral("بهبودهای عملکرد، امنیت و پایداری.") : changelogText(root));
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
        const QString target = QDir(packageRoot()).filePath(QStringLiteral("IrAutoX-Launcher-v%1-Setup.exe").arg(version));
        QFile::remove(target);
        m_output.setFileName(target);
        if (!m_output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            irautox::StyledMessageBox::critical(nullptr, QStringLiteral("IrAutoX Updater"),
                                                QStringLiteral("ساخت فایل Setup در Temp ممکن نبود.\n%1").arg(target));
            return;
        }

        auto *progress = new QDialog;
        progress->setObjectName(QStringLiteral("updaterProgress"));
        progress->setWindowTitle(QStringLiteral("IrAutoX Updater"));
        progress->setMinimumSize(520, 230);
        irautox::WindowChrome::makeFrameless(progress);
        auto *outer = new QVBoxLayout(progress);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        outer->addWidget(irautox::WindowChrome::createTitleBar(progress, QStringLiteral("دریافت بروزرسانی"), false));
        auto *body = new QWidget(progress);
        auto *layout = new QVBoxLayout(body);
        layout->setContentsMargins(26, 24, 26, 24);
        auto *label = new QLabel(QStringLiteral("در حال دریافت نسخه %1…").arg(version), body);
        label->setObjectName(QStringLiteral("sectionTitle"));
        auto *bar = new QProgressBar(body);
        bar->setRange(0, 100);
        auto *detail = new QLabel(QStringLiteral("در حال اتصال به سرور…"), body);
        detail->setObjectName(QStringLiteral("muted"));
        layout->addWidget(label);
        layout->addWidget(bar);
        layout->addWidget(detail);
        outer->addWidget(body, 1);
        progress->show();

        QNetworkRequest request(url);
        request.setTransferTimeout(60000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("IrAutoX-Updater/%1").arg(QString::fromLatin1(IRAUTOX_VERSION)));
        QNetworkReply *reply = m_network.get(request);
        connect(reply, &QNetworkReply::readyRead, this, [this, reply] { m_output.write(reply->readAll()); });
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
                irautox::StyledMessageBox::warning(nullptr, QStringLiteral("IrAutoX Updater"),
                                                   QStringLiteral("دانلود بروزرسانی ناموفق بود. HTTP %1\n%2").arg(status).arg(networkError));
                return;
            }

            QFile file(target);
            if (!file.open(QIODevice::ReadOnly)) {
                QFile::remove(target);
                irautox::StyledMessageBox::critical(nullptr, QStringLiteral("IrAutoX Updater"),
                                                    QStringLiteral("فایل دانلودشده قابل خواندن نیست."));
                return;
            }
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (!hash.addData(&file)) {
                file.close();
                QFile::remove(target);
                irautox::StyledMessageBox::critical(nullptr, QStringLiteral("IrAutoX Updater"),
                                                    QStringLiteral("محاسبه SHA-256 ناموفق بود."));
                return;
            }
            const QString actual = QString::fromLatin1(hash.result().toHex()).toLower();
            file.close();
            if (actual != expectedSha) {
                QFile::remove(target);
                irautox::StyledMessageBox::critical(nullptr, QStringLiteral("IrAutoX Security"),
                                                    QStringLiteral("SHA-256 فایل نصب با version.json مطابقت ندارد.\nExpected: %1\nActual: %2").arg(expectedSha, actual));
                return;
            }

            QSaveFile marker(QDir(updaterRoot()).filePath(QStringLiteral("update-pending.txt")));
            if (marker.open(QIODevice::WriteOnly | QIODevice::Text)) {
                marker.write(version.toUtf8());
                marker.commit();
            }
#ifdef Q_OS_WIN
            requestLauncherQuit();
            if (!waitUntilClosed(L"IrAutoXLauncher.exe", 2800)) {
                terminateProcesses(L"IrAutoXLauncher.exe");
                waitUntilClosed(L"IrAutoXLauncher.exe", 1500);
            }
            QString setupError;
            if (!launchSetupElevated(target, &setupError)) {
                irautox::StyledMessageBox::critical(nullptr, QStringLiteral("IrAutoX Updater"), setupError);
                return;
            }
            QCoreApplication::exit(0);
#else
            Q_UNUSED(target)
            irautox::StyledMessageBox::critical(nullptr, QStringLiteral("IrAutoX Updater"),
                                                QStringLiteral("این بروزرسان برای ویندوز ساخته شده است."));
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
    QCoreApplication::setApplicationVersion(QString::fromLatin1(IRAUTOX_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setLayoutDirection(Qt::RightToLeft);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/logo.svg")));
    irautox::WindowChrome::installBundledFont(app, 10);
    QFile style(QStringLiteral(":/theme.qss"));
    if (style.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(style.readAll()));

    QLockFile lock(QDir(updaterRoot()).filePath(QStringLiteral("updater.lock")));
    lock.setStaleLockTime(30000);
    if (!lock.tryLock(0))
        return 0;

    const bool background = app.arguments().contains(QStringLiteral("--background"));
    Updater updater(background);
    return app.exec();
}

#include "main_v202.moc"
