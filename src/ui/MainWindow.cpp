#include "ui/MainWindow.h"

#include "core/ArchiveUtil.h"
#include "core/SecureStore.h"
#include "core/ShortcutManager.h"
#include "ui/StyledMessageBox.h"
#include "core/SdkBridge.h"
#include "core/PresenceMonitor.h"
#include "ui/LoginDialog.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLayout>
#include <QLayoutItem>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QPainterPath>
#include <QPainter>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVersionNumber>

#ifdef Q_OS_WIN
#  include <QSettings>
#endif

namespace irautox {
namespace {

void clearLayout(QLayout *layout)
{
    if (!layout)
        return;
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (item->layout())
            clearLayout(item->layout());
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
}

QPixmap decodedPixmap(const QString &value, const QSize &size, bool crop = false)
{
    QString payload = value.trimmed();
    if (payload.startsWith(QStringLiteral("data:image"), Qt::CaseInsensitive)) {
        const qsizetype comma = payload.indexOf(QLatin1Char(','));
        if (comma >= 0) payload = payload.mid(comma + 1);
    }
    QPixmap pixmap;
    pixmap.loadFromData(QByteArray::fromBase64(payload.toLatin1()));
    if (pixmap.isNull()) return {};
    return pixmap.scaled(size, crop ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QPixmap circularPixmap(const QPixmap &source, const QSize &size)
{
    if (source.isNull()) return {};
    const QPixmap scaled = source.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QPixmap result(size);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addEllipse(QRectF(QPointF(0, 0), QSizeF(size)));
    painter.setClipPath(path);
    const QPoint offset((size.width() - scaled.width()) / 2, (size.height() - scaled.height()) / 2);
    painter.drawPixmap(offset, scaled);
    return result;
}

QString humanBytes(qint64 bytes)
{
    if (bytes < 0)
        return QStringLiteral("—");
    static const QStringList units{QStringLiteral("B"), QStringLiteral("KB"), QStringLiteral("MB"), QStringLiteral("GB")};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < units.size() - 1) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', unit == 0 ? 0 : 1).arg(units.at(unit));
}

QString humanDuration(qint64 seconds)
{
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    if (hours > 0)
        return QObject::tr("%1 ساعت و %2 دقیقه").arg(hours).arg(minutes);
    return QObject::tr("%1 دقیقه").arg(minutes);
}

QLabel *titleLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("pageTitle"));
    return label;
}

QScrollArea *scrollAreaFor(QWidget *content, QWidget *parent)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    return scroll;
}

QString fileToBase64(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromLatin1(file.readAll().toBase64());
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("IrAutoX Launcher"));
    setWindowIcon(QIcon(QStringLiteral(":/logo.svg")));
    setMinimumSize(820, 560);
    resize(1180, 760);
    setupUi();
    setupTray();
    m_assetNetwork = new QNetworkAccessManager(this);

    QString libraryError;
    if (!m_library.load(&libraryError) && !libraryError.isEmpty())
        StyledMessageBox::warning(this, tr("کتابخانه"), libraryError);

    connect(&m_client, &ProtocolClient::messageReceived, this, &MainWindow::onServerMessage);
    connect(&m_client, &ProtocolClient::connectionStateChanged, this, &MainWindow::onConnectionState);
    connect(&m_client, &ProtocolClient::protocolError, this, [this](const QString &error) {
        m_connectionLabel->setToolTip(error);
    });
    connect(&m_library, &GameLibrary::changed, this, &MainWindow::renderLibrary);
    m_presenceMonitor = new PresenceMonitor(&m_library, this);
    connect(m_presenceMonitor, &PresenceMonitor::presenceChanged, this,
            [this](qint64 gameId, bool playing, qint64 elapsed, const QString &source) {
        publishPresence(gameId, playing, elapsed, QString(), QString(), 0, 0, source);
    });
    m_presenceMonitor->start();

    m_sdkBridge = new SdkBridge(this);
    if (!m_sdkBridge->listen())
        m_tray->showMessage(tr("IrAutoX SDK"), tr("پورت محلی SDK در دسترس نیست."), QSystemTrayIcon::Warning, 3500);
    connect(m_sdkBridge, &SdkBridge::presenceChanged, this,
            [this](qint64 gameId, bool playing, qint64 elapsed, const QString &details, const QString &state,
                   int partySize, int partyMax, const QString &source) {
        publishPresence(gameId, playing, elapsed, details, state, partySize, partyMax, source);
    });
    connect(&m_downloadManager, &DownloadManager::taskAdded, this, &MainWindow::onDownloadAdded);
    connect(&m_downloadManager, &DownloadManager::taskProgress, this, &MainWindow::onDownloadProgress);
    connect(&m_downloadManager, &DownloadManager::taskCompleted, this, &MainWindow::onDownloadCompleted);
    connect(&m_downloadManager, &DownloadManager::taskFailed, this, &MainWindow::onDownloadFailed);

    m_announcementTimer = new QTimer(this);
    m_announcementTimer->setInterval(15000);
    connect(m_announcementTimer, &QTimer::timeout, this, [this] {
        if (!m_user.isEmpty() && m_client.isConnected())
            m_client.sendCommand(QStringLiteral("get_announcements"));
    });
    m_announcementTimer->start();

    renderLibrary();
    showLogin();
    m_client.connectToServer(m_settings.serverHost(), m_settings.serverPort());
}

MainWindow::~MainWindow() = default;

void MainWindow::setStartupGameId(qint64 gameId)
{
    if (gameId <= 0)
        return;
    if (!m_user.isEmpty()) {
        QTimer::singleShot(0, this, [this, gameId] { checkAndLaunch(gameId); });
        return;
    }
    m_startupGameId = gameId;
}

void MainWindow::handleProtocolUrl(const QString &rawUrl)
{
    QUrl url(rawUrl);
    if (!url.isValid() || url.scheme().compare(QStringLiteral("irautox"), Qt::CaseInsensitive) != 0) return;
    QString action = url.host().toLower();
    QString token = url.path().mid(1);
    if (token.isEmpty()) { token = action; action = QStringLiteral("launch"); }
    if (action != QStringLiteral("download") && action != QStringLiteral("launch") && action != QStringLiteral("game")) {
        token = url.host(); action = QStringLiteral("launch");
    }
    bool ok = false;
    qint64 id = token.toLongLong(&ok);
    if (!ok) {
        for (auto it = m_games.constBegin(); it != m_games.constEnd(); ++it) {
            if (it.value().value(QStringLiteral("name")).toString().compare(token, Qt::CaseInsensitive) == 0) { id = it.key(); ok = true; break; }
        }
    }
    if (!ok || id <= 0) {
        m_startupGameName = action + QLatin1Char('|') + token;
        return;
    }
    if (action == QStringLiteral("download")) showGameDetails(id);
    else checkAndLaunch(id);
}

bool MainWindow::gameSessionActive() const
{
    for (QProcess *process : m_activeGameProcesses) {
        if (process && process->state() != QProcess::NotRunning) return true;
    }
    return false;
}

void MainWindow::applyGameAsset(QLabel *label, const QJsonObject &game, const QSize &size, bool banner, bool circular)
{
    QString value;
    const QStringList keys = banner
        ? QStringList{QStringLiteral("banner"), QStringLiteral("banner_url"), QStringLiteral("hero"), QStringLiteral("cover"), QStringLiteral("banner_base64")}
        : QStringList{QStringLiteral("icon"), QStringLiteral("icon_url"), QStringLiteral("logo"), QStringLiteral("icon_base64")};
    for (const QString &key : keys) {
        const QJsonValue candidate = game.value(key);
        if (candidate.isString())
            value = candidate.toString().trimmed();
        else if (candidate.isObject())
            value = candidate.toObject().value(QStringLiteral("url")).toString().trimmed();
        if (!value.isEmpty())
            break;
    }

    if (value.startsWith(QStringLiteral("//")))
        value.prepend(QStringLiteral("https:"));
    else if (value.startsWith(QLatin1Char('/')))
        value.prepend(QStringLiteral("https://irautox.ir"));
    else if (value.startsWith(QStringLiteral("icons/"), Qt::CaseInsensitive) ||
             value.startsWith(QStringLiteral("uploads/"), Qt::CaseInsensitive) ||
             value.startsWith(QStringLiteral("games/"), Qt::CaseInsensitive))
        value.prepend(QStringLiteral("https://irautox.ir/"));

    const qint64 gameId = jsonId(game.value(QStringLiteral("id")));
    const QString assetName = banner ? QStringLiteral("banner.img") : QStringLiteral("icon.img");
    const QString fallbackKey = QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
    const QString gameKey = gameId > 0 ? QString::number(gameId) : fallbackKey;
    const QString cacheDir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                                 .filePath(QStringLiteral("cache/games/%1").arg(gameKey));
    QDir().mkpath(cacheDir);
    const QString cachePath = QDir(cacheDir).filePath(assetName);

    QPointer<QLabel> safeLabel(label);
    auto display = [safeLabel, size, banner, circular](const QPixmap &source) {
        if (!safeLabel || source.isNull())
            return;
        if (circular)
            safeLabel->setPixmap(circularPixmap(source, size));
        else
            safeLabel->setPixmap(source.scaled(size, banner ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio, Qt::SmoothTransformation));
        safeLabel->setText(QString());
    };

    QPixmap cached;
    const bool hasCached = cached.load(cachePath);
    if (hasCached)
        display(cached);
    const QFileInfo cacheInfo(cachePath);
    const bool fresh = hasCached && cacheInfo.lastModified().secsTo(QDateTime::currentDateTime()) < 21600;
    if (fresh)
        return;

    const bool remote = value.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
                        value.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
    if (remote) {
        if (label && !hasCached)
            label->setPixmap(QIcon(QStringLiteral(":/logo.svg")).pixmap(size));
        QNetworkRequest request{QUrl(value)};
        request.setTransferTimeout(10000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("IrAutoX-Launcher/%1").arg(QString::fromLatin1(IRAUTOX_VERSION)));
        QNetworkReply *reply = m_assetNetwork->get(request);
        connect(reply, &QNetworkReply::finished, this, [reply, safeLabel, size, banner, circular, cachePath] {
            const QByteArray bytes = reply->readAll();
            const bool ok = reply->error() == QNetworkReply::NoError;
            reply->deleteLater();
            if (!ok || bytes.isEmpty())
                return;
            QPixmap pix;
            if (!pix.loadFromData(bytes))
                return;
            QSaveFile cache(cachePath);
            if (cache.open(QIODevice::WriteOnly)) {
                cache.write(bytes);
                cache.commit();
            }
            if (!safeLabel)
                return;
            if (circular)
                safeLabel->setPixmap(circularPixmap(pix, size));
            else
                safeLabel->setPixmap(pix.scaled(size, banner ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio, Qt::SmoothTransformation));
            safeLabel->setText(QString());
        });
        return;
    }

    if (!value.isEmpty()) {
        QString payload = value;
        if (payload.startsWith(QStringLiteral("data:image"), Qt::CaseInsensitive)) {
            const qsizetype comma = payload.indexOf(QLatin1Char(','));
            if (comma >= 0)
                payload = payload.mid(comma + 1);
        }
        const QByteArray bytes = QByteArray::fromBase64(payload.toLatin1());
        QPixmap pix;
        if (!bytes.isEmpty() && pix.loadFromData(bytes)) {
            QSaveFile cache(cachePath);
            if (cache.open(QIODevice::WriteOnly)) {
                cache.write(bytes);
                cache.commit();
            }
            display(pix);
            return;
        }
    }

    if (label && !hasCached) {
        label->setPixmap(QIcon(QStringLiteral(":/logo.svg")).pixmap(size));
        if (banner)
            label->setText(tr("IrAutoX"));
    }
}

