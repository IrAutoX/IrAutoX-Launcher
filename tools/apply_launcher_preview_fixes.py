from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8", newline="\n")


def replace_function(text: str, signature: str, next_signature: str, body: str) -> str:
    start = text.find(signature)
    end = text.find(next_signature, start + 1)
    if start < 0 or end < 0:
        raise RuntimeError(f"Cannot replace {signature}")
    return text[:start] + body.rstrip() + "\n\n" + text[end:]


# Keep Vazir embedded.
qrc_path = "resources/resources.qrc"
qrc = read(qrc_path)
if "<file>Vazir.ttf</file>" not in qrc:
    qrc = qrc.replace("    <file>theme.qss</file>\n", "    <file>theme.qss</file>\n    <file>Vazir.ttf</file>\n")
write(qrc_path, qrc)

# Header additions for remote assets and protocol routing.
h_path = "src/ui/MainWindow.h"
h = read(h_path)
if "class QNetworkAccessManager;" not in h:
    h = h.replace("class QMouseEvent;\n", "class QMouseEvent;\nclass QNetworkAccessManager;\n")
if "void applyGameIcon(" not in h:
    h = h.replace("    bool gameSessionActive() const;\n", "    bool gameSessionActive() const;\n    void applyGameIcon(QLabel *label, const QJsonObject &game, const QSize &size, bool circular = false);\n")
if "QNetworkAccessManager *m_assetNetwork" not in h:
    h = h.replace("    QTimer *m_announcementTimer = nullptr;\n", "    QTimer *m_announcementTimer = nullptr;\n    QNetworkAccessManager *m_assetNetwork = nullptr;\n")
write(h_path, h)

cpp_path = "src/ui/MainWindow.cpp"
cpp = read(cpp_path)
for inc in ["#include <QNetworkAccessManager>\n", "#include <QNetworkReply>\n", "#include <QNetworkRequest>\n", "#include <QPainter>\n", "#include <QPainterPath>\n", "#include <QPointer>\n"]:
    if inc not in cpp:
        cpp = cpp.replace("#include <QMouseEvent>\n", "#include <QMouseEvent>\n" + inc)

# Better base64/data-url decoder.
old = '''QPixmap decodedPixmap(const QString &base64, const QSize &size, bool crop = false)\n{\n    QPixmap pixmap;\n    pixmap.loadFromData(QByteArray::fromBase64(base64.toLatin1()));\n    if (pixmap.isNull())\n        return {};\n    return pixmap.scaled(size, crop ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio,\n                         Qt::SmoothTransformation);\n}\n'''
new = '''QPixmap decodedPixmap(const QString &value, const QSize &size, bool crop = false)\n{\n    QString payload = value.trimmed();\n    if (payload.startsWith(QStringLiteral("data:image"), Qt::CaseInsensitive)) {\n        const qsizetype comma = payload.indexOf(QLatin1Char(','));\n        if (comma >= 0) payload = payload.mid(comma + 1);\n    }\n    QPixmap pixmap;\n    pixmap.loadFromData(QByteArray::fromBase64(payload.toLatin1()));\n    if (pixmap.isNull()) return {};\n    return pixmap.scaled(size, crop ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio, Qt::SmoothTransformation);\n}\n\nQPixmap circularPixmap(const QPixmap &source, const QSize &size)\n{\n    if (source.isNull()) return {};\n    const QPixmap scaled = source.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);\n    QPixmap result(size);\n    result.fill(Qt::transparent);\n    QPainter painter(&result);\n    painter.setRenderHint(QPainter::Antialiasing, true);\n    QPainterPath path;\n    path.addEllipse(QRectF(QPointF(0, 0), QSizeF(size)));\n    painter.setClipPath(path);\n    const QPoint offset((size.width() - scaled.width()) / 2, (size.height() - scaled.height()) / 2);\n    painter.drawPixmap(offset, scaled);\n    return result;\n}\n'''
if old in cpp:
    cpp = cpp.replace(old, new, 1)

