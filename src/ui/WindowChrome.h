#pragma once

#include <QString>

class QApplication;
class QFrame;
class QWidget;

namespace irautox {

class WindowChrome final {
public:
    static QString installBundledFont(QApplication &app, int pointSize = 10);
    static void makeFrameless(QWidget *window);
    static QFrame *createTitleBar(QWidget *window, const QString &title, bool allowMaximize = true);
};

}
