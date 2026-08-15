#include "ui/LoginDialog.h"

#include "ui/WindowChrome.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace irautox {

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("loginRoot"));
    setWindowTitle(tr("ورود به IrAutoX"));
    setWindowIcon(QIcon(QStringLiteral(":/logo.svg")));
    setModal(false);
    setMinimumSize(900, 600);
    resize(980, 660);
    WindowChrome::makeFrameless(this);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(WindowChrome::createTitleBar(this, tr("ورود به IrAutoX"), true));

    auto *body = new QWidget(this);
    auto *root = new QHBoxLayout(body);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *visual = new QFrame(body);
    visual->setStyleSheet(QStringLiteral(
        "QFrame { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #131a25,stop:0.55 #181820,stop:1 #3a1c0f); }"));
    auto *visualLayout = new QVBoxLayout(visual);
    visualLayout->setContentsMargins(52, 48, 52, 48);
    auto *logo = new QLabel(visual);
    logo->setPixmap(QIcon(QStringLiteral(":/logo.svg")).pixmap(82, 82));
    auto *brand = new QLabel(QStringLiteral("IrAutoX"), visual);
    brand->setObjectName(QStringLiteral("brand"));
    brand->setStyleSheet(QStringLiteral("font-size: 32pt;"));
    auto *headline = new QLabel(tr("همهٔ بازی‌ها،\nیک خانه."), visual);
    headline->setStyleSheet(QStringLiteral("font-size: 28pt; font-weight: 800; color: white;"));
    auto *copy = new QLabel(tr("فروشگاه، کتابخانه، دانلود و دوستان؛\nدر یک لانچر سریع و بومی."), visual);
    copy->setObjectName(QStringLiteral("muted"));
    copy->setStyleSheet(QStringLiteral("font-size: 12pt; color: #b9c0cd;"));
    visualLayout->addWidget(logo, 0, Qt::AlignLeft);
    visualLayout->addSpacing(24);
    visualLayout->addWidget(brand);
    visualLayout->addStretch();
    visualLayout->addWidget(headline);
    visualLayout->addSpacing(18);
    visualLayout->addWidget(copy);
    visualLayout->addStretch();

    auto *formHost = new QFrame(body);
    auto *form = new QVBoxLayout(formHost);
    form->setContentsMargins(64, 54, 64, 54);
    auto *title = new QLabel(tr("خوش آمدید"), formHost);
    title->setObjectName(QStringLiteral("pageTitle"));
    auto *subtitle = new QLabel(tr("برای ادامه وارد حساب IrAutoX شوید."), formHost);
    subtitle->setObjectName(QStringLiteral("muted"));
    m_connection = new QLabel(tr("در حال اتصال…"), formHost);
    m_connection->setObjectName(QStringLiteral("accent"));
    m_connection->setLayoutDirection(Qt::LeftToRight);

    m_username = new QLineEdit(formHost);
    m_username->setPlaceholderText(tr("نام کاربری"));
    m_username->setClearButtonEnabled(true);
    m_password = new QLineEdit(formHost);
    m_password->setPlaceholderText(tr("رمز عبور"));
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setClearButtonEnabled(true);
    m_remember = new QCheckBox(tr("ورود من را با رمزگذاری ویندوز حفظ کن"), formHost);
    m_remember->setChecked(true);
    m_login = new QPushButton(QIcon(QStringLiteral(":/logo.svg")), tr("ورود به لانچر"), formHost);
    m_login->setObjectName(QStringLiteral("primary"));
    m_register = new QPushButton(QIcon(QStringLiteral(":/logo.svg")), tr("ساخت حساب جدید"), formHost);

    form->addWidget(title);
    form->addWidget(subtitle);
    form->addWidget(m_connection);
    form->addStretch();
    form->addWidget(m_username);
    form->addSpacing(8);
    form->addWidget(m_password);
    form->addWidget(m_remember);
    form->addSpacing(10);
    form->addWidget(m_login);
    form->addWidget(m_register);
    form->addStretch();

    root->addWidget(visual, 11);
    root->addWidget(formHost, 9);
    outer->addWidget(body, 1);

    connect(m_login, &QPushButton::clicked, this, [this] { submit(false); });
    connect(m_register, &QPushButton::clicked, this, [this] { submit(true); });
    connect(m_password, &QLineEdit::returnPressed, this, [this] { submit(false); });
}

void LoginDialog::prefill(const QString &username, const QString &password)
{
    m_username->setText(username);
    m_password->setText(password);
}

void LoginDialog::setBusy(bool busy)
{
    m_username->setEnabled(!busy);
    m_password->setEnabled(!busy);
    m_remember->setEnabled(!busy);
    m_login->setEnabled(!busy);
    m_register->setEnabled(!busy);
    m_login->setText(busy ? tr("در حال بررسی…") : tr("ورود به لانچر"));
}

void LoginDialog::setConnectionStatus(bool connected, const QString &detail)
{
    m_connection->setText((connected ? QStringLiteral("●  ") : QStringLiteral("○  ")) + detail);
    m_connection->setStyleSheet(connected ? QStringLiteral("color: #4ade80; font-weight: 700;")
                                          : QStringLiteral("color: #ff9f43; font-weight: 700;"));
}

QString LoginDialog::username() const
{
    return m_username->text().trimmed();
}

QString LoginDialog::password() const
{
    return m_password->text();
}

bool LoginDialog::rememberCredentials() const
{
    return m_remember->isChecked();
}

void LoginDialog::submit(bool registration)
{
    if (username().size() < 3 || password().size() < 4) {
        m_connection->setText(tr("نام کاربری حداقل ۳ و رمز عبور حداقل ۴ نویسه باشد."));
        m_connection->setStyleSheet(QStringLiteral("color: #ff7d91; font-weight: 700;"));
        return;
    }
    setBusy(true);
    if (registration)
        emit registerRequested(username(), password(), rememberCredentials());
    else
        emit loginRequested(username(), password(), rememberCredentials());
}

}