void MainWindow::applyGameIcon(QLabel *label, const QJsonObject &game, const QSize &size, bool circular)
{
    applyGameAsset(label, game, size, false, circular);
}

void MainWindow::applyGameBanner(QLabel *label, const QJsonObject &game, const QSize &size)
{
    applyGameAsset(label, game, size, true, false);
}

void MainWindow::precacheGameAssets(const QJsonObject &game)
{
    applyGameAsset(nullptr, game, QSize(1, 1), false, false);
    applyGameAsset(nullptr, game, QSize(1, 1), true, false);
}

void MainWindow::publishPresence(qint64 gameId, bool playing, qint64 elapsedSeconds, const QString &details,
                                 const QString &state, int partySize, int partyMax, const QString &source)
{
    if (gameId <= 0 || m_user.isEmpty() || !m_client.isConnected())
        return;
    QJsonObject data{
        {QStringLiteral("user_id"), jsonId(m_user.value(QStringLiteral("id")))},
        {QStringLiteral("game_id"), gameId},
        {QStringLiteral("playing"), playing ? 1 : 0},
        {QStringLiteral("playtime"), qMax<qint64>(0, elapsedSeconds)},
        {QStringLiteral("source"), source}
    };
    if (!details.isEmpty()) data.insert(QStringLiteral("details"), details);
    if (!state.isEmpty()) data.insert(QStringLiteral("state"), state);
    if (partySize > 0) data.insert(QStringLiteral("party_size"), partySize);
    if (partyMax > 0) data.insert(QStringLiteral("party_max"), partyMax);
    m_client.sendCommand(QStringLiteral("set_status"), data);
}


QFrame *MainWindow::createWindowBar(QWidget *parent)
{
    auto *bar = new QFrame(parent);
    bar->setObjectName(QStringLiteral("windowBar"));
    bar->setFixedHeight(42);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(14, 0, 6, 0);
    layout->setSpacing(6);

    auto *title = new QLabel(QStringLiteral("IrAutoX Launcher"), bar);
    title->setObjectName(QStringLiteral("windowTitle"));
    auto *minimize = new QPushButton(QString(), bar);
    auto *maximize = new QPushButton(QString(), bar);
    auto *close = new QPushButton(QString(), bar);
    minimize->setObjectName(QStringLiteral("windowMinimize"));
    maximize->setObjectName(QStringLiteral("windowMaximize"));
    close->setObjectName(QStringLiteral("windowClose"));
    minimize->setToolTip(tr("کمینه‌سازی"));
    maximize->setToolTip(tr("بزرگ‌نمایی"));
    close->setToolTip(tr("بستن"));
    minimize->setFixedSize(34, 28);
    maximize->setFixedSize(34, 28);
    close->setFixedSize(38, 28);
    connect(minimize, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(maximize, &QPushButton::clicked, this, [this] { isMaximized() ? showNormal() : showMaximized(); });
    connect(close, &QPushButton::clicked, this, &QWidget::close);

    layout->addWidget(title);
    layout->addStretch();
    layout->addWidget(minimize);
    layout->addWidget(maximize);
    layout->addWidget(close);
    return bar;
}

void MainWindow::setupUi()
{
    auto *root = new QWidget(this);
    root->setObjectName(QStringLiteral("appRoot"));
    setCentralWidget(root);
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(createWindowBar(root));

    auto *body = new QWidget(root);
    auto *shell = new QHBoxLayout(body);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);
    shell->setDirection(QBoxLayout::RightToLeft);

    auto *sidebar = new QFrame(body);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(236);
    auto *side = new QVBoxLayout(sidebar);
    side->setContentsMargins(18, 22, 18, 20);
    side->setSpacing(7);

    auto *brandRow = new QHBoxLayout;
    auto *logo = new QLabel(sidebar);
    logo->setPixmap(QIcon(QStringLiteral(":/logo.svg")).pixmap(46, 46));
    auto *brand = new QLabel(QStringLiteral("IrAutoX"), sidebar);
    brand->setObjectName(QStringLiteral("brand"));
    brandRow->addWidget(logo);
    brandRow->addWidget(brand);
    brandRow->addStretch();
    side->addLayout(brandRow);
    side->addSpacing(24);

    m_navigation = new QButtonGroup(this);
    m_navigation->setExclusive(true);
    side->addWidget(addNavigationButton(tr("فروشگاه"), StorePage));
    side->addWidget(addNavigationButton(tr("کتابخانهٔ من"), LibraryPage));
    side->addWidget(addNavigationButton(tr("دانلودها"), DownloadsPage));
    side->addWidget(addNavigationButton(tr("دوستان"), FriendsPage));
    side->addSpacing(16);
    auto *collectionLabel = new QLabel(tr("حساب و لانچر"), sidebar);
    collectionLabel->setObjectName(QStringLiteral("muted"));
    side->addWidget(collectionLabel);
    side->addWidget(addNavigationButton(tr("پروفایل"), ProfilePage));
    side->addWidget(addNavigationButton(tr("تنظیمات"), SettingsPage));
    m_adminNavButton = addNavigationButton(tr("پنل مدیریت"), AdminPage);
    m_adminNavButton->hide();
    side->addWidget(m_adminNavButton);
    side->addStretch();

    auto *version = new QLabel(tr("IrAutoX  •  v%1").arg(QString::fromLatin1(IRAUTOX_VERSION)), sidebar);
    version->setObjectName(QStringLiteral("muted"));
    side->addWidget(version);

    auto *content = new QWidget(body);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    auto *topbar = new QFrame(content);
    topbar->setObjectName(QStringLiteral("topbar"));
    topbar->setFixedHeight(70);
    auto *top = new QHBoxLayout(topbar);
    top->setContentsMargins(26, 12, 26, 12);
    m_search = new QLineEdit(topbar);
    m_search->setPlaceholderText(tr("جست‌وجوی بازی‌ها…"));
    m_search->setMaximumWidth(480);
    m_search->setClearButtonEnabled(true);
    m_connectionLabel = new QLabel(tr("آفلاین"), topbar);
    m_connectionLabel->setObjectName(QStringLiteral("muted"));
    m_profileButton = new QPushButton(tr("حساب من"), topbar);
    connect(m_profileButton, &QPushButton::clicked, this, [this] { setCurrentPage(ProfilePage); });
    connect(m_search, &QLineEdit::textChanged, this, &MainWindow::renderStore);
    top->addWidget(m_search, 1);
    top->addStretch();
    top->addWidget(m_connectionLabel);
    top->addWidget(m_profileButton);

    m_announcementBar = new QFrame(content);
    m_announcementBar->setObjectName(QStringLiteral("announcementBar"));
    m_announcementBar->setFixedHeight(40);
    auto *announcementLayout = new QHBoxLayout(m_announcementBar);
    announcementLayout->setContentsMargins(26, 0, 26, 0);
    m_announcementLabel = new QLabel(tr("اطلاعیه‌ای وجود ندارد."), m_announcementBar);
    m_announcementLabel->setObjectName(QStringLiteral("announcementText"));
    announcementLayout->addWidget(m_announcementLabel);
    m_announcementBar->hide();

    m_pages = new QStackedWidget(content);
    m_pages->addWidget(createStorePage());
    m_pages->addWidget(createLibraryPage());
    m_pages->addWidget(createDownloadsPage());
    m_pages->addWidget(createFriendsPage());
    m_pages->addWidget(createProfilePage());
    m_pages->addWidget(createSettingsPage());
    m_pages->addWidget(createAdminPage());
    m_pages->addWidget(createDetailPage());

    auto *statusBar = new QFrame(content);
    statusBar->setObjectName(QStringLiteral("statusBar"));
    statusBar->setFixedHeight(54);
    auto *status = new QHBoxLayout(statusBar);
    status->setContentsMargins(26, 8, 26, 8);
    m_globalDownloadLabel = new QLabel(tr("دانلود فعالی نیست"), statusBar);
    m_globalDownloadSpeed = new QLabel(statusBar);
    m_globalDownloadSpeed->setObjectName(QStringLiteral("muted"));
    m_globalDownloadProgress = new QProgressBar(statusBar);
    m_globalDownloadProgress->setRange(0, 100);
    m_globalDownloadProgress->setValue(0);
    m_globalDownloadProgress->setFixedWidth(260);
    status->addWidget(m_globalDownloadLabel);
    status->addWidget(m_globalDownloadSpeed);
    status->addStretch();
    status->addWidget(m_globalDownloadProgress);

    contentLayout->addWidget(topbar);
    contentLayout->addWidget(m_announcementBar);
    contentLayout->addWidget(m_pages, 1);
    contentLayout->addWidget(statusBar);
    shell->addWidget(sidebar);
    shell->addWidget(content, 1);
    rootLayout->addWidget(body, 1);
    setCurrentPage(StorePage);
}

QPushButton *MainWindow::addNavigationButton(const QString &text, Page page)
{
    auto *button = new QPushButton(text, this);
    button->setObjectName(QStringLiteral("nav"));
    button->setCheckable(true);
    button->setProperty("page", static_cast<int>(page));
    QString iconPath = QStringLiteral(":/icons/store.svg");
    if (page == LibraryPage) iconPath = QStringLiteral(":/icons/library.svg");
    else if (page == DownloadsPage) iconPath = QStringLiteral(":/icons/download.svg");
    else if (page == FriendsPage) iconPath = QStringLiteral(":/icons/friends.svg");
    else if (page == ProfilePage) iconPath = QStringLiteral(":/icons/profile.svg");
    else if (page == SettingsPage) iconPath = QStringLiteral(":/icons/settings.svg");
    else if (page == AdminPage) iconPath = QStringLiteral(":/icons/admin.svg");
    button->setIcon(QIcon(iconPath));
    button->setIconSize(QSize(18, 18));
    m_navigation->addButton(button, static_cast<int>(page));
    connect(button, &QPushButton::clicked, this, [this, page] {
        setCurrentPage(page);
        if (page == FriendsPage && !m_user.isEmpty())
            m_client.sendCommand(QStringLiteral("get_friends"), {{QStringLiteral("user_id"), jsonId(m_user.value(QStringLiteral("id")))}});
    });
    return button;
}

