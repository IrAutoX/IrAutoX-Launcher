#include "ui/LauncherChrome.h"

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPushButton>
#include <QSizeGrip>
#include <QTimer>

namespace irautox {
namespace {

class WindowTitleBar final : public QFrame {
public:
    explicit WindowTitleBar(QMainWindow *window)
        : QFrame(window), m_window(window)
    {
        setObjectName(QStringLiteral("windowTitleBar"));
        setFixedHeight(46);
        setLayoutDirection(Qt::LeftToRight);

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(14, 0, 0, 0);
        layout->setSpacing(6);

        auto *logo = new QLabel(this);
        logo->setPixmap(QIcon(QStringLiteral(":/logo.svg")).pixmap(28, 28));
        auto *title = new QLabel(QStringLiteral("IrAutoX"), this);
        title->setObjectName(QStringLiteral("windowBrand"));
        auto *subtitle = new QLabel(QObject::tr("Game Launcher"), this);
        subtitle->setObjectName(QStringLiteral("windowSubtitle"));

        layout->addWidget(logo);
        layout->addWidget(title);
        layout->addSpacing(4);
        layout->addWidget(subtitle);
        layout->addStretch();

        auto *minimize = windowButton(QStringLiteral("windowMinimize"), QStringLiteral(":/icons/minimize.svg"));
        auto *maximize = windowButton(QStringLiteral("windowMaximize"), QStringLiteral(":/icons/maximize.svg"));
        auto *close = windowButton(QStringLiteral("windowClose"), QStringLiteral(":/icons/close.svg"));

        connect(minimize, &QPushButton::clicked, window, &QWidget::showMinimized);
        connect(maximize, &QPushButton::clicked, this, [this] { toggleMaximize(); });
        connect(close, &QPushButton::clicked, window, &QWidget::close);

        layout->addWidget(minimize);
        layout->addWidget(maximize);
        layout->addWidget(close);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_dragging = true;
            m_dragOffset = event->globalPosition().toPoint() - m_window->frameGeometry().topLeft();
            event->accept();
            return;
        }
        QFrame::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_dragging && (event->buttons() & Qt::LeftButton)) {
            if (m_window->isMaximized())
                m_window->showNormal();
            m_window->move(event->globalPosition().toPoint() - m_dragOffset);
            event->accept();
            return;
        }
        QFrame::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        m_dragging = false;
        QFrame::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            toggleMaximize();
            event->accept();
            return;
        }
        QFrame::mouseDoubleClickEvent(event);
    }

private:
    QPushButton *windowButton(const QString &name, const QString &iconPath)
    {
        auto *button = new QPushButton(this);
        button->setObjectName(name);
        button->setFixedSize(46, 46);
        button->setFlat(true);
        button->setIcon(QIcon(iconPath));
        button->setIconSize(QSize(16, 16));
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    }

    void toggleMaximize()
    {
        if (m_window->isMaximized())
            m_window->showNormal();
        else
            m_window->showMaximized();
    }

    QMainWindow *m_window = nullptr;
    bool m_dragging = false;
    QPoint m_dragOffset;
};

QString navText(int page)
{
    switch (page) {
    case 0: return QObject::tr("فروشگاه");
    case 1: return QObject::tr("کتابخانه من");
    case 2: return QObject::tr("دانلودها");
    case 3: return QObject::tr("دوستان");
    case 4: return QObject::tr("پروفایل");
    case 5: return QObject::tr("تنظیمات");
    case 6: return QObject::tr("پنل مدیریت");
    default: return {};
    }
}

QString navIcon(int page)
{
    switch (page) {
    case 0: return QStringLiteral(":/icons/store.svg");
    case 1: return QStringLiteral(":/icons/library.svg");
    case 2: return QStringLiteral(":/icons/download.svg");
    case 3: return QStringLiteral(":/icons/friends.svg");
    case 4: return QStringLiteral(":/icons/profile.svg");
    case 5: return QStringLiteral(":/icons/settings.svg");
    case 6: return QStringLiteral(":/icons/admin.svg");
    default: return {};
    }
}

} // namespace

void LauncherChrome::install(QMainWindow *window)
{
    if (!window)
        return;

    window->setWindowFlag(Qt::FramelessWindowHint, true);
    window->setMenuWidget(new WindowTitleBar(window));

    const QList<QPushButton *> buttons = window->findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        if (button->objectName() != QStringLiteral("nav"))
            continue;
        const int page = button->property("page").toInt();
        const QString text = navText(page);
        const QString icon = navIcon(page);
        if (!text.isEmpty())
            button->setText(text);
        if (!icon.isEmpty()) {
            button->setIcon(QIcon(icon));
            button->setIconSize(QSize(18, 18));
        }
    }

    auto *grip = new QSizeGrip(window);
    grip->setObjectName(QStringLiteral("windowSizeGrip"));
    grip->setFixedSize(18, 18);
    grip->raise();

    auto *timer = new QTimer(window);
    timer->setInterval(150);
    QObject::connect(timer, &QTimer::timeout, window, [window, grip] {
        grip->move(window->width() - grip->width(), window->height() - grip->height());
        grip->raise();
    });
    timer->start();
}

} // namespace irautox
