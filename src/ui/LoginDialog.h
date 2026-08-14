#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace irautox {

class LoginDialog final : public QDialog {
    Q_OBJECT
public:
    explicit LoginDialog(QWidget *parent = nullptr);

    void prefill(const QString &username, const QString &password);
    void setBusy(bool busy);
    void setConnectionStatus(bool connected, const QString &detail);
    QString username() const;
    QString password() const;
    bool rememberCredentials() const;

signals:
    void loginRequested(const QString &username, const QString &password, bool remember);
    void registerRequested(const QString &username, const QString &password, bool remember);

private:
    void submit(bool registration);

    QLineEdit *m_username = nullptr;
    QLineEdit *m_password = nullptr;
    QCheckBox *m_remember = nullptr;
    QPushButton *m_login = nullptr;
    QPushButton *m_register = nullptr;
    QLabel *m_connection = nullptr;
};

} // namespace irautox