QWidget *MainWindow::createStorePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 18);
    layout->setSpacing(18);

    auto *hero = new QFrame(page);
    hero->setObjectName(QStringLiteral("hero"));
    hero->setMinimumHeight(205);
    auto *heroLayout = new QVBoxLayout(hero);
    heroLayout->setContentsMargins(30, 25, 30, 25);
    auto *eyebrow = new QLabel(tr("IRAUTOX  /  DISCOVER"), hero);
    eyebrow->setObjectName(QStringLiteral("accent"));
    auto *headline = new QLabel(tr("بازی بعدی‌ات همین‌جاست."), hero);
    headline->setObjectName(QStringLiteral("heroTitle"));
    auto *copy = new QLabel(tr("بازی‌ها را پیدا کن، سریع نصب کن و همیشه به‌روز بمان."), hero);
    copy->setObjectName(QStringLiteral("muted"));
    auto *refresh = new QPushButton(QIcon(QStringLiteral(":/icons/refresh.svg")), tr("تازه‌سازی فروشگاه"), hero);
    refresh->setObjectName(QStringLiteral("primary"));
    refresh->setMaximumWidth(180);
    connect(refresh, &QPushButton::clicked, this, [this] { m_client.sendCommand(QStringLiteral("get_games")); });
    heroLayout->addWidget(eyebrow);
    heroLayout->addWidget(headline);
    heroLayout->addWidget(copy);
    heroLayout->addStretch();
    heroLayout->addWidget(refresh, 0, Qt::AlignLeft);
    layout->addWidget(hero);

    auto *section = new QLabel(tr("همهٔ بازی‌ها"), page);
    section->setObjectName(QStringLiteral("sectionTitle"));
    layout->addWidget(section);
    auto *container = new QWidget(page);
    m_storeGrid = new QGridLayout(container);
    m_storeGrid->setContentsMargins(0, 0, 0, 0);
    m_storeGrid->setHorizontalSpacing(14);
    m_storeGrid->setVerticalSpacing(14);
    layout->addWidget(scrollAreaFor(container, page), 1);
    return page;
}

QWidget *MainWindow::createLibraryPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 18);
    layout->addWidget(titleLabel(tr("کتابخانهٔ من"), page));
    auto *subtitle = new QLabel(tr("بازی‌های نصب‌شده و زمان بازی شما"), page);
    subtitle->setObjectName(QStringLiteral("muted"));
    layout->addWidget(subtitle);
    auto *container = new QWidget(page);
    m_libraryLayout = new QVBoxLayout(container);
    m_libraryLayout->setContentsMargins(0, 12, 0, 0);
    m_libraryLayout->setSpacing(12);
    layout->addWidget(scrollAreaFor(container, page), 1);
    return page;
}

QWidget *MainWindow::createDownloadsPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("downloadsPage"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(10);
    layout->addWidget(titleLabel(tr("دانلودها"), page));
    auto *subtitle = new QLabel(tr("صف دانلود و بروزرسانی بازی‌ها"), page);
    subtitle->setObjectName(QStringLiteral("muted"));
    layout->addWidget(subtitle);

    auto *toolbar = new QFrame(page);
    toolbar->setObjectName(QStringLiteral("downloadToolbar"));
    auto *controls = new QHBoxLayout(toolbar);
    controls->setContentsMargins(8, 6, 8, 6);
    auto *pause = new QPushButton(tr("توقف"), toolbar);
    auto *resume = new QPushButton(tr("ادامه"), toolbar);
    auto *cancel = new QPushButton(tr("لغو"), toolbar);
    pause->setObjectName(QStringLiteral("downloadControl")); resume->setObjectName(QStringLiteral("downloadControl")); cancel->setObjectName(QStringLiteral("downloadCancel"));
    connect(pause, &QPushButton::clicked, &m_downloadManager, &DownloadManager::pauseCurrent);
    connect(resume, &QPushButton::clicked, &m_downloadManager, &DownloadManager::resumeCurrent);
    connect(cancel, &QPushButton::clicked, &m_downloadManager, &DownloadManager::cancelCurrent);
    controls->addStretch(); controls->addWidget(pause); controls->addWidget(resume); controls->addWidget(cancel);
    layout->addWidget(toolbar);

    auto *container = new QWidget(page);
    m_downloadsGrid = new QGridLayout(container);
    m_downloadsGrid->setContentsMargins(0, 4, 0, 0);
    m_downloadsGrid->setHorizontalSpacing(10);
    m_downloadsGrid->setVerticalSpacing(10);
    auto *empty = new QLabel(tr("دانلود فعالی وجود ندارد."), container);
    empty->setObjectName(QStringLiteral("downloadEmpty")); empty->setAlignment(Qt::AlignCenter);
    m_downloadsGrid->addWidget(empty, 0, 0, 1, 3);
    layout->addWidget(scrollAreaFor(container, page), 1);
    return page;
}

QWidget *MainWindow::createFriendsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 18);
    layout->addWidget(titleLabel(tr("دوستان"), page));
    auto *addRow = new QHBoxLayout;
    m_friendUsername = new QLineEdit(page);
    m_friendUsername->setPlaceholderText(tr("نام کاربری دوست…"));
    auto *add = new QPushButton(tr("ارسال درخواست"), page);
    add->setObjectName(QStringLiteral("primary"));
    connect(add, &QPushButton::clicked, this, [this] {
        const QString username = m_friendUsername->text().trimmed();
        if (username.isEmpty() || m_user.isEmpty())
            return;
        m_client.sendCommand(QStringLiteral("add_friend"), {
            {QStringLiteral("user_id"), jsonId(m_user.value(QStringLiteral("id")))},
            {QStringLiteral("friend_username"), username}
        });
        m_friendUsername->clear();
        QTimer::singleShot(500, this, [this] {
            m_client.sendCommand(QStringLiteral("get_friends"), {{QStringLiteral("user_id"), jsonId(m_user.value(QStringLiteral("id")))}});
        });
    });
    addRow->addWidget(m_friendUsername, 1);
    addRow->addWidget(add);
    layout->addLayout(addRow);

    auto *columns = new QHBoxLayout;
    auto *friendsPanel = new QFrame(page);
    friendsPanel->setObjectName(QStringLiteral("panel"));
    auto *friendsPanelLayout = new QVBoxLayout(friendsPanel);
    auto *friendsTitle = new QLabel(tr("فهرست دوستان"), friendsPanel);
    friendsTitle->setObjectName(QStringLiteral("sectionTitle"));
    friendsPanelLayout->addWidget(friendsTitle);
    auto *friendsContainer = new QWidget(friendsPanel);
    m_friendsLayout = new QVBoxLayout(friendsContainer);
    friendsPanelLayout->addWidget(scrollAreaFor(friendsContainer, friendsPanel), 1);

    auto *requestsPanel = new QFrame(page);
    requestsPanel->setObjectName(QStringLiteral("panel"));
    auto *requestsPanelLayout = new QVBoxLayout(requestsPanel);
    auto *requestsTitle = new QLabel(tr("درخواست‌ها"), requestsPanel);
    requestsTitle->setObjectName(QStringLiteral("sectionTitle"));
    requestsPanelLayout->addWidget(requestsTitle);
    auto *requestsContainer = new QWidget(requestsPanel);
    m_requestsLayout = new QVBoxLayout(requestsContainer);
    requestsPanelLayout->addWidget(scrollAreaFor(requestsContainer, requestsPanel), 1);
    columns->addWidget(friendsPanel, 2);
    columns->addWidget(requestsPanel, 1);
    layout->addLayout(columns, 1);
    return page;
}

QWidget *MainWindow::createProfilePage()
{
    auto *page = new QWidget(this);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(28, 24, 28, 18);
    outer->addWidget(titleLabel(tr("پروفایل"), page));
    auto *panel = new QFrame(page);
    panel->setObjectName(QStringLiteral("panel"));
    panel->setMaximumWidth(720);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(32, 30, 32, 30);
    m_profileAvatar = new QLabel(panel);
    m_profileAvatar->setFixedSize(110, 110);
    m_profileAvatar->setAlignment(Qt::AlignCenter);
    m_profileAvatar->setPixmap(QIcon(QStringLiteral(":/logo.svg")).pixmap(96, 96));
    m_profileName = new QLabel(tr("مهمان"), panel);
    m_profileName->setObjectName(QStringLiteral("pageTitle"));
    m_profileRole = new QLabel(tr("آفلاین"), panel);
    m_profileRole->setObjectName(QStringLiteral("accent"));
    m_profileStats = new QLabel(tr("۰ بازی • ۰ دقیقه زمان بازی"), panel);
    m_profileStats->setObjectName(QStringLiteral("muted"));
    auto *avatar = new QPushButton(tr("تغییر تصویر پروفایل"), panel);
    auto *logoutButton = new QPushButton(tr("خروج از حساب"), panel);
    logoutButton->setObjectName(QStringLiteral("danger"));
    connect(avatar, &QPushButton::clicked, this, &MainWindow::uploadAvatar);
    connect(logoutButton, &QPushButton::clicked, this, &MainWindow::logout);
    layout->addWidget(m_profileAvatar, 0, Qt::AlignCenter);
    layout->addWidget(m_profileName, 0, Qt::AlignCenter);
    layout->addWidget(m_profileRole, 0, Qt::AlignCenter);
    layout->addWidget(m_profileStats, 0, Qt::AlignCenter);
    layout->addSpacing(18);
    layout->addWidget(avatar);
    layout->addWidget(logoutButton);
    outer->addWidget(panel, 0, Qt::AlignHCenter);
    outer->addStretch();
    return page;
}

QWidget *MainWindow::createSettingsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 18);
    layout->addWidget(titleLabel(tr("تنظیمات"), page));
    auto *panel = new QFrame(page); panel->setObjectName(QStringLiteral("panel"));
    auto *form = new QGridLayout(panel); form->setContentsMargins(24, 22, 24, 22); form->setHorizontalSpacing(14); form->setVerticalSpacing(14);
    m_downloadPath = new QLineEdit(m_settings.downloadRoot(), panel);
    auto *browse = new QPushButton(tr("انتخاب پوشه"), panel);
    connect(browse, &QPushButton::clicked, this, [this] { const QString path = QFileDialog::getExistingDirectory(this, tr("پوشهٔ نصب بازی‌ها"), m_downloadPath->text()); if (!path.isEmpty()) m_downloadPath->setText(path); });
    m_minimizeToTray = new QCheckBox(tr("با کوچک‌کردن پنجره به Tray برود"), panel); m_minimizeToTray->setChecked(m_settings.minimizeToTray());
    m_closeToTray = new QCheckBox(tr("با بستن پنجره در پس‌زمینه بماند"), panel); m_closeToTray->setChecked(m_settings.closeToTray());
    m_startWithWindows = new QCheckBox(tr("همراه ویندوز اجرا شود"), panel); m_startWithWindows->setChecked(m_settings.launchOnStartup());
    auto *save = new QPushButton(tr("ذخیرهٔ تنظیمات"), panel); save->setObjectName(QStringLiteral("primary")); connect(save, &QPushButton::clicked, this, &MainWindow::saveSettings);
    form->addWidget(new QLabel(tr("مسیر نصب پیش‌فرض"), panel), 0, 0); form->addWidget(m_downloadPath, 0, 1); form->addWidget(browse, 0, 2);
    form->addWidget(m_minimizeToTray, 1, 1, 1, 2); form->addWidget(m_closeToTray, 2, 1, 1, 2); form->addWidget(m_startWithWindows, 3, 1, 1, 2); form->addWidget(save, 4, 1, 1, 2);
    layout->addWidget(panel);
    auto *note = new QLabel(tr("سرور IrAutoX ثابت است. اطلاعات ورود ذخیره‌شده با Windows DPAPI محافظت می‌شود."), page); note->setObjectName(QStringLiteral("muted")); layout->addWidget(note); layout->addStretch();
    return page;
}

