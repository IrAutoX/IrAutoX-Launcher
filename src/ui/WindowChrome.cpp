#include "ui/WindowChrome.h"

#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVariant>
#include <QWidget>

namespace irautox {
namespace {

class DragFilter final : public QObject {
public:
    DragFilter(QWidget *window, QWidget *bar)
        : QObject(bar), m_window(window)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (!m_window)
            return QObject::eventFilter(watched, event);
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton && m_allowMaximize) {
                m_window->isMaximized() ? m_window->showNormal() : m_window->showMaximized();
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_dragging = true;
                m_offset = mouse->globalPosition().toPoint() - m_window->frameGeometry().topLeft();
                return false;
            }
        }
        if (event->type() == QEvent::MouseMove && m_dragging) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->buttons().testFlag(Qt::LeftButton) && !m_window->isMaximized()) {
                m_window->move(mouse->globalPosition().toPoint() - m_offset);
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonRelease)
            m_dragging = false;
        return QObject::eventFilter(watched, event);
    }

public:
    void setAllowMaximize(bool value) { m_allowMaximize = value; }

private:
    QWidget *m_window = nullptr;
    QPoint m_offset;
    bool m_dragging = false;
    bool m_allowMaximize = true;
};

}

QString WindowChrome::installBundledFont(QApplication &app, int pointSize)
{
    const int fontId = QFontDatabase::addApplicationFont(QStringLiteral(":/Vazir.ttf"));
    QString family = QStringLiteral("Tahoma");
    if (fontId >= 0) {
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty())
            family = families.first();
    }
    app.setFont(QFont(family, pointSize));
    return family;
}

void WindowChrome::makeFrameless(QWidget *window)
{
    if (!window)
        return;
    window->setWindowFlag(Qt::FramelessWindowHint, true);
    window->setAttribute(Qt::WA_StyledBackground, true);
}

QFrame *WindowChrome::createTitleBar(QWidget *window, const QString &titleText, bool allowMaximize)
{
    auto *bar = new QFrame(window);
    bar->setObjectName(QStringLiteral("windowBar"));
    bar->setFixedHeight(42);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(12, 0, 6, 0);
    layout->setSpacing(6);

    auto *appIcon = new QLabel(bar);
    appIcon->setFixedSize(24, 24);
    appIcon->setPixmap(QIcon(QStringLiteral(":/logo.svg")).pixmap(22, 22));
    auto *title = new QLabel(titleText, bar);
    title->setObjectName(QStringLiteral("windowTitle"));
    auto *minimize = new QPushButton(bar);
    auto *maximize = new QPushButton(bar);
    auto *close = new QPushButton(bar);
    minimize->setObjectName(QStringLiteral("windowMinimize"));
    maximize->setObjectName(QStringLiteral("windowMaximize"));
    close->setObjectName(QStringLiteral("windowClose"));
    minimize->setIcon(QIcon(QStringLiteral(":/icons/minimize.svg")));
    maximize->setIcon(QIcon(QStringLiteral(":/icons/maximize.svg")));
    close->setIcon(QIcon(QStringLiteral(":/icons/close.svg")));
    minimize->setToolTip(QStringLiteral("کمینه‌سازی"));
    maximize->setToolTip(QStringLiteral("بزرگ‌نمایی"));
    close->setToolTip(QStringLiteral("بستن"));
    minimize->setFixedSize(34, 28);
    maximize->setFixedSize(34, 28);
    close->setFixedSize(38, 28);
    maximize->setVisible(allowMaximize);

    QObject::connect(minimize, &QPushButton::clicked, window, &QWidget::showMinimized);
    QObject::connect(maximize, &QPushButton::clicked, window, [window] {
        window->isMaximized() ? window->showNormal() : window->showMaximized();
    });
    QObject::connect(close, &QPushButton::clicked, window, &QWidget::close);

    layout->addWidget(appIcon);
    layout->addWidget(title);
    layout->addStretch();
    layout->addWidget(minimize);
    layout->addWidget(maximize);
    layout->addWidget(close);

    auto *filter = new DragFilter(window, bar);
    filter->setAllowMaximize(allowMaximize);
    bar->installEventFilter(filter);
    title->installEventFilter(filter);
    appIcon->installEventFilter(filter);
    return bar;
}

}
