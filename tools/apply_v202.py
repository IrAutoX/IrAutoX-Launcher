from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / "src/ui/MainWindow.cpp"
HDR = ROOT / "src/ui/MainWindow.h"
cpp = CPP.read_text(encoding="utf-8")
h = HDR.read_text(encoding="utf-8")


def replace_function(text, signature, next_signature, body):
    start = text.find(signature)
    end = text.find(next_signature, start + 1)
    if start < 0 or end < 0:
        raise RuntimeError(f"function boundary not found: {signature}")
    return text[:start] + body.rstrip() + "\n\n" + text[end:]

for include in [
    '#include "core/PresenceMonitor.h"\n',
    '#include "core/SdkBridge.h"\n',
    '#include "ui/StyledMessageBox.h"\n',
]:
    if include not in cpp:
        cpp = cpp.replace('#include "core/ShortcutManager.h"\n', '#include "core/ShortcutManager.h"\n' + include)

if "class PresenceMonitor;" not in h:
    h = h.replace("class LoginDialog;\n", "class LoginDialog;\nclass PresenceMonitor;\nclass SdkBridge;\n")

for declaration in [
    "    void applyGameBanner(QLabel *label, const QJsonObject &game, const QSize &size);\n",
    "    void applyGameAsset(QLabel *label, const QJsonObject &game, const QSize &size, bool banner, bool circular);\n",
    "    void precacheGameAssets(const QJsonObject &game);\n",
    "    void publishPresence(qint64 gameId, bool playing, qint64 elapsedSeconds, const QString &details, const QString &state, int partySize, int partyMax, const QString &source);\n",
]:
    if declaration not in h:
        h = h.replace("    void applyGameIcon(QLabel *label, const QJsonObject &game, const QSize &size, bool circular = false);\n",
                      "    void applyGameIcon(QLabel *label, const QJsonObject &game, const QSize &size, bool circular = false);\n" + declaration)

if "PresenceMonitor *m_presenceMonitor" not in h:
    h = h.replace("    QNetworkAccessManager *m_assetNetwork = nullptr;\n",
                  "    QNetworkAccessManager *m_assetNetwork = nullptr;\n    PresenceMonitor *m_presenceMonitor = nullptr;\n    SdkBridge *m_sdkBridge = nullptr;\n")

for old, new in [
    ("QMessageBox::warning(", "StyledMessageBox::warning("),
    ("QMessageBox::information(", "StyledMessageBox::information("),
    ("QMessageBox::critical(", "StyledMessageBox::critical("),
    ("QMessageBox::question(", "StyledMessageBox::question("),
]:
    cpp = cpp.replace(old, new)

asset_body = r'''void MainWindow::applyGameAsset(QLabel *label, const QJsonObject &game, const QSize &size, bool banner, bool circular)
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
'''

start = cpp.find("void MainWindow::applyGameIcon(QLabel *label, const QJsonObject &game, const QSize &size, bool circular)")
end = cpp.find("QFrame *MainWindow::createWindowBar(QWidget *parent)", start)
if start < 0 or end < 0:
    raise RuntimeError("asset function boundary not found")
cpp = cpp[:start] + asset_body + "\n\n" + cpp[end:]

service_marker = "    connect(&m_library, &GameLibrary::changed, this, &MainWindow::renderLibrary);\n"
if "m_presenceMonitor = new PresenceMonitor" not in cpp:
    services = service_marker + r'''    m_presenceMonitor = new PresenceMonitor(&m_library, this);
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
'''
    cpp = cpp.replace(service_marker, services, 1)

cpp = cpp.replace("    auto *menu = new QMenu(this);\n", "    auto *menu = new QMenu(this);\n    menu->setFont(qApp->font());\n", 1)

old_login = "        show();\n        raise();\n        requestInitialData();\n"
new_login = r'''        if (m_sdkBridge)
            m_sdkBridge->setIdentity(jsonId(m_user.value(QStringLiteral("id"))), m_user.value(QStringLiteral("username")).toString(m_sessionUsername));
        if (!qApp->property("irautox.startHidden").toBool()) {
            show();
            raise();
        } else {
            hide();
        }
        requestInitialData();
'''
if old_login in cpp:
    cpp = cpp.replace(old_login, new_login, 1)

old_games = '''            if (id > 0)\n                m_games.insert(id, game);\n'''
new_games = '''            if (id > 0) {\n                m_games.insert(id, game);\n                precacheGameAssets(game);\n            }\n'''
cpp = cpp.replace(old_games, new_games)
cpp = cpp.replace("        renderStore();\n        if (!m_startupGameName.isEmpty())", "        renderStore();\n        renderLibrary();\n        if (!m_startupGameName.isEmpty())", 1)

old_detail_banner = '''    QPixmap banner = decodedPixmap(game.value(QStringLiteral("banner")).toString(), QSize(1000, 230), true);\n    if (banner.isNull()) {\n        m_detailBanner->setPixmap(QPixmap{});\n        m_detailBanner->setText(tr("IrAutoX  •  %1").arg(m_detailName->text()));\n    } else {\n        m_detailBanner->setText(QString{});\n        m_detailBanner->setPixmap(banner);\n    }\n'''
new_detail_banner = '''    m_detailBanner->setText(tr("IrAutoX  •  %1").arg(m_detailName->text()));\n    applyGameBanner(m_detailBanner, game, QSize(1000, 230));\n'''
if old_detail_banner in cpp:
    cpp = cpp.replace(old_detail_banner, new_detail_banner, 1)

for old, new in [
    ('auto *refresh = new QPushButton(tr("تازه‌سازی فروشگاه"), hero);', 'auto *refresh = new QPushButton(QIcon(QStringLiteral(":/icons/refresh.svg")), tr("تازه‌سازی فروشگاه"), hero);'),
    ('auto *back = new QPushButton(tr("بازگشت به فروشگاه"), page);', 'auto *back = new QPushButton(QIcon(QStringLiteral(":/icons/back.svg")), tr("بازگشت به فروشگاه"), page);'),
    ('auto *play = new QPushButton(tr("اجرا"), row);', 'auto *play = new QPushButton(QIcon(QStringLiteral(":/icons/play.svg")), tr("اجرا"), row);'),
    ('auto *more = new QPushButton(tr("گزینه‌ها"), row);', 'auto *more = new QPushButton(QIcon(QStringLiteral(":/icons/more.svg")), tr("گزینه‌ها"), row);'),
]:
    cpp = cpp.replace(old, new)

nav_signature = "QPushButton *MainWindow::addNavigationButton(const QString &text, Page page)"
nav_next = "QWidget *MainWindow::createStorePage()"
nav_body = r'''QPushButton *MainWindow::addNavigationButton(const QString &text, Page page)
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
}'''
cpp = replace_function(cpp, nav_signature, nav_next, nav_body)

CPP.write_text(cpp, encoding="utf-8", newline="\n")
HDR.write_text(h, encoding="utf-8", newline="\n")