cpp = cpp.replace("    setMinimumSize(1060, 680);\n    resize(1280, 800);\n", "    setMinimumSize(820, 560);\n    resize(1180, 760);\n", 1)
if "m_assetNetwork = new QNetworkAccessManager(this);" not in cpp:
    cpp = cpp.replace("    setupTray();\n", "    setupTray();\n    m_assetNetwork = new QNetworkAccessManager(this);\n", 1)
cpp = cpp.replace('tr("Native C++  •  v%1")', 'tr("IrAutoX  •  v%1")')

# Protocol routing + session lock helpers.
marker = '''void MainWindow::setStartupGameId(qint64 gameId)\n{\n    m_startupGameId = gameId;\n}\n'''
extra = marker + '''\nvoid MainWindow::handleProtocolUrl(const QString &rawUrl)\n{\n    QUrl url(rawUrl);\n    if (!url.isValid() || url.scheme().compare(QStringLiteral("irautox"), Qt::CaseInsensitive) != 0) return;\n    QString action = url.host().toLower();\n    QString token = url.path().mid(1);\n    if (token.isEmpty()) { token = action; action = QStringLiteral("launch"); }\n    if (action != QStringLiteral("download") && action != QStringLiteral("launch") && action != QStringLiteral("game")) {\n        token = url.host(); action = QStringLiteral("launch");\n    }\n    bool ok = false;\n    qint64 id = token.toLongLong(&ok);\n    if (!ok) {\n        for (auto it = m_games.constBegin(); it != m_games.constEnd(); ++it) {\n            if (it.value().value(QStringLiteral("name")).toString().compare(token, Qt::CaseInsensitive) == 0) { id = it.key(); ok = true; break; }\n        }\n    }\n    if (!ok || id <= 0) {\n        m_startupGameName = action + QLatin1Char('|') + token;\n        return;\n    }\n    if (action == QStringLiteral("download")) showGameDetails(id);\n    else checkAndLaunch(id);\n}\n\nbool MainWindow::gameSessionActive() const\n{\n    for (QProcess *process : m_activeGameProcesses) {\n        if (process && process->state() != QProcess::NotRunning) return true;\n    }\n    return false;\n}\n\nvoid MainWindow::applyGameIcon(QLabel *label, const QJsonObject &game, const QSize &size, bool circular)\n{\n    if (!label) return;\n    QString value = game.value(QStringLiteral("icon")).toString();\n    if (value.isEmpty()) value = game.value(QStringLiteral("icon_url")).toString();\n    if (value.isEmpty()) value = game.value(QStringLiteral("logo")).toString();\n    auto apply = [label, size, circular](const QPixmap &source) {\n        if (source.isNull()) return;\n        label->setPixmap(circular ? circularPixmap(source, size) : source.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));\n    };\n    if (value.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) || value.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {\n        label->setPixmap(QIcon(QStringLiteral(":/logo.svg")).pixmap(size));\n        QNetworkRequest request(QUrl(value));\n        request.setTransferTimeout(7000);\n        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);\n        QNetworkReply *reply = m_assetNetwork->get(request);\n        QPointer<QLabel> safeLabel(label);\n        connect(reply, &QNetworkReply::finished, this, [reply, safeLabel, size, circular] {\n            const QByteArray bytes = reply->readAll();\n            const bool ok = reply->error() == QNetworkReply::NoError;\n            reply->deleteLater();\n            if (!ok || !safeLabel) return;\n            QPixmap pix; pix.loadFromData(bytes);\n            if (pix.isNull()) return;\n            safeLabel->setPixmap(circular ? circularPixmap(pix, size) : pix.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));\n        });\n        return;\n    }\n    QPixmap pix = decodedPixmap(value, size, true);\n    if (pix.isNull()) pix = QIcon(QStringLiteral(":/logo.svg")).pixmap(size);\n    apply(pix);\n}\n'''
if "void MainWindow::handleProtocolUrl" not in cpp:
    cpp = cpp.replace(marker, extra, 1)

