#include "ui/MainWindow.h"
#include "ui/StyledMessageBox.h"
#include "ui/WindowChrome.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace {
constexpr auto kInstanceName = "IrAutoXLauncher.SingleInstance.v2";

QString routeFromArguments(const QStringList &args)
{
    if (args.contains(QStringLiteral("--sdk-wake")) || args.contains(QStringLiteral("--background")))
        return QStringLiteral("sdk-wake");
    const int routeIndex = args.indexOf(QStringLiteral("--launch-game"));
    if (routeIndex >= 0 && routeIndex + 1 < args.size())
        return QStringLiteral("launch:%1").arg(args.at(routeIndex + 1));
    const int uriIndex = args.indexOf(QStringLiteral("--uri"));
    if (uriIndex >= 0 && uriIndex + 1 < args.size())
        return QStringLiteral("uri:%1").arg(args.at(uriIndex + 1));
    for (const QString &arg : args) {
        if (arg.startsWith(QStringLiteral("irautox://"), Qt::CaseInsensitive))
            return QStringLiteral("uri:%1").arg(arg);
    }
    return QStringLiteral("activate");
}

bool forwardToExistingInstance(const QString &route)
{
    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(kInstanceName), QIODevice::WriteOnly);
    if (!socket.waitForConnected(140))
        return false;
    socket.write(route.toUtf8());
    socket.flush();
    socket.waitForBytesWritten(140);
    socket.disconnectFromServer();
    return true;
}
}

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
    irautox::WindowChrome::installBundledFont(app, 10);
    app.setProperty("irautox.startHidden", app.arguments().contains(QStringLiteral("--sdk-wake")) || app.arguments().contains(QStringLiteral("--background")));

    const QString initialRoute = routeFromArguments(app.arguments());
    if (forwardToExistingInstance(initialRoute))
        return 0;

    QLocalServer::removeServer(QString::fromLatin1(kInstanceName));
    QLocalServer routeServer;
    if (!routeServer.listen(QString::fromLatin1(kInstanceName)))
        return 2;

    QFile style(QStringLiteral(":/theme.qss"));
    if (style.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(style.readAll()));

    irautox::MainWindow window;
    window.setWindowFlag(Qt::FramelessWindowHint, true);

    auto dispatchRoute = [&window](const QString &route) {
        if (route == QStringLiteral("quit-update")) {
            QCoreApplication::quit();
            return;
        }
        if (route.startsWith(QStringLiteral("launch:"))) {
            bool ok = false;
            const qint64 gameId = route.mid(7).toLongLong(&ok);
            if (ok && gameId > 0)
                window.setStartupGameId(gameId);
        } else if (route.startsWith(QStringLiteral("uri:"))) {
            window.handleProtocolUrl(route.mid(4));
        }
        if (route != QStringLiteral("sdk-wake")) {
            window.showNormal();
            window.raise();
            window.activateWindow();
        }
    };

    QObject::connect(&routeServer, &QLocalServer::newConnection, &app, [&routeServer, dispatchRoute] {
        while (QLocalSocket *socket = routeServer.nextPendingConnection()) {
            QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket, dispatchRoute] {
                dispatchRoute(QString::fromUtf8(socket->readAll()).trimmed());
                socket->disconnectFromServer();
            });
        }
    });

    if (initialRoute.startsWith(QStringLiteral("launch:")) || initialRoute.startsWith(QStringLiteral("uri:")))
        QTimer::singleShot(0, &app, [dispatchRoute, initialRoute] { dispatchRoute(initialRoute); });

    const QString pendingUpdate = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                                      .filePath(QStringLiteral("updater/update-pending.txt"));
    QFile marker(pendingUpdate);
    if (marker.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString version = QString::fromUtf8(marker.readAll()).trimmed();
        marker.close();
        QFile::remove(pendingUpdate);
        QTimer::singleShot(1500, &window, [&window, version] {
            irautox::StyledMessageBox::information(&window, QStringLiteral("IrAutoX"),
                                                   QStringLiteral("بروزرسانی %1 با موفقیت نصب شد. ممنون که IrAutoX را به‌روز نگه می‌دارید.").arg(version));
        });
    }

    if (!app.arguments().contains(QStringLiteral("--no-updater"))) {
        const QString updater = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("IrAutoXUpdater.exe"));
        if (QFile::exists(updater))
            QTimer::singleShot(100, &app, [updater] { QProcess::startDetached(updater, {QStringLiteral("--background"), QStringLiteral("--fast-check")}); });
    }

    return app.exec();
}
