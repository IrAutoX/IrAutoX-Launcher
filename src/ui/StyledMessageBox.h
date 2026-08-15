#pragma once

#include <QMessageBox>
#include <QString>

class QWidget;

namespace irautox {

class StyledMessageBox final {
public:
    static void information(QWidget *parent, const QString &title, const QString &text);
    static void warning(QWidget *parent, const QString &title, const QString &text);
    static void critical(QWidget *parent, const QString &title, const QString &text);
    static QMessageBox::StandardButton question(QWidget *parent, const QString &title, const QString &text);
};

}
