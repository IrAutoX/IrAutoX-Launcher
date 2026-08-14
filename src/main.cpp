#include "core/ShortcutSync.h"
#include "core/SingleInstance.h"
#include "ui/LauncherChrome.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QProcess>
#include <QTimer>

namespace {

void installEmbeddedPersianFont(QApplication &app)
{
    QString family = QStringLiteral("Tahoma");
    QFile fontResource(QStringLiteral(":/vazirmatn.b64"));
    if (fontResource.open(QIODevice::ReadOnly)) {
        const QByteArray bytes = QByteArray::fromBase64(fontResource.readAll().trimmed());
        if (!bytes.isEmpty()) {
            const int fontId = QFontDatabase::addApplicationFontFromData(bytes);
            if (fontId >= 0) {
                const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
                if (!families.isEmpty())
                    family = families.first();
            }
        }
    }
    app.setFont(QFont(family, 10));
}

void startUpdater()
{
#ifdef Q_OS_WIN
    const QString updater = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("IrAutoXUpdater.exe"));
    if (QFileInfo::exists(updater))
        QProcess::startDetached(updater, {QStringLiteral("--background")}, QCoreApplication::applicationDirPath());
#endif
}

QString launchMessage(qint64 gameId)
{
    return gameId > 0 ? QStringLiteral("launch:%1").arg(gameId) : QStringLiteral("show");
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("IrAutoX"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("irautox.ir"));
    QCoreApplication::setApplicationName(QStringLiteral("Launcher"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(IRAUTOX_VERSION));
    QApplication::setQuitOnLastWindowClosed(true);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/logo.svg")));
    QApplication::setLayoutDirection(Qt::RightToLeft);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("IrAutoX native game launcher"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption launchGame(QStringLiteral("launch-game"),
                                  QStringLiteral("Launch an installed game by IrAutoX game ID."),
                                  QStringLiteral("id"));
    QCommandLineOption background(QStringLiteral("background"),
                                  QStringLiteral("Start the launcher in background mode."));
    parser.addOption(launchGame);
    parser.addOption(background);
    parser.process(app);

    bool ok = false;
    const qint64 requestedGame = parser.value(launchGame).toLongLong(&ok);
    const qint64 gameId = ok && requestedGame > 0 ? requestedGame : 0;

    QString instanceName = QStringLiteral("IrAutoXLauncher");
    const QString userName = qEnvironmentVariable("USERNAME").trimmed();
    if (!userName.isEmpty())
        instanceName += QStringLiteral("-") + userName;

    irautox::SingleInstance singleInstance(instanceName);
    if (!singleInstance.acquire()) {
        if (parser.isSet(background) && gameId <= 0)
            return 0;
        singleInstance.sendMessage(launchMessage(gameId));
        return 0;
    }

    installEmbeddedPersianFont(app);
    QFile style(QStringLiteral(":/theme.qss"));
    if (style.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(style.readAll()));

    irautox::MainWindow window;
    irautox::LauncherChrome::install(&window);
    irautox::ShortcutSync shortcutSync(&window);

    QObject::connect(&singleInstance, &irautox::SingleInstance::messageReceived,
                     &window, [&window](const QString &message) {
        if (message.startsWith(QStringLiteral("launch:"))) {
            bool idOk = false;
            const qint64 id = message.mid(7).toLongLong(&idOk);
            if (idOk && id > 0) {
                window.requestGameLaunch(id);
                return;
            }
        }
        window.showNormal();
        window.raise();
        window.activateWindow();
    });

    startUpdater();

    if (gameId > 0)
        QTimer::singleShot(0, &window, [&window, gameId] { window.requestGameLaunch(gameId); });

    if (parser.isSet(background) && gameId <= 0) {
        auto *backgroundGuard = new QTimer(&window);
        backgroundGuard->setInterval(500);
        int *remaining = new int(24);
        QObject::connect(backgroundGuard, &QTimer::timeout, &window, [&window, backgroundGuard, remaining] {
            if (window.isVisible())
                window.hide();
            --(*remaining);
            if (*remaining <= 0) {
                backgroundGuard->stop();
                backgroundGuard->deleteLater();
                delete remaining;
            }
        });
        backgroundGuard->start();
    }

    return app.exec();
}