QWidget *MainWindow::createAdminPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 18);
    layout->addWidget(titleLabel(tr("پنل مدیریت"), page));
    auto *tabs = new QTabWidget(page);

    auto *announcementTab = new QWidget(tabs);
    auto *announcementLayout = new QVBoxLayout(announcementTab);
    auto *announcementTitle = new QLabel(tr("اطلاعیهٔ سراسری"), announcementTab);
    announcementTitle->setObjectName(QStringLiteral("sectionTitle"));
    m_adminAnnouncement = new QPlainTextEdit(announcementTab);
    m_adminAnnouncement->setPlaceholderText(tr("متن اطلاعیه را بنویسید…"));
    auto *publishAnnouncement = new QPushButton(tr("انتشار اطلاعیه"), announcementTab);
    publishAnnouncement->setObjectName(QStringLiteral("primary"));
    connect(publishAnnouncement, &QPushButton::clicked, this, [this] {
        const QString text = m_adminAnnouncement->toPlainText().trimmed();
        if (text.isEmpty())
            return;
        m_client.sendCommand(QStringLiteral("post_announcement"), {
            {QStringLiteral("text"), text}, {QStringLiteral("role"), QStringLiteral("admin")}
        });
        m_adminAnnouncement->clear();
    });
    announcementLayout->addWidget(announcementTitle);
    announcementLayout->addWidget(m_adminAnnouncement, 1);
    announcementLayout->addWidget(publishAnnouncement);

    auto *gamesTab = new QWidget(tabs);
    auto *gamesLayout = new QHBoxLayout(gamesTab);
    m_adminGames = new QListWidget(gamesTab);
    m_adminGames->setMinimumWidth(260);
    auto *editor = new QFrame(gamesTab);
    editor->setObjectName(QStringLiteral("panel"));
    auto *form = new QGridLayout(editor);
    form->setContentsMargins(20, 20, 20, 20);
    m_adminName = new QLineEdit(editor);
    m_adminDescription = new QPlainTextEdit(editor);
    m_adminDescription->setMinimumHeight(90);
    m_adminVersion = new QLineEdit(editor);
    m_adminUrl = new QLineEdit(editor);
    m_adminExecutable = new QLineEdit(editor);
    m_adminSha256 = new QLineEdit(editor);
    m_adminIconPath = new QLineEdit(editor);
    m_adminBannerPath = new QLineEdit(editor);
    m_adminName->setPlaceholderText(tr("نام بازی"));
    m_adminDescription->setPlaceholderText(tr("توضیحات"));
    m_adminVersion->setPlaceholderText(tr("مثلاً 1.2.0"));
    m_adminUrl->setPlaceholderText(tr("لینک مستقیم ZIP"));
    m_adminExecutable->setPlaceholderText(tr("مسیر فایل exe داخل بسته"));
    m_adminSha256->setPlaceholderText(tr("SHA-256 بسته (اختیاری)"));
    m_adminIconPath->setPlaceholderText(tr("مسیر آیکون"));
    m_adminBannerPath->setPlaceholderText(tr("مسیر بنر"));
    auto *iconBrowse = new QPushButton(tr("آیکون"), editor);
    auto *bannerBrowse = new QPushButton(tr("بنر"), editor);
    connect(iconBrowse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("آیکون بازی"), {}, tr("Images (*.png *.jpg *.jpeg *.webp)"));
        if (!path.isEmpty()) m_adminIconPath->setText(path);
    });
    connect(bannerBrowse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("بنر بازی"), {}, tr("Images (*.png *.jpg *.jpeg *.webp)"));
        if (!path.isEmpty()) m_adminBannerPath->setText(path);
    });
    form->addWidget(new QLabel(tr("نام"), editor), 0, 0); form->addWidget(m_adminName, 0, 1, 1, 2);
    form->addWidget(new QLabel(tr("توضیحات"), editor), 1, 0); form->addWidget(m_adminDescription, 1, 1, 1, 2);
    form->addWidget(new QLabel(tr("نسخه"), editor), 2, 0); form->addWidget(m_adminVersion, 2, 1, 1, 2);
    form->addWidget(new QLabel(tr("URL"), editor), 3, 0); form->addWidget(m_adminUrl, 3, 1, 1, 2);
    form->addWidget(new QLabel(tr("Executable"), editor), 4, 0); form->addWidget(m_adminExecutable, 4, 1, 1, 2);
    form->addWidget(new QLabel(tr("SHA-256"), editor), 5, 0); form->addWidget(m_adminSha256, 5, 1, 1, 2);
    form->addWidget(new QLabel(tr("تصاویر"), editor), 6, 0); form->addWidget(m_adminIconPath, 6, 1); form->addWidget(iconBrowse, 6, 2);
    form->addWidget(m_adminBannerPath, 7, 1); form->addWidget(bannerBrowse, 7, 2);
    auto *publish = new QPushButton(tr("انتشار / بروزرسانی بازی"), editor);
    publish->setObjectName(QStringLiteral("primary"));
    auto *clear = new QPushButton(tr("فرم جدید"), editor);
    connect(publish, &QPushButton::clicked, this, &MainWindow::publishAdminGame);
    connect(clear, &QPushButton::clicked, this, &MainWindow::clearAdminForm);
    form->addWidget(clear, 8, 1);
    form->addWidget(publish, 8, 2);
    gamesLayout->addWidget(m_adminGames, 1);
    gamesLayout->addWidget(editor, 3);

    connect(m_adminGames, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QJsonObject game = item->data(Qt::UserRole).toJsonObject();
        m_editingGameId = jsonId(game.value(QStringLiteral("id")));
        m_adminName->setText(game.value(QStringLiteral("name")).toString());
        m_adminDescription->setPlainText(game.value(QStringLiteral("desc")).toString());
        m_adminVersion->setText(game.value(QStringLiteral("version")).toString());
        m_adminUrl->setText(game.value(QStringLiteral("url")).toString(game.value(QStringLiteral("download_url")).toString()));
        m_adminExecutable->setText(game.value(QStringLiteral("launch")).toString(game.value(QStringLiteral("launch_exe")).toString()));
        m_adminSha256->setText(game.value(QStringLiteral("sha256")).toString());
    });

    tabs->addTab(gamesTab, tr("مدیریت بازی‌ها"));
    tabs->addTab(announcementTab, tr("اطلاعیه‌ها"));
    layout->addWidget(tabs, 1);
    return page;
}

QWidget *MainWindow::createDetailPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 20, 28, 18);
    layout->setSpacing(14);
    auto *back = new QPushButton(QIcon(QStringLiteral(":/icons/back.svg")), tr("بازگشت به فروشگاه"), page);
    back->setMaximumWidth(180);
    connect(back, &QPushButton::clicked, this, [this] { setCurrentPage(StorePage); });
    layout->addWidget(back, 0, Qt::AlignRight);

    m_detailBanner = new QLabel(page);
    m_detailBanner->setFixedHeight(230);
    m_detailBanner->setAlignment(Qt::AlignCenter);
    m_detailBanner->setObjectName(QStringLiteral("detailBanner"));
    layout->addWidget(m_detailBanner);

    auto *info = new QHBoxLayout;
    m_detailIcon = new QLabel(page);
    m_detailIcon->setFixedSize(92, 92);
    m_detailIcon->setAlignment(Qt::AlignCenter);
    m_detailIcon->setObjectName(QStringLiteral("detailIcon"));
    auto *text = new QVBoxLayout;
    m_detailName = new QLabel(tr("بازی"), page);
    m_detailName->setObjectName(QStringLiteral("pageTitle"));
    m_detailVersion = new QLabel(page);
    m_detailVersion->setObjectName(QStringLiteral("accent"));
    m_detailDescription = new QLabel(page);
    m_detailDescription->setObjectName(QStringLiteral("muted"));
    m_detailDescription->setWordWrap(true);
    text->addWidget(m_detailName);
    text->addWidget(m_detailVersion);
    text->addWidget(m_detailDescription);
    m_detailAction = new QPushButton(tr("نصب بازی"), page);
    m_detailAction->setObjectName(QStringLiteral("primary"));
    m_detailAction->setMinimumWidth(150);
    connect(m_detailAction, &QPushButton::clicked, this, [this] {
        if (m_library.find(m_currentGameId))
            checkAndLaunch(m_currentGameId);
        else
            installCurrentGame(false);
    });
    info->addWidget(m_detailIcon);
    info->addLayout(text, 1);
    info->addWidget(m_detailAction, 0, Qt::AlignBottom);
    layout->addLayout(info);

    auto *reviewHeader = new QHBoxLayout;
    auto *reviewTitle = new QLabel(tr("نظرات کاربران"), page);
    reviewTitle->setObjectName(QStringLiteral("sectionTitle"));
    m_reviewRating = new QSpinBox(page);
    m_reviewRating->setRange(1, 5);
    m_reviewRating->setValue(5);
    m_reviewRating->setSuffix(tr(" ستاره"));
    m_reviewText = new QLineEdit(page);
    m_reviewText->setPlaceholderText(tr("تجربهٔ خود را بنویسید…"));
    auto *submit = new QPushButton(tr("ثبت نظر"), page);
    connect(submit, &QPushButton::clicked, this, &MainWindow::submitReview);
    reviewHeader->addWidget(reviewTitle);
    reviewHeader->addStretch();
    reviewHeader->addWidget(m_reviewRating);
    reviewHeader->addWidget(m_reviewText, 1);
    reviewHeader->addWidget(submit);
    layout->addLayout(reviewHeader);
    auto *reviewContainer = new QWidget(page);
    m_reviewsLayout = new QVBoxLayout(reviewContainer);
    m_reviewsLayout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scrollAreaFor(reviewContainer, page), 1);
    return page;
}

void MainWindow::setupTray()
{
    m_tray = new QSystemTrayIcon(QIcon(QStringLiteral(":/logo.svg")), this);
    auto *menu = new QMenu(this);
    menu->setFont(qApp->font());
    menu->setFont(qApp->font());
    menu->setFont(qApp->font());
    menu->setFont(qApp->font());
    menu->setFont(qApp->font());
    menu->setFont(qApp->font());
    menu->setFont(qApp->font());
    menu->setFont(qApp->font());
    menu->setFont(qApp->font());
    menu->setFont(qApp->font());
    menu->setFont(qApp->font());
    menu->setFont(qApp->font());
    menu->setFont(qApp->font());
    menu->setFont(qApp->font());
    menu->setFont(qApp->font());
    menu->setFont(qApp->font());
    menu->setFont(qApp->font());
    auto *showAction = menu->addAction(tr("نمایش لانچر"));
    auto *downloadsAction = menu->addAction(tr("دانلودها"));
    menu->addSeparator();
    auto *quitAction = menu->addAction(tr("خروج کامل"));
    connect(showAction, &QAction::triggered, this, [this] { showNormal(); raise(); activateWindow(); });
    connect(downloadsAction, &QAction::triggered, this, [this] { showNormal(); setCurrentPage(DownloadsPage); });
    connect(quitAction, &QAction::triggered, this, [this] { m_quitting = true; qApp->quit(); });
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick) {
            showNormal();
            raise();
        }
    });
    m_tray->setContextMenu(menu);
    m_tray->show();
}

