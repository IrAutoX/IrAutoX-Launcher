#include "ui/StyledMessageBox.h"

#include "ui/WindowChrome.h"

#include <QDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace irautox {
namespace {

enum class Kind { Information, Warning, Critical, Question };

QMessageBox::StandardButton showBox(QWidget *parent, const QString &title, const QString &text, Kind kind)
{
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("messageDialog"));
    dialog.setModal(true);
    dialog.setMinimumWidth(430);
    dialog.setWindowTitle(title);
    WindowChrome::makeFrameless(&dialog);

    auto *outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(WindowChrome::createTitleBar(&dialog, title, false));

    auto *body = new QWidget(&dialog);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(16);
    auto *row = new QHBoxLayout;
    auto *icon = new QLabel(body);
    icon->setFixedSize(42, 42);
    QStyle::StandardPixmap pixmap = QStyle::SP_MessageBoxInformation;
    if (kind == Kind::Warning) pixmap = QStyle::SP_MessageBoxWarning;
    if (kind == Kind::Critical) pixmap = QStyle::SP_MessageBoxCritical;
    if (kind == Kind::Question) pixmap = QStyle::SP_MessageBoxQuestion;
    icon->setPixmap(dialog.style()->standardIcon(pixmap).pixmap(36, 36));
    auto *label = new QLabel(text, body);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row->addWidget(icon, 0, Qt::AlignTop);
    row->addWidget(label, 1);
    layout->addLayout(row);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    QMessageBox::StandardButton result = QMessageBox::NoButton;
    if (kind == Kind::Question) {
        auto *no = new QPushButton(QStringLiteral("خیر"), body);
        auto *yes = new QPushButton(QStringLiteral("بله"), body);
        yes->setObjectName(QStringLiteral("primary"));
        QObject::connect(no, &QPushButton::clicked, &dialog, [&] { result = QMessageBox::No; dialog.reject(); });
        QObject::connect(yes, &QPushButton::clicked, &dialog, [&] { result = QMessageBox::Yes; dialog.accept(); });
        buttons->addWidget(no);
        buttons->addWidget(yes);
    } else {
        auto *ok = new QPushButton(QStringLiteral("باشه"), body);
        ok->setObjectName(QStringLiteral("primary"));
        QObject::connect(ok, &QPushButton::clicked, &dialog, [&] { result = QMessageBox::Ok; dialog.accept(); });
        buttons->addWidget(ok);
    }
    layout->addLayout(buttons);
    outer->addWidget(body);
    dialog.exec();
    if (kind == Kind::Question && result == QMessageBox::NoButton)
        result = QMessageBox::No;
    return result;
}

}

void StyledMessageBox::information(QWidget *parent, const QString &title, const QString &text)
{
    showBox(parent, title, text, Kind::Information);
}

void StyledMessageBox::warning(QWidget *parent, const QString &title, const QString &text)
{
    showBox(parent, title, text, Kind::Warning);
}

void StyledMessageBox::critical(QWidget *parent, const QString &title, const QString &text)
{
    showBox(parent, title, text, Kind::Critical);
}

QMessageBox::StandardButton StyledMessageBox::question(QWidget *parent, const QString &title, const QString &text)
{
    return showBox(parent, title, text, Kind::Question);
}

}
