#include "ui/MainWindow.h"
#include "../resources/VazirEmbedded.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QProcess>
#include <QTimer>

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

    const QByteArray vazirData = QByteArray::fromBase64(QByteArrayLiteral(IRAUTOX_VAZIR_BASE64));
    const int fontId = vazirData.isEmpty() ? -1 : QFontDatabase::addApplicationFontFromData(vazirData);
    if (fontId >= 0) {
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty())
            app.setFont(QFont(families.first(), 10));
    }
    if (fontId < 0)
        app.setFont(QFont(QStringLiteral("Tahoma"), 10));

    QFile style(QStringLiteral(":/theme.qss"));
    if (style.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(style.readAll()));

    irautox::MainWindow window;
    window.setWindowFlag(Qt::FramelessWindowHint, true);

    const QStringList args = app.arguments();
    const int routeIndex = args.indexOf(QStringLiteral("--launch-game"));
    if (routeIndex >= 0 && routeIndex + 1 < args.size()) {
        bool ok = false;
        const qint64 gameId = args.at(routeIndex + 1).toLongLong(&ok);
        if (ok && gameId > 0)
            window.setStartupGameId(gameId);
    }

    if (!args.contains(QStringLiteral("--no-updater"))) {
        const QString updater = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("IrAutoXUpdater.exe"));
        if (QFile::exists(updater))
            QTimer::singleShot(1800, &app, [updater] { QProcess::startDetached(updater, {QStringLiteral("--background")}); });
    }

    return app.exec();
}