void MainWindow::showLogin()
{
    if (!m_loginDialog) {
        m_loginDialog = new LoginDialog;
        connect(m_loginDialog, &LoginDialog::loginRequested, this, &MainWindow::onLoginRequested);
        connect(m_loginDialog, &LoginDialog::registerRequested, this, &MainWindow::onRegisterRequested);
    }

    const auto [username, password] = SecureStore::loadCredentials();
    m_loginDialog->prefill(username, password);
    m_loginDialog->setBusy(false);

    // IRAUTOX_PATCH_V2: cached credentials stay encrypted by DPAPI and login silently.
    if (!username.isEmpty() && !password.isEmpty()) {
        m_sessionUsername = username;
        m_sessionPassword = password;
        m_rememberSession = true;
        m_loginDialog->hide();
        hide();
        if (m_client.isConnected())
            onLoginRequested(username, password, true);
        return;
    }

    m_loginDialog->show();
    m_loginDialog->raise();
    m_loginDialog->activateWindow();
    hide();
}

void MainWindow::setCurrentPage(Page page)
{
    m_pages->setCurrentIndex(static_cast<int>(page));
    if (QAbstractButton *button = m_navigation->button(static_cast<int>(page)))
        button->setChecked(true);
    if (page == AdminPage && !m_user.isEmpty()) {
        m_client.sendCommand(QStringLiteral("get_dev_games"), {
            {QStringLiteral("dev_id"), jsonId(m_user.value(QStringLiteral("id")))},
            {QStringLiteral("role"), QStringLiteral("admin")}
        });
    }
}

void MainWindow::onServerMessage(const QJsonObject &message)
{
    const QString status = message.value(QStringLiteral("status")).toString();
    if (status == QStringLiteral("error")) {
        if (m_loginDialog)
            m_loginDialog->setBusy(false);
        const QString error = message.value(QStringLiteral("msg")).toString(tr("خطای ناشناختهٔ سرور"));
        if (m_user.isEmpty() && m_rememberSession && m_loginDialog && !m_loginDialog->isVisible()) {
            SecureStore::clearCredentials();
            m_rememberSession = false;
            m_sessionUsername.clear();
            m_sessionPassword.clear();
            m_loginDialog->prefill(QString(), QString());
            m_loginDialog->show();
            m_loginDialog->raise();
            m_loginDialog->activateWindow();
            hide();
        }
        StyledMessageBox::warning(m_loginDialog && m_loginDialog->isVisible() ? static_cast<QWidget *>(m_loginDialog) : this,
                             tr("IrAutoX"), error);
        return;
    }
    if (status != QStringLiteral("ok"))
        return;

    if (message.contains(QStringLiteral("user"))) {
        m_user = message.value(QStringLiteral("user")).toObject();
        if (m_rememberSession)
            SecureStore::saveCredentials(m_sessionUsername, m_sessionPassword);
        else
            SecureStore::clearCredentials();
        if (m_loginDialog) {
            m_loginDialog->setBusy(false);
            m_loginDialog->hide();
        }
        m_profileName->setText(m_user.value(QStringLiteral("username")).toString(m_sessionUsername));
        const QString role = m_user.value(QStringLiteral("role")).toString(QStringLiteral("user"));
        m_profileRole->setText(role == QStringLiteral("admin") ? tr("مدیر IrAutoX") : tr("کاربر IrAutoX"));
        m_profileButton->setText(m_user.value(QStringLiteral("username")).toString(tr("حساب من")));
        m_adminNavButton->setVisible(role == QStringLiteral("admin"));
        const QString avatar = m_user.value(QStringLiteral("avatar")).toString();
        const QPixmap avatarPixmap = decodedPixmap(avatar, QSize(104, 104), true);
        if (!avatarPixmap.isNull())
            m_profileAvatar->setPixmap(circularPixmap(avatarPixmap, QSize(104, 104)));
        if (m_sdkBridge)
            m_sdkBridge->setIdentity(jsonId(m_user.value(QStringLiteral("id"))), m_user.value(QStringLiteral("username")).toString(m_sessionUsername));
        if (!qApp->property("irautox.startHidden").toBool()) {
            show();
            raise();
        } else {
            hide();
        }
        requestInitialData();
        if (m_startupGameId > 0) {
            const qint64 id = m_startupGameId;
            m_startupGameId = 0;
            QTimer::singleShot(350, this, [this, id] { checkAndLaunch(id); });
        }
        return;
    }

    if (message.contains(QStringLiteral("games"))) {
        m_games.clear();
        for (const QJsonValue &value : message.value(QStringLiteral("games")).toArray()) {
            const QJsonObject game = value.toObject();
            const qint64 id = jsonId(game.value(QStringLiteral("id")));
            if (id > 0) {
                m_games.insert(id, game);
                precacheGameAssets(game);
            }
        }
        renderStore();
        renderLibrary();
        if (!m_startupGameName.isEmpty()) { const QString pending = m_startupGameName; m_startupGameName.clear(); QTimer::singleShot(0, this, [this, pending] { handleProtocolUrl(QStringLiteral("irautox://") + pending.section(QLatin1Char('|'), 0, 0) + QLatin1Char('/') + pending.section(QLatin1Char('|'), 1)); }); }
        return;
    }

    if (message.contains(QStringLiteral("game"))) {
        const QJsonObject game = message.value(QStringLiteral("game")).toObject();
        const qint64 id = jsonId(game.value(QStringLiteral("id")));
        if (id > 0) {
            m_games.insert(id, game);
            precacheGameAssets(game);
        }
        updateDetail(game, message.value(QStringLiteral("reviews")).toArray());
        return;
    }

    if (message.contains(QStringLiteral("dev_games"))) {
        refreshAdminGames(message.value(QStringLiteral("dev_games")).toArray());
        return;
    }

    if (message.contains(QStringLiteral("friends"))) {
        renderFriends(message.value(QStringLiteral("friends")).toArray(),
                      message.value(QStringLiteral("requests")).toArray());
        return;
    }

    if (message.contains(QStringLiteral("announcements"))) {
        const QJsonArray announcements = message.value(QStringLiteral("announcements")).toArray();
        if (announcements.isEmpty()) {
            m_announcementBar->hide();
        } else {
            const QJsonValue first = announcements.first();
            const QString text = first.isObject()
                ? first.toObject().value(QStringLiteral("text")).toString()
                : first.toString();
            m_announcementLabel->setText(text);
            m_announcementBar->setVisible(!text.isEmpty());
        }
        return;
    }

    if (message.contains(QStringLiteral("version")) && m_pendingUpdateCheck > 0) {
        const qint64 gameId = m_pendingUpdateCheck;
        m_pendingUpdateCheck = 0;
        const InstalledGame *installed = m_library.find(gameId);
        if (!installed)
            return;
        const QString serverVersion = message.value(QStringLiteral("version")).toString(QStringLiteral("1.0"));
        if (installed->version.trimmed() != serverVersion.trimmed()) {
            const QString prompt = tr("نسخهٔ جدید %1 آماده است.\nنسخهٔ فعلی: %2\nنسخهٔ جدید: %3\nاکنون بروزرسانی شود؟")
                .arg(installed->name, installed->version, serverVersion);
            if (StyledMessageBox::question(this, tr("بروزرسانی بازی"), prompt) == QMessageBox::Yes) {
                m_currentGameId = gameId;
                m_currentGame = {
                    {QStringLiteral("id"), gameId},
                    {QStringLiteral("name"), installed->name},
                    {QStringLiteral("version"), serverVersion},
                    {QStringLiteral("url"), message.value(QStringLiteral("url"))},
                    {QStringLiteral("launch"), message.value(QStringLiteral("launch"))},
                    {QStringLiteral("sha256"), message.value(QStringLiteral("sha256"))},
                    {QStringLiteral("exe_sha256"), message.value(QStringLiteral("exe_sha256"))},
                    {QStringLiteral("launch_args"), message.value(QStringLiteral("launch_args"))}
                };
                installCurrentGame(true);
            } else {
                launchGame(gameId);
            }
        } else {
            launchGame(gameId);
        }
        return;
    }

    if (message.contains(QStringLiteral("msg"))) {
        if (m_loginDialog)
            m_loginDialog->setBusy(false);
        const QString text = message.value(QStringLiteral("msg")).toString();
        if (!text.isEmpty())
            m_tray->showMessage(QStringLiteral("IrAutoX"), text, QSystemTrayIcon::Information, 3500);
        if (m_currentGameId > 0 && text.contains(QStringLiteral("Review"), Qt::CaseInsensitive))
            showGameDetails(m_currentGameId);
    }
}

void MainWindow::onConnectionState(bool connected, const QString &detail)
{
    m_connectionLabel->setText(detail);
    m_connectionLabel->setStyleSheet(connected ? QStringLiteral("color:#66c0f4; font-weight:700;")
                                                : QStringLiteral("color:#d9a45d; font-weight:700;"));
    if (m_loginDialog)
        m_loginDialog->setConnectionStatus(connected, detail);
    if (connected && m_user.isEmpty() && !m_sessionUsername.isEmpty() && !m_sessionPassword.isEmpty()) {
        m_client.sendCommand(QStringLiteral("login"), {
            {QStringLiteral("username"), m_sessionUsername},
            {QStringLiteral("password"), m_sessionPassword}
        });
    }
}

void MainWindow::onLoginRequested(const QString &username, const QString &password, bool remember)
{
    m_sessionUsername = username;
    m_sessionPassword = password;
    m_rememberSession = remember;
    m_client.sendCommand(QStringLiteral("login"), {
        {QStringLiteral("username"), username}, {QStringLiteral("password"), password}
    });
}

void MainWindow::onRegisterRequested(const QString &username, const QString &password, bool remember)
{
    m_sessionUsername = username;
    m_sessionPassword = password;
    m_rememberSession = remember;
    m_client.sendCommand(QStringLiteral("register"), {
        {QStringLiteral("username"), username}, {QStringLiteral("password"), password}
    });
}

void MainWindow::requestInitialData()
{
    m_client.sendCommand(QStringLiteral("get_games"));
    m_client.sendCommand(QStringLiteral("get_announcements"));
    m_client.sendCommand(QStringLiteral("get_friends"), {
        {QStringLiteral("user_id"), jsonId(m_user.value(QStringLiteral("id")))}
    });
    renderLibrary();
}