# Small multi-card downloads page.
downloads = r'''QWidget *MainWindow::createDownloadsPage()
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
}'''
cpp = replace_function(cpp, "QWidget *MainWindow::createDownloadsPage()", "QWidget *MainWindow::createFriendsPage()", downloads)

# Remove editable server endpoint from settings.
settings = r'''QWidget *MainWindow::createSettingsPage()
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
}'''
cpp = replace_function(cpp, "QWidget *MainWindow::createSettingsPage()", "QWidget *MainWindow::createAdminPage()", settings)

# Store/library/detail/download icons always use server metadata including icon_url.
cpp = cpp.replace('''        // IRAUTOX_PATCH_V2: the store always prefers the real game icon, not a generic/banner image.\n        QPixmap gameIcon = decodedPixmap(game.value(QStringLiteral("icon")).toString(), QSize(116, 116), true);\n        if (!gameIcon.isNull()) {\n            image->setPixmap(gameIcon);\n        } else {\n            QPixmap banner = decodedPixmap(game.value(QStringLiteral("banner")).toString(), QSize(340, 132), true);\n            image->setPixmap(banner.isNull() ? QIcon(QStringLiteral(":/logo.svg")).pixmap(72, 72) : banner);\n        }\n''', '''        applyGameIcon(image, game, QSize(116, 116));\n''')
cpp = cpp.replace('''            QPixmap pix = decodedPixmap(metadata.value(QStringLiteral("icon")).toString(), QSize(68, 68), true);\n            icon->setPixmap(pix.isNull() ? QIcon(QStringLiteral(":/logo.svg")).pixmap(58, 58) : pix);\n''', '''            applyGameIcon(icon, metadata, QSize(68, 68));\n''')
cpp = cpp.replace('''    QPixmap icon = decodedPixmap(game.value(QStringLiteral("icon")).toString(), QSize(86, 86), true);\n    m_detailIcon->setPixmap(icon.isNull() ? QIcon(QStringLiteral(":/logo.svg")).pixmap(72, 72) : icon);\n''', '''    applyGameIcon(m_detailIcon, game, QSize(86, 86));\n''')

on_add = r'''void MainWindow::onDownloadAdded(const DownloadRequest &request)
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
}'''
cpp = replace_function(cpp, "void MainWindow::onDownloadAdded(const DownloadRequest &request)", "void MainWindow::onDownloadProgress(qint64 gameId", on_add)

# Game session prevents another install/launch.
needle = '''void MainWindow::installCurrentGame(bool update)\n{\n    if (m_currentGameId <= 0)\n        return;\n'''
repl = '''void MainWindow::installCurrentGame(bool update)\n{\n    if (gameSessionActive()) { QMessageBox::information(this, tr("در حال بازی"), tr("تا وقتی یک بازی در حال اجراست، نصب یا بروزرسانی بازی دیگری غیرفعال است.")); return; }\n    if (m_currentGameId <= 0)\n        return;\n'''
if needle in cpp: cpp = cpp.replace(needle, repl, 1)
needle = '''void MainWindow::checkAndLaunch(qint64 gameId)\n{\n    if (!m_library.find(gameId)) {\n'''
repl = '''void MainWindow::checkAndLaunch(qint64 gameId)\n{\n    if (gameSessionActive()) { QMessageBox::information(this, tr("در حال بازی"), tr("یک بازی هم‌اکنون در حال اجراست. ابتدا آن را ببندید.")); return; }\n    if (!m_library.find(gameId)) {\n'''
if needle in cpp: cpp = cpp.replace(needle, repl, 1)

