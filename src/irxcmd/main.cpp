#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocalSocket>
#include <QProcess>
#include <QStringList>

namespace {
constexpr auto kInstanceName = "IrAutoXLauncher.SingleInstance.v2";

QString routeFromArgs(const QStringList &args)
{
    const int launch = args.indexOf(QStringLiteral("--launch-game"));
    if (launch >= 0 && launch + 1 < args.size())
        return QStringLiteral("launch:%1").arg(args.at(launch + 1));

    const int download = args.indexOf(QStringLiteral("--download-game"));
    if (download >= 0 && download + 1 < args.size())
        return QStringLiteral("uri:irautox://download/%1").arg(args.at(download + 1));

    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i).startsWith(QStringLiteral("irautox://"), Qt::CaseInsensitive))
            return QStringLiteral("uri:%1").arg(args.at(i));
    }
    return QStringLiteral("activate");
}

bool forward(const QString &route)
{
    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(kInstanceName), QIODevice::WriteOnly);
    if (!socket.waitForConnected(180))
        return false;
    socket.write(route.toUtf8());
    socket.flush();
    socket.waitForBytesWritten(180);
    return true;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("IrAutoX Command Broker"));

    const QString route = routeFromArgs(app.arguments());
    if (forward(route))
        return 0;

    const QString launcher = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("IrAutoXLauncher.exe"));
    if (!QFileInfo::exists(launcher))
        return 2;

    QStringList launchArgs;
    if (route.startsWith(QStringLiteral("launch:")))
        launchArgs << QStringLiteral("--launch-game") << route.mid(7);
    else if (route.startsWith(QStringLiteral("uri:")))
        launchArgs << QStringLiteral("--uri") << route.mid(4);

    return QProcess::startDetached(launcher, launchArgs, QCoreApplication::applicationDirPath()) ? 0 : 3;
}