void MainWindow::renderStore()
{
    clearLayout(m_storeGrid);
    const QString query = m_search->text().trimmed();
    int index = 0;
    for (auto it = m_games.constBegin(); it != m_games.constEnd(); ++it) {
        const QJsonObject game = it.value();
        const QString name = game.value(QStringLiteral("name")).toString(tr("بدون نام"));
        const QString description = game.value(QStringLiteral("desc")).toString();
        if (!query.isEmpty() && !name.contains(query, Qt::CaseInsensitive)
            && !description.contains(query, Qt::CaseInsensitive)) {
            continue;
        }

        auto *card = new QFrame;
        card->setObjectName(QStringLiteral("card"));
        card->setMinimumWidth(230);
        card->setMaximumWidth(390);
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(12, 12, 12, 12);
        auto *image = new QLabel(card);
        image->setFixedHeight(138);
        image->setAlignment(Qt::AlignCenter);
        image->setObjectName(QStringLiteral("storeImage"));
        applyGameIcon(image, game, QSize(116, 116));
        auto *nameLabel = new QLabel(name, card);
        nameLabel->setObjectName(QStringLiteral("sectionTitle"));
        auto *meta = new QLabel(tr("نسخه %1").arg(game.value(QStringLiteral("version")).toString(QStringLiteral("1.0"))), card);
        meta->setObjectName(QStringLiteral("metadata"));
        auto *open = new QPushButton(m_library.find(it.key()) ? tr("اجرا") : tr("مشاهده"), card);
        open->setObjectName(QStringLiteral("primary"));
        const qint64 id = it.key();
        connect(open, &QPushButton::clicked, this, [this, id] {
            if (m_library.find(id))
                checkAndLaunch(id);
            else
                showGameDetails(id);
        });
        layout->addWidget(image);
        layout->addWidget(nameLabel);
        layout->addWidget(meta);
        layout->addWidget(open);
        m_storeGrid->addWidget(card, index / 3, index % 3);
        ++index;
    }
    if (index == 0) {
        auto *empty = new QLabel(tr("بازی‌ای برای نمایش وجود ندارد."));
        empty->setObjectName(QStringLiteral("muted"));
        empty->setAlignment(Qt::AlignCenter);
        m_storeGrid->addWidget(empty, 0, 0, 1, 3);
    }
}

void MainWindow::renderLibrary()
{
    clearLayout(m_libraryLayout);
    qint64 totalPlaytime = 0;
    if (m_library.games().isEmpty()) {
        auto *empty = new QLabel(tr("هنوز بازی‌ای نصب نشده است."));
        empty->setObjectName(QStringLiteral("muted"));
        empty->setAlignment(Qt::AlignCenter);
        m_libraryLayout->addWidget(empty);
    } else {
        for (auto it = m_library.games().constBegin(); it != m_library.games().constEnd(); ++it) {
            const InstalledGame &game = it.value();
            totalPlaytime += game.playtimeSeconds;
            auto *row = new QFrame;
            row->setObjectName(QStringLiteral("libraryItem"));
            auto *layout = new QHBoxLayout(row);
            layout->setContentsMargins(14, 12, 14, 12);
            auto *icon = new QLabel(row);
            icon->setFixedSize(72, 72);
            icon->setAlignment(Qt::AlignCenter);
            const QJsonObject metadata = m_games.value(game.id);
            applyGameIcon(icon, metadata, QSize(68, 68));
            auto *info = new QVBoxLayout;
            auto *name = new QLabel(game.name, row);
            name->setObjectName(QStringLiteral("sectionTitle"));
            auto *meta = new QLabel(tr("نسخه %1  •  %2").arg(game.version, humanDuration(game.playtimeSeconds)), row);
            meta->setObjectName(QStringLiteral("muted"));
            info->addWidget(name);
            info->addWidget(meta);
            auto *play = new QPushButton(QIcon(QStringLiteral(":/icons/play.svg")), tr("اجرا"), row);
            play->setObjectName(QStringLiteral("primary"));
            auto *more = new QPushButton(QIcon(QStringLiteral(":/icons/more.svg")), tr("گزینه‌ها"), row);
            const qint64 id = game.id;
            connect(play, &QPushButton::clicked, this, [this, id] { checkAndLaunch(id); });
            connect(more, &QPushButton::clicked, this, [this, id, more] {
                QMenu menu;
                QAction *details = menu.addAction(tr("صفحهٔ بازی"));
                QAction *folder = menu.addAction(tr("بازکردن پوشهٔ نصب"));
                QAction *shortcut = menu.addAction(tr("ساخت شورتکات دسکتاپ"));
                QAction *favorite = menu.addAction(m_library.find(id) && m_library.find(id)->favorite
                                                    ? tr("حذف از علاقه‌مندی") : tr("افزودن به علاقه‌مندی"));
                menu.addSeparator();
                QAction *remove = menu.addAction(tr("حذف بازی"));
                QAction *selected = menu.exec(more->mapToGlobal(QPoint(0, more->height())));
                if (selected == details) showGameDetails(id);
                else if (selected == folder) openInstallDirectory(id);
                else if (selected == shortcut) createDesktopShortcut(id);
                else if (selected == favorite) {
                    const InstalledGame *installed = m_library.find(id);
                    if (installed) m_library.setFavorite(id, !installed->favorite);
                } else if (selected == remove) uninstallGame(id);
            });
            layout->addWidget(icon);
            layout->addLayout(info, 1);
            layout->addWidget(more);
            layout->addWidget(play);
            m_libraryLayout->addWidget(row);
        }
    }
    m_libraryLayout->addStretch();
    if (m_profileStats)
        m_profileStats->setText(tr("%1 بازی • %2").arg(m_library.games().size()).arg(humanDuration(totalPlaytime)));
}

void MainWindow::renderFriends(const QJsonArray &friends, const QJsonArray &requests)
{
    clearLayout(m_friendsLayout);
    clearLayout(m_requestsLayout);
    if (friends.isEmpty()) {
        auto *empty = new QLabel(tr("هنوز دوستی اضافه نشده است."));
        empty->setObjectName(QStringLiteral("muted"));
        m_friendsLayout->addWidget(empty);
    }
    for (const QJsonValue &value : friends) {
        const QJsonObject friendObject = value.toObject();
        auto *row = new QFrame;
        row->setObjectName(QStringLiteral("friendItem"));
        auto *layout = new QHBoxLayout(row);
        auto *presence = new QLabel(friendObject.value(QStringLiteral("playing")).toBool() ? QStringLiteral("●") : QStringLiteral("○"), row);
        presence->setStyleSheet(friendObject.value(QStringLiteral("playing")).toBool()
                                   ? QStringLiteral("color:#66c0f4;") : QStringLiteral("color:#7c8798;"));
        auto *name = new QLabel(friendObject.value(QStringLiteral("username")).toString(), row);
        auto *status = new QLabel(friendObject.value(QStringLiteral("playing")).toBool()
                                  ? tr("در حال بازی: %1").arg(friendObject.value(QStringLiteral("game")).toString())
                                  : tr("آنلاین"), row);
        status->setObjectName(QStringLiteral("muted"));
        layout->addWidget(presence);
        layout->addWidget(name);
        layout->addStretch();
        layout->addWidget(status);
        m_friendsLayout->addWidget(row);
    }
    m_friendsLayout->addStretch();

    if (requests.isEmpty()) {
        auto *empty = new QLabel(tr("درخواستی ندارید."));
        empty->setObjectName(QStringLiteral("muted"));
        m_requestsLayout->addWidget(empty);
    }
    for (const QJsonValue &value : requests) {
        const QJsonObject request = value.toObject();
        auto *row = new QFrame;
        row->setObjectName(QStringLiteral("friendItem"));
        auto *layout = new QVBoxLayout(row);
        auto *name = new QLabel(request.value(QStringLiteral("username")).toString(), row);
        auto *accept = new QPushButton(tr("پذیرفتن"), row);
        accept->setObjectName(QStringLiteral("primary"));
        const qint64 fromId = jsonId(request.value(QStringLiteral("id")));
        connect(accept, &QPushButton::clicked, this, [this, fromId] {
            m_client.sendCommand(QStringLiteral("accept_friend"), {
                {QStringLiteral("user_id"), jsonId(m_user.value(QStringLiteral("id")))},
                {QStringLiteral("from_id"), fromId}
            });
            QTimer::singleShot(400, this, [this] {
                m_client.sendCommand(QStringLiteral("get_friends"), {{QStringLiteral("user_id"), jsonId(m_user.value(QStringLiteral("id")))}});
            });
        });
        layout->addWidget(name);
        layout->addWidget(accept);
        m_requestsLayout->addWidget(row);
    }
    m_requestsLayout->addStretch();
}

void MainWindow::renderReviews(const QJsonArray &reviews)
{
    clearLayout(m_reviewsLayout);
    if (reviews.isEmpty()) {
        auto *empty = new QLabel(tr("هنوز نظری ثبت نشده است."));
        empty->setObjectName(QStringLiteral("muted"));
        empty->setAlignment(Qt::AlignCenter);
        m_reviewsLayout->addWidget(empty);
    }
    for (const QJsonValue &value : reviews) {
        const QJsonObject review = value.toObject();
        auto *frame = new QFrame;
        frame->setObjectName(QStringLiteral("panel"));
        auto *layout = new QVBoxLayout(frame);
        const int rating = qBound(1, review.value(QStringLiteral("rating")).toInt(5), 5);
        auto *author = new QLabel(tr("%1  •  %2/5").arg(review.value(QStringLiteral("username")).toString()).arg(rating), frame);
        author->setObjectName(QStringLiteral("accent"));
        auto *text = new QLabel(review.value(QStringLiteral("text")).toString(), frame);
        text->setWordWrap(true);
        layout->addWidget(author);
        layout->addWidget(text);
        m_reviewsLayout->addWidget(frame);
    }
    m_reviewsLayout->addStretch();
}

void MainWindow::showGameDetails(qint64 gameId)
{
    m_currentGameId = gameId;
    if (m_games.contains(gameId))
        updateDetail(m_games.value(gameId), {});
    m_client.sendCommand(QStringLiteral("get_game_details"), {{QStringLiteral("game_id"), gameId}});
    setCurrentPage(DetailPage);
}

void MainWindow::updateDetail(const QJsonObject &game, const QJsonArray &reviews)
{
    const qint64 id = jsonId(game.value(QStringLiteral("id")));
    if (id > 0)
        m_currentGameId = id;
    m_currentGame = game;
    m_detailName->setText(game.value(QStringLiteral("name")).toString(tr("بدون نام")));
    m_detailVersion->setText(tr("نسخه %1").arg(game.value(QStringLiteral("version")).toString(QStringLiteral("1.0"))));
    m_detailDescription->setText(game.value(QStringLiteral("desc")).toString(tr("توضیحی برای این بازی ثبت نشده است.")));
    applyGameIcon(m_detailIcon, game, QSize(86, 86));
    m_detailBanner->setText(tr("IrAutoX  •  %1").arg(m_detailName->text()));
    applyGameBanner(m_detailBanner, game, QSize(1000, 230));
    m_detailAction->setText(m_library.find(m_currentGameId) ? tr("بررسی و اجرا") : tr("نصب بازی"));
    renderReviews(reviews);
}

void MainWindow::installCurrentGame(bool update)
{
    if (gameSessionActive()) { StyledMessageBox::information(this, tr("در حال بازی"), tr("تا وقتی یک بازی در حال اجراست، نصب یا بروزرسانی بازی دیگری غیرفعال است.")); return; }
    if (m_currentGameId <= 0)
        return;
    const QString urlText = m_currentGame.value(QStringLiteral("url")).toString(m_currentGame.value(QStringLiteral("download_url")).toString());
    const QString executable = m_currentGame.value(QStringLiteral("launch")).toString(m_currentGame.value(QStringLiteral("launch_exe")).toString());
    if (!QUrl(urlText).isValid() || urlText.isEmpty() || executable.isEmpty()) {
        StyledMessageBox::warning(this, tr("نصب بازی"), tr("لینک دانلود یا فایل اجرای بازی در سرور کامل نیست."));
        return;
    }
    if (!ArchiveUtil::isSafeEntry(executable)) {
        StyledMessageBox::warning(this, tr("نصب بازی"), tr("مسیر فایل اجرایی اعلام‌شده ناامن است."));
        return;
    }

    const QString name = m_currentGame.value(QStringLiteral("name")).toString(tr("Game"));
    const InstalledGame *installed = m_library.find(m_currentGameId);
    const QString target = update && installed
        ? installed->rootPath
        : QDir(m_settings.downloadRoot()).filePath(safeFolderName(name, m_currentGameId));
    DownloadRequest request;
    request.gameId = m_currentGameId;
    request.gameName = name;
    request.url = QUrl(urlText);
    request.targetDirectory = target;
    request.executable = executable;
    request.version = m_currentGame.value(QStringLiteral("version")).toString(QStringLiteral("1.0"));
    request.archiveSha256 = m_currentGame.value(QStringLiteral("sha256")).toString();
    request.executableSha256 = m_currentGame.value(QStringLiteral("exe_sha256")).toString();
    request.update = update;
    m_downloadManager.enqueue(request);
    setCurrentPage(DownloadsPage);
}