# Circular profile image.
cpp = cpp.replace('''        if (!avatarPixmap.isNull())\n            m_profileAvatar->setPixmap(avatarPixmap);\n''', '''        if (!avatarPixmap.isNull())\n            m_profileAvatar->setPixmap(circularPixmap(avatarPixmap, QSize(104, 104)));\n''')
cpp = cpp.replace('''    m_profileAvatar->setPixmap(preview.scaled(104, 104, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));\n''', '''    m_profileAvatar->setPixmap(circularPixmap(preview, QSize(104, 104)));\n''')

# Resolve cold-start irautox://name routes once games arrive.
needle = '''        renderStore();\n        return;\n    }\n\n    if (message.contains(QStringLiteral("game"))) {\n'''
repl = '''        renderStore();\n        if (!m_startupGameName.isEmpty()) { const QString pending = m_startupGameName; m_startupGameName.clear(); QTimer::singleShot(0, this, [this, pending] { handleProtocolUrl(QStringLiteral("irautox://") + pending.section(QLatin1Char('|'), 0, 0) + QLatin1Char('/') + pending.section(QLatin1Char('|'), 1)); }); }\n        return;\n    }\n\n    if (message.contains(QStringLiteral("game"))) {\n'''
if needle in cpp: cpp = cpp.replace(needle, repl, 1)

# Save settings without editable network endpoint.
old = '''    m_settings.setServer(m_serverHost->text(), static_cast<quint16>(m_serverPort->value()));\n'''
cpp = cpp.replace(old, "")

# Cache a real .ico for desktop shortcuts when server image is embedded; URL images are fetched first.
shortcut = r'''void MainWindow::createDesktopShortcut(qint64 gameId)
{
    const InstalledGame *game = m_library.find(gameId);
    if (!game) return;
    const QJsonObject metadata = m_games.value(gameId);
    QString value = metadata.value(QStringLiteral("icon")).toString(); if (value.isEmpty()) value = metadata.value(QStringLiteral("icon_url")).toString();
    const QString iconDir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("icons")); QDir().mkpath(iconDir);
    const QString icoPath = QDir(iconDir).filePath(QStringLiteral("game-%1.ico").arg(gameId));
    auto finish = [this, gameId, game, icoPath](const QPixmap &pix) { QString iconPath; if (!pix.isNull() && pix.toImage().save(icoPath, "ICO")) iconPath = icoPath; if (iconPath.isEmpty()) { const QString exe = QDir(game->rootPath).absoluteFilePath(game->executable); if (QFileInfo::exists(exe)) iconPath = exe; } QString error; if (!ShortcutManager::createGameShortcut(gameId, game->name, iconPath, &error) && !error.isEmpty()) m_tray->showMessage(tr("شورتکات"), error, QSystemTrayIcon::Warning, 3500); };
    if (value.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) || value.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) { QNetworkRequest req(QUrl(value)); req.setTransferTimeout(7000); QNetworkReply *reply = m_assetNetwork->get(req); connect(reply, &QNetworkReply::finished, this, [reply, finish] { QByteArray bytes = reply->readAll(); const bool ok = reply->error() == QNetworkReply::NoError; reply->deleteLater(); QPixmap pix; if (ok) pix.loadFromData(bytes); finish(pix); }); return; }
    finish(decodedPixmap(value, QSize(256, 256), true));
}'''
cpp = replace_function(cpp, "void MainWindow::createDesktopShortcut(qint64 gameId)", "void MainWindow::uninstallGame(qint64 gameId)", shortcut)

write(cpp_path, cpp)

# Smaller download cards.
qss_path = "resources/theme.qss"
qss = read(qss_path)
if "IRAUTOX_V3_COMPACT" not in qss:
    qss += '''\n/* IRAUTOX_V3_COMPACT */\nQFrame#downloadItem { min-width:220px; max-width:320px; min-height:92px; max-height:116px; }\nQLabel#downloadGameIcon { min-width:38px; max-width:38px; min-height:38px; max-height:38px; }\nQWidget#downloadsPage QScrollArea > QWidget > QWidget { background:#171a21; }\nQLabel#downloadName { font-size:10pt; }\n'''
write(qss_path, qss)
