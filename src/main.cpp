#include "ui/MainWindow.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QIcon>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <dwmapi.h>
#endif

namespace {
void enableDarkTitleBar(QWidget *window)
{
#ifdef Q_OS_WIN
    const BOOL enabled = TRUE;
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    constexpr DWORD useImmersiveDarkMode = 20;
    DwmSetWindowAttribute(hwnd, useImmersiveDarkMode, &enabled, sizeof(enabled));
#else
    Q_UNUSED(window)
#endif
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
    app.setFont(QFont(QStringLiteral("Segoe UI"), 10));

    QFile style(QStringLiteral(":/theme.qss"));
    if (style.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(style.readAll()));

    irautox::MainWindow window;
    enableDarkTitleBar(&window);
    return app.exec();
}