void MainWindow::onDownloadAdded(const DownloadRequest &request)
{
    if (m_downloadRows.isEmpty()) clearLayout(m_downloadsGrid);
    auto *row = new QFrame; row->setObjectName(QStringLiteral("downloadItem")); row->setProperty("gameId", request.gameId); row->setMinimumWidth(220); row->setMaximumWidth(320);
    auto *layout = new QVBoxLayout(row); layout->setContentsMargins(11, 10, 11, 10); layout->setSpacing(6);
    auto *top = new QHBoxLayout; auto *icon = new QLabel(row); icon->setObjectName(QStringLiteral("downloadGameIcon")); icon->setFixedSize(38, 38); icon->setAlignment(Qt::AlignCenter);
    applyGameIcon(icon, m_games.value(request.gameId), QSize(36, 36));
    auto *name = new QLabel(request.gameName, row); name->setObjectName(QStringLiteral("downloadName")); name->setWordWrap(true);
    auto *state = new QLabel(request.update ? tr("بروزرسانی") : tr("نصب"), row); state->setProperty("role", QStringLiteral("state")); state->setObjectName(QStringLiteral("downloadState"));
    top->addWidget(icon); top->addWidget(name, 1); top->addWidget(state);
    auto *progress = new QProgressBar(row); progress->setProperty("role", QStringLiteral("progress")); progress->setRange(0, 100); progress->setFixedHeight(5);
    auto *meta = new QLabel(tr("در صف"), row); meta->setProperty("role", QStringLiteral("meta")); meta->setObjectName(QStringLiteral("downloadMeta"));
    layout->addLayout(top); layout->addWidget(progress); layout->addWidget(meta);
    const int index = m_downloadRows.size(); m_downloadsGrid->addWidget(row, index / 3, index % 3); m_downloadRows.insert(request.gameId, row);
}

void MainWindow::onDownloadProgress(qint64 gameId, DownloadManager::State state, int percent,
                                    qint64 received, qint64 total, double speed, const QString &detail)
{
    QFrame *row = m_downloadRows.value(gameId);
    if (row) {
        const QList<QLabel *> labels = row->findChildren<QLabel *>();
        for (QLabel *label : labels) {
            if (label->property("role").toString() == QStringLiteral("state"))
                label->setText(detail);
            else if (label->property("role").toString() == QStringLiteral("meta")) {
                QString text = total > 0 ? tr("%1 از %2").arg(humanBytes(received), humanBytes(total)) : humanBytes(received);
                if (speed > 0)
                    text += tr("  •  %1/s").arg(humanBytes(static_cast<qint64>(speed)));
                label->setText(text);
            }
        }
        if (QProgressBar *progress = row->findChild<QProgressBar *>())
            progress->setValue(percent);
    }
    m_globalDownloadProgress->setValue(percent);
    m_globalDownloadLabel->setText(detail);
    m_globalDownloadSpeed->setText(speed > 0 ? humanBytes(static_cast<qint64>(speed)) + QStringLiteral("/s") : QString());
    if (state == DownloadManager::State::Completed || state == DownloadManager::State::Cancelled
        || state == DownloadManager::State::Failed) {
        m_globalDownloadSpeed->clear();
    }
}

void MainWindow::onDownloadCompleted(const DownloadRequest &request)
{
    InstalledGame game;
    game.id = request.gameId;
    game.name = request.gameName;
    game.version = request.version;
    game.rootPath = request.targetDirectory;
    game.executable = request.executable;
    game.archiveSha256 = request.archiveSha256;
    game.executableSha256 = request.executableSha256;
    if (m_currentGameId == request.gameId)
        game.launchArguments = m_currentGame.value(QStringLiteral("launch_args")).toString();
    if (const InstalledGame *previous = m_library.find(request.gameId)) {
        game.playtimeSeconds = previous->playtimeSeconds;
        game.favorite = previous->favorite;
        game.lastPlayedAt = previous->lastPlayedAt;
        if (game.launchArguments.isEmpty())
            game.launchArguments = previous->launchArguments;
    }
    m_library.upsert(game);
    writeInstallMarker(request);
    createDesktopShortcut(request.gameId);
    m_globalDownloadLabel->setText(tr("%1 آمادهٔ اجراست").arg(request.gameName));
    m_tray->showMessage(tr("نصب کامل شد"), tr("%1 با موفقیت نصب شد.").arg(request.gameName),
                        QSystemTrayIcon::Information, 4000);
}

void MainWindow::onDownloadFailed(qint64 gameId, const QString &error)
{
    Q_UNUSED(gameId)
    StyledMessageBox::critical(this, tr("دانلود / نصب ناموفق"), error);
    m_globalDownloadLabel->setText(tr("عملیات ناموفق بود"));
}

void MainWindow::checkAndLaunch(qint64 gameId)
{
    if (gameSessionActive()) { StyledMessageBox::information(this, tr("در حال بازی"), tr("یک بازی هم‌اکنون در حال اجراست. ابتدا آن را ببندید.")); return; }
    if (!m_library.find(gameId)) {
        showGameDetails(gameId);
        return;
    }
    m_pendingUpdateCheck = gameId;
    m_client.sendCommand(QStringLiteral("check_update"), {{QStringLiteral("game_id"), gameId}});
    QTimer::singleShot(7000, this, [this, gameId] {
        if (m_pendingUpdateCheck == gameId) {
            m_pendingUpdateCheck = 0;
            launchGame(gameId);
        }
    });
}

void MainWindow::launchGame(qint64 gameId)
{
    const InstalledGame *game = m_library.find(gameId);
    if (!game)
        return;

    if (QProcess *existing = m_activeGameProcesses.value(gameId, nullptr)) {
        if (existing->state() != QProcess::NotRunning) {
            m_tray->showMessage(tr("بازی در حال اجراست"), tr("%1 همین حالا در حال اجراست.").arg(game->name),
                                QSystemTrayIcon::Information, 2500);
            return;
        }
        m_activeGameProcesses.remove(gameId);
    }

    if (!ArchiveUtil::isSafeEntry(game->executable)) {
        StyledMessageBox::warning(this, tr("اجرای بازی"), tr("مسیر فایل اجرایی ناامن است."));
        return;
    }
    const QString executable = QDir(game->rootPath).absoluteFilePath(game->executable);
    const QString rootPrefix = QDir(game->rootPath).absolutePath() + QDir::separator();
    if (!QFileInfo::exists(executable) || !QDir::cleanPath(executable).startsWith(QDir::cleanPath(rootPrefix), Qt::CaseInsensitive)) {
        StyledMessageBox::warning(this, tr("اجرای بازی"), tr("فایل اجرایی پیدا نشد:\n%1").arg(executable));
        return;
    }
    if (!game->executableSha256.isEmpty()) {
        QFile file(executable);
        if (file.open(QIODevice::ReadOnly)) {
            QCryptographicHash hash(QCryptographicHash::Sha256);
            hash.addData(&file);
            if (QString::fromLatin1(hash.result().toHex()).compare(game->executableSha256, Qt::CaseInsensitive) != 0) {
                StyledMessageBox::warning(this, tr("بررسی فایل"), tr("فایل اجرایی بازی تغییر کرده است؛ بازی را دوباره نصب کنید."));
                return;
            }
        }
    }

    auto *process = new QProcess(this);
    process->setProgram(executable);
    process->setWorkingDirectory(game->rootPath);
    process->setArguments(QProcess::splitCommand(game->launchArguments));
    process->setProperty("gameId", gameId);
    process->setProperty("startedAt", QDateTime::currentSecsSinceEpoch());
    m_activeGameProcesses.insert(gameId, process);

    auto sendPresence = [this, gameId](bool playing, qint64 playtime) {
        if (m_user.isEmpty() || !m_client.isConnected())
            return;
        m_client.sendCommand(QStringLiteral("set_status"), {
            {QStringLiteral("user_id"), jsonId(m_user.value(QStringLiteral("id")))},
            {QStringLiteral("game_id"), gameId},
            {QStringLiteral("playing"), playing ? 1 : 0},
            {QStringLiteral("playtime"), playtime}
        });
    };

    auto *heartbeat = new QTimer(process);
    heartbeat->setInterval(25000);
    connect(heartbeat, &QTimer::timeout, this, [process, sendPresence] {
        if (process->state() == QProcess::Running) {
            const qint64 elapsed = qMax<qint64>(0, QDateTime::currentSecsSinceEpoch()
                                                    - process->property("startedAt").toLongLong());
            sendPresence(true, elapsed);
        }
    });

    connect(process, &QProcess::started, this, [heartbeat, sendPresence] {
        sendPresence(true, 0);
        heartbeat->start();
    });

    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, heartbeat, gameId, sendPresence](int, QProcess::ExitStatus) {
        heartbeat->stop();
        const qint64 elapsed = qMax<qint64>(0, QDateTime::currentSecsSinceEpoch()
                                                - process->property("startedAt").toLongLong());
        m_library.updatePlaytime(gameId, elapsed);
        sendPresence(false, elapsed);
        m_activeGameProcesses.remove(gameId);
        process->deleteLater();
    });

    connect(process, &QProcess::errorOccurred, this,
            [this, process, gameId, sendPresence](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            sendPresence(false, 0);
            m_activeGameProcesses.remove(gameId);
            StyledMessageBox::warning(this, tr("اجرای بازی"), process->errorString());
            process->deleteLater();
        }
    });

    // IRAUTOX_PATCH_V2: presence is now bound to the actual child process lifecycle.
    process->start();
}

void MainWindow::openInstallDirectory(qint64 gameId)
{
    const InstalledGame *game = m_library.find(gameId);
    if (game)
        QDesktopServices::openUrl(QUrl::fromLocalFile(game->rootPath));
}

void MainWindow::createDesktopShortcut(qint64 gameId)
{
    const InstalledGame *game = m_library.find(gameId);
    if (!game) return;
    const QJsonObject metadata = m_games.value(gameId);
    QString value = metadata.value(QStringLiteral("icon")).toString(); if (value.isEmpty()) value = metadata.value(QStringLiteral("icon_url")).toString();
    const QString iconDir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("icons")); QDir().mkpath(iconDir);
    const QString icoPath = QDir(iconDir).filePath(QStringLiteral("game-%1.ico").arg(gameId));
    auto finish = [this, gameId, game, icoPath](const QPixmap &pix) { QString iconPath; if (!pix.isNull() && pix.toImage().save(icoPath, "ICO")) iconPath = icoPath; if (iconPath.isEmpty()) { const QString exe = QDir(game->rootPath).absoluteFilePath(game->executable); if (QFileInfo::exists(exe)) iconPath = exe; } QString error; if (!ShortcutManager::createGameShortcut(gameId, game->name, iconPath, &error) && !error.isEmpty()) m_tray->showMessage(tr("شورتکات"), error, QSystemTrayIcon::Warning, 3500); };
    if (value.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) || value.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) { QNetworkRequest req{QUrl(value)}; req.setTransferTimeout(7000); QNetworkReply *reply = m_assetNetwork->get(req); connect(reply, &QNetworkReply::finished, this, [reply, finish] { QByteArray bytes = reply->readAll(); const bool ok = reply->error() == QNetworkReply::NoError; reply->deleteLater(); QPixmap pix; if (ok) pix.loadFromData(bytes); finish(pix); }); return; }
    finish(decodedPixmap(value, QSize(256, 256), true));
}

void MainWindow::uninstallGame(qint64 gameId)
{
    const InstalledGame *gamePointer = m_library.find(gameId);
    if (!gamePointer)
        return;
    const InstalledGame game = *gamePointer;
    if (StyledMessageBox::question(this, tr("حذف بازی"), tr("%1 و فایل‌های نصب‌شده حذف شوند؟").arg(game.name)) != QMessageBox::Yes)
        return;
    if (!hasValidInstallMarker(game)) {
        StyledMessageBox::information(this, tr("ایمنی حذف"),
            tr("این نصب نشانگر معتبر IrAutoX ندارد؛ فقط از کتابخانه حذف می‌شود و پوشه دست‌نخورده می‌ماند."));
        m_library.forget(gameId);
        return;
    }
    const QString absolute = QFileInfo(game.rootPath).absoluteFilePath();
    if (absolute.isEmpty() || QDir(absolute).isRoot() || !QDir(absolute).removeRecursively()) {
        StyledMessageBox::warning(this, tr("حذف بازی"), tr("حذف پوشه ناموفق بود. بازی را ببندید و دوباره امتحان کنید."));
        return;
    }
    QFile::remove(ShortcutManager::shortcutPath(gameId, game.name));
    m_library.forget(gameId);
}

void MainWindow::saveSettings()
{
    if (m_downloadPath->text().trimmed().isEmpty())
        return;
    m_settings.setDownloadRoot(m_downloadPath->text());
    m_settings.setMinimizeToTray(m_minimizeToTray->isChecked());
    m_settings.setCloseToTray(m_closeToTray->isChecked());
    m_settings.setLaunchOnStartup(m_startWithWindows->isChecked());
    applyStartupSetting(m_startWithWindows->isChecked());
    QDir().mkpath(m_settings.downloadRoot());
    m_client.connectToServer(m_settings.serverHost(), m_settings.serverPort());
    StyledMessageBox::information(this, tr("تنظیمات"), tr("تنظیمات ذخیره شد."));
}

void MainWindow::applyStartupSetting(bool enabled)
{
#ifdef Q_OS_WIN
    QSettings startup(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                      QSettings::NativeFormat);
    if (enabled)
        startup.setValue(QStringLiteral("IrAutoXLauncher"), QStringLiteral("\"") + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + QStringLiteral("\" --background"));
    else
        startup.remove(QStringLiteral("IrAutoXLauncher"));
#else
    Q_UNUSED(enabled)
#endif
}

void MainWindow::logout()
{
    SecureStore::clearCredentials();
    m_user = {};
    m_sessionPassword.clear();
    m_sessionUsername.clear();
    m_adminNavButton->hide();
    showLogin();
}

void MainWindow::uploadAvatar()
{
    if (m_user.isEmpty())
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("تصویر پروفایل"), {}, tr("Images (*.png *.jpg *.jpeg *.webp)"));
    if (path.isEmpty())
        return;
    QFileInfo info(path);
    if (info.size() > 2 * 1024 * 1024) {
        StyledMessageBox::warning(this, tr("تصویر پروفایل"), tr("حجم تصویر باید کمتر از ۲ مگابایت باشد."));
        return;
    }
    const QString data = fileToBase64(path);
    m_client.sendCommand(QStringLiteral("upload_avatar"), {
        {QStringLiteral("user_id"), jsonId(m_user.value(QStringLiteral("id")))},
        {QStringLiteral("image"), data}
    });
    QPixmap preview(path);
    m_profileAvatar->setPixmap(circularPixmap(preview, QSize(104, 104)));
}

void MainWindow::submitReview()
{
    const QString text = m_reviewText->text().trimmed();
    if (text.isEmpty() || m_user.isEmpty() || m_currentGameId <= 0)
        return;
    m_client.sendCommand(QStringLiteral("submit_review"), {
        {QStringLiteral("game_id"), m_currentGameId},
        {QStringLiteral("user_id"), jsonId(m_user.value(QStringLiteral("id")))},
        {QStringLiteral("username"), m_user.value(QStringLiteral("username"))},
        {QStringLiteral("rating"), m_reviewRating->value()},
        {QStringLiteral("text"), text}
    });
    m_reviewText->clear();
    QTimer::singleShot(600, this, [this] { showGameDetails(m_currentGameId); });
}

void MainWindow::refreshAdminGames(const QJsonArray &games)
{
    m_adminGames->clear();
    for (const QJsonValue &value : games) {
        const QJsonObject game = value.toObject();
        auto *item = new QListWidgetItem(tr("%1  •  v%2").arg(game.value(QStringLiteral("name")).toString(),
                                                               game.value(QStringLiteral("version")).toString()));
        item->setData(Qt::UserRole, game);
        m_adminGames->addItem(item);
    }
}

void MainWindow::publishAdminGame()
{
    if (m_user.value(QStringLiteral("role")).toString() != QStringLiteral("admin"))
        return;
    const QString name = m_adminName->text().trimmed();
    const QString version = m_adminVersion->text().trimmed();
    const QString url = m_adminUrl->text().trimmed();
    const QString executable = m_adminExecutable->text().trimmed();
    if (name.isEmpty() || version.isEmpty() || !QUrl(url).isValid() || !ArchiveUtil::isSafeEntry(executable)) {
        StyledMessageBox::warning(this, tr("انتشار بازی"), tr("نام، نسخه، لینک مستقیم و مسیر اجرای معتبر لازم است."));
        return;
    }
    const QString sha = m_adminSha256->text().trimmed();
    if (!sha.isEmpty() && !QRegularExpression(QStringLiteral("^[0-9A-Fa-f]{64}$")).match(sha).hasMatch()) {
        StyledMessageBox::warning(this, tr("انتشار بازی"), tr("SHA-256 باید دقیقاً ۶۴ نویسهٔ هگزادسیمال باشد."));
        return;
    }
    QJsonObject data{
        {QStringLiteral("name"), name},
        {QStringLiteral("desc"), m_adminDescription->toPlainText().trimmed()},
        {QStringLiteral("version"), version},
        {QStringLiteral("launch_exe"), executable},
        {QStringLiteral("download_url"), url},
        {QStringLiteral("sha256"), sha},
        {QStringLiteral("role"), QStringLiteral("admin")},
        {QStringLiteral("dev_id"), jsonId(m_user.value(QStringLiteral("id")))}
    };
    if (m_editingGameId > 0)
        data.insert(QStringLiteral("game_id"), m_editingGameId);
    const QString icon = fileToBase64(m_adminIconPath->text());
    const QString banner = fileToBase64(m_adminBannerPath->text());
    if (!icon.isEmpty()) data.insert(QStringLiteral("icon"), icon);
    if (!banner.isEmpty()) data.insert(QStringLiteral("banner"), banner);
    m_client.sendCommand(QStringLiteral("publish_game"), data);
    QTimer::singleShot(700, this, [this] {
        m_client.sendCommand(QStringLiteral("get_dev_games"), {
            {QStringLiteral("dev_id"), jsonId(m_user.value(QStringLiteral("id")))},
            {QStringLiteral("role"), QStringLiteral("admin")}
        });
        m_client.sendCommand(QStringLiteral("get_games"));
    });
}

void MainWindow::clearAdminForm()
{
    m_editingGameId = 0;
    m_adminGames->clearSelection();
    m_adminName->clear();
    m_adminDescription->clear();
    m_adminVersion->clear();
    m_adminUrl->clear();
    m_adminExecutable->clear();
    m_adminSha256->clear();
    m_adminIconPath->clear();
    m_adminBannerPath->clear();
}

void MainWindow::writeInstallMarker(const DownloadRequest &request) const
{
    QSaveFile marker(QDir(request.targetDirectory).filePath(QStringLiteral(".irautox-install.json")));
    if (!marker.open(QIODevice::WriteOnly))
        return;
    const QJsonObject object{
        {QStringLiteral("gameId"), request.gameId},
        {QStringLiteral("name"), request.gameName},
        {QStringLiteral("installedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("launcherVersion"), QString::fromLatin1(IRAUTOX_VERSION)}
    };
    marker.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    marker.commit();
}

bool MainWindow::hasValidInstallMarker(const InstalledGame &game) const
{
    QFile marker(QDir(game.rootPath).filePath(QStringLiteral(".irautox-install.json")));
    if (!marker.open(QIODevice::ReadOnly))
        return false;
    return QJsonDocument::fromJson(marker.readAll()).object().value(QStringLiteral("gameId")).toInteger() == game.id;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_quitting && !m_activeGameProcesses.isEmpty() && m_tray->isVisible()) {
        hide();
        m_tray->showMessage(QStringLiteral("IrAutoX"),
                            tr("لانچر برای ثبت دقیق وضعیت بازی در پس‌زمینه فعال می‌ماند."),
                            QSystemTrayIcon::Information, 3000);
        event->ignore();
        return;
    }
    if (!m_quitting && m_settings.closeToTray() && m_tray->isVisible()) {
        hide();
        m_tray->showMessage(QStringLiteral("IrAutoX"), tr("لانچر در پس‌زمینه فعال ماند."),
                            QSystemTrayIcon::Information, 2500);
        event->ignore();
        return;
    }
    m_quitting = true;
    event->accept();
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && isMinimized()
        && m_settings.minimizeToTray() && m_tray->isVisible()) {
        QTimer::singleShot(0, this, &QWidget::hide);
    }
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && event->position().y() <= 42.0) {
        m_draggingWindow = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QMainWindow::mousePressEvent(event);
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_draggingWindow && !isMaximized()) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }
    QMainWindow::mouseMoveEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    m_draggingWindow = false;
    QMainWindow::mouseReleaseEvent(event);
}

qint64 MainWindow::jsonId(const QJsonValue &value)
{
    if (value.isString())
        return value.toString().toLongLong();
    return value.toInteger();
}

QString MainWindow::safeFolderName(const QString &name, qint64 id)
{
    QString safe = name.trimmed();
    safe.replace(QRegularExpression(QStringLiteral(R"([<>:\"/\\|?*]+)")), QStringLiteral("-"));
    safe.replace(QRegularExpression(QStringLiteral("[. ]+$")), QString());
    if (safe.isEmpty())
        safe = QStringLiteral("Game");
    return QStringLiteral("%1-%2").arg(safe.left(64)).arg(id);
}

} // namespace irautox
