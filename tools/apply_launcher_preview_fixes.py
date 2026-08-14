from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8", newline="\n")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        if new in text:
            return text
        raise RuntimeError(f"Could not find block for {label}")
    return text.replace(old, new, 1)


def replace_function(text: str, signature: str, next_signature: str, new_body: str) -> str:
    start = text.find(signature)
    if start < 0:
        if "IRAUTOX_PATCH_V2" in text and new_body.strip() in text:
            return text
        raise RuntimeError(f"Could not find function: {signature}")
    end = text.find(next_signature, start)
    if end < 0:
        raise RuntimeError(f"Could not find next function after: {signature}")
    return text[:start] + new_body.rstrip() + "\n\n" + text[end:]


# --- main.cpp: load the real TTF bundled through Qt resources. ---
main_path = "src/main.cpp"
main = read(main_path)
main = main.replace('#include "../resources/VazirEmbedded.h"\n', "")
old_font = '''    const QByteArray vazirData = QByteArray::fromBase64(QByteArrayLiteral(IRAUTOX_VAZIR_BASE64));
    const int fontId = vazirData.isEmpty() ? -1 : QFontDatabase::addApplicationFontFromData(vazirData);
    if (fontId >= 0) {
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty())
            app.setFont(QFont(families.first(), 10));
    }
    if (fontId < 0)
        app.setFont(QFont(QStringLiteral("Tahoma"), 10));
'''
new_font = '''    // IRAUTOX_PATCH_V2: the actual Vazirmatn Regular TTF is vendored as resources/Vazir.ttf.
    const int fontId = QFontDatabase::addApplicationFont(QStringLiteral(":/Vazir.ttf"));
    if (fontId >= 0) {
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        app.setFont(QFont(families.isEmpty() ? QStringLiteral("Vazirmatn") : families.first(), 10));
    } else {
        app.setFont(QFont(QStringLiteral("Tahoma"), 10));
    }
'''
main = replace_once(main, old_font, new_font, "real Vazir font loader")
write(main_path, main)


# --- resources.qrc: embed the binary font in the executable. ---
qrc_path = "resources/resources.qrc"
qrc = read(qrc_path)
if "<file>Vazir.ttf</file>" not in qrc:
    qrc = qrc.replace("    <file>theme.qss</file>\n", "    <file>theme.qss</file>\n    <file>Vazir.ttf</file>\n")
write(qrc_path, qrc)


# --- MainWindow.h: track game processes so presence is tied to a real process lifetime. ---
h_path = "src/ui/MainWindow.h"
h = read(h_path)
if "class QProcess;" not in h:
    h = h.replace("class QProgressBar;\n", "class QProgressBar;\nclass QProcess;\n")
if "m_activeGameProcesses" not in h:
    h = h.replace("    QHash<qint64, QFrame *> m_downloadRows;\n",
                  "    QHash<qint64, QFrame *> m_downloadRows;\n    QHash<qint64, QProcess *> m_activeGameProcesses;\n")
write(h_path, h)


# --- MainWindow.cpp: session restore, icon-first store, minimal downloads, robust presence. ---
cpp_path = "src/ui/MainWindow.cpp"
cpp = read(cpp_path)

show_login = r'''void MainWindow::showLogin()
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
}'''
cpp = replace_function(cpp, "void MainWindow::showLogin()", "void MainWindow::setCurrentPage(Page page)", show_login)

old_connection = '''    if (connected && !m_user.isEmpty() && !m_sessionUsername.isEmpty()) {
        m_client.sendCommand(QStringLiteral("login"), {
            {QStringLiteral("username"), m_sessionUsername},
            {QStringLiteral("password"), m_sessionPassword}
        });
    }
'''
new_connection = '''    if (connected && m_user.isEmpty() && !m_sessionUsername.isEmpty() && !m_sessionPassword.isEmpty()) {
        m_client.sendCommand(QStringLiteral("login"), {
            {QStringLiteral("username"), m_sessionUsername},
            {QStringLiteral("password"), m_sessionPassword}
        });
    }
'''
cpp = replace_once(cpp, old_connection, new_connection, "cached login reconnect")

old_error = '''    if (status == QStringLiteral("error")) {
        if (m_loginDialog)
            m_loginDialog->setBusy(false);
        const QString error = message.value(QStringLiteral("msg")).toString(tr("خطای ناشناختهٔ سرور"));
        QMessageBox::warning(m_loginDialog && m_loginDialog->isVisible() ? static_cast<QWidget *>(m_loginDialog) : this,
                             tr("IrAutoX"), error);
        return;
    }
'''
new_error = '''    if (status == QStringLiteral("error")) {
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
        QMessageBox::warning(m_loginDialog && m_loginDialog->isVisible() ? static_cast<QWidget *>(m_loginDialog) : this,
                             tr("IrAutoX"), error);
        return;
    }
'''
cpp = replace_once(cpp, old_error, new_error, "cached login failure fallback")

old_store_image = '''        auto *image = new QLabel(card);
        image->setFixedHeight(150);
        image->setAlignment(Qt::AlignCenter);
        image->setObjectName(QStringLiteral("storeImage"));
        QPixmap banner = decodedPixmap(game.value(QStringLiteral("banner")).toString(), QSize(360, 150), true);
        if (banner.isNull())
            banner = decodedPixmap(game.value(QStringLiteral("icon")).toString(), QSize(110, 110));
        if (banner.isNull())
            image->setPixmap(QIcon(QStringLiteral(":/logo.svg")).pixmap(72, 72));
        else
            image->setPixmap(banner);
'''
new_store_image = '''        auto *image = new QLabel(card);
        image->setFixedHeight(138);
        image->setAlignment(Qt::AlignCenter);
        image->setObjectName(QStringLiteral("storeImage"));
        // IRAUTOX_PATCH_V2: the store always prefers the real game icon, not a generic/banner image.
        QPixmap gameIcon = decodedPixmap(game.value(QStringLiteral("icon")).toString(), QSize(116, 116), true);
        if (!gameIcon.isNull()) {
            image->setPixmap(gameIcon);
        } else {
            QPixmap banner = decodedPixmap(game.value(QStringLiteral("banner")).toString(), QSize(340, 132), true);
            image->setPixmap(banner.isNull() ? QIcon(QStringLiteral(":/logo.svg")).pixmap(72, 72) : banner);
        }
'''
cpp = replace_once(cpp, old_store_image, new_store_image, "icon-first store cards")

downloads_page = r'''QWidget *MainWindow::createDownloadsPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("downloadsPage"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 26, 32, 22);
    layout->setSpacing(14);

    auto *title = titleLabel(tr("دانلودها"), page);
    auto *subtitle = new QLabel(tr("نصب و بروزرسانی بازی‌ها"), page);
    subtitle->setObjectName(QStringLiteral("muted"));
    layout->addWidget(title);
    layout->addWidget(subtitle);

    auto *toolbar = new QFrame(page);
    toolbar->setObjectName(QStringLiteral("downloadToolbar"));
    auto *controls = new QHBoxLayout(toolbar);
    controls->setContentsMargins(10, 8, 10, 8);
    controls->setSpacing(8);
    auto *pause = new QPushButton(tr("توقف"), toolbar);
    auto *resume = new QPushButton(tr("ادامه"), toolbar);
    auto *cancel = new QPushButton(tr("لغو"), toolbar);
    pause->setObjectName(QStringLiteral("downloadControl"));
    resume->setObjectName(QStringLiteral("downloadControl"));
    cancel->setObjectName(QStringLiteral("downloadCancel"));
    connect(pause, &QPushButton::clicked, &m_downloadManager, &DownloadManager::pauseCurrent);
    connect(resume, &QPushButton::clicked, &m_downloadManager, &DownloadManager::resumeCurrent);
    connect(cancel, &QPushButton::clicked, &m_downloadManager, &DownloadManager::cancelCurrent);
    controls->addStretch();
    controls->addWidget(pause);
    controls->addWidget(resume);
    controls->addWidget(cancel);
    layout->addWidget(toolbar);

    auto *container = new QWidget(page);
    m_downloadsLayout = new QVBoxLayout(container);
    m_downloadsLayout->setContentsMargins(0, 4, 0, 0);
    m_downloadsLayout->setSpacing(8);
    auto *empty = new QLabel(tr("دانلود فعالی وجود ندارد."), container);
    empty->setObjectName(QStringLiteral("downloadEmpty"));
    empty->setAlignment(Qt::AlignCenter);
    m_downloadsLayout->addWidget(empty);
    m_downloadsLayout->addStretch();
    layout->addWidget(scrollAreaFor(container, page), 1);
    return page;
}'''
cpp = replace_function(cpp, "QWidget *MainWindow::createDownloadsPage()", "QWidget *MainWindow::createFriendsPage()", downloads_page)

on_download_added = r'''void MainWindow::onDownloadAdded(const DownloadRequest &request)
{
    if (m_downloadRows.isEmpty())
        clearLayout(m_downloadsLayout);

    auto *row = new QFrame;
    row->setObjectName(QStringLiteral("downloadItem"));
    row->setProperty("gameId", request.gameId);
    auto *layout = new QVBoxLayout(row);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(7);

    auto *top = new QHBoxLayout;
    top->setSpacing(10);
    auto *icon = new QLabel(row);
    icon->setObjectName(QStringLiteral("downloadGameIcon"));
    icon->setFixedSize(44, 44);
    icon->setAlignment(Qt::AlignCenter);
    const QJsonObject metadata = m_games.value(request.gameId);
    const QPixmap pix = decodedPixmap(metadata.value(QStringLiteral("icon")).toString(), QSize(42, 42), true);
    icon->setPixmap(pix.isNull() ? QIcon(QStringLiteral(":/logo.svg")).pixmap(34, 34) : pix);

    auto *name = new QLabel(request.gameName, row);
    name->setObjectName(QStringLiteral("downloadName"));
    auto *state = new QLabel(request.update ? tr("بروزرسانی") : tr("نصب"), row);
    state->setProperty("role", QStringLiteral("state"));
    state->setObjectName(QStringLiteral("downloadState"));
    top->addWidget(icon);
    top->addWidget(name);
    top->addStretch();
    top->addWidget(state);

    auto *progress = new QProgressBar(row);
    progress->setProperty("role", QStringLiteral("progress"));
    progress->setRange(0, 100);
    progress->setFixedHeight(5);

    auto *meta = new QLabel(tr("در صف"), row);
    meta->setProperty("role", QStringLiteral("meta"));
    meta->setObjectName(QStringLiteral("downloadMeta"));

    layout->addLayout(top);
    layout->addWidget(progress);
    layout->addWidget(meta);
    m_downloadsLayout->addWidget(row);
    m_downloadRows.insert(request.gameId, row);
}'''
cpp = replace_function(cpp, "void MainWindow::onDownloadAdded(const DownloadRequest &request)", "void MainWindow::onDownloadProgress(qint64 gameId", on_download_added)

launch_game = r'''void MainWindow::launchGame(qint64 gameId)
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
        QMessageBox::warning(this, tr("اجرای بازی"), tr("مسیر فایل اجرایی ناامن است."));
        return;
    }
    const QString executable = QDir(game->rootPath).absoluteFilePath(game->executable);
    const QString rootPrefix = QDir(game->rootPath).absolutePath() + QDir::separator();
    if (!QFileInfo::exists(executable) || !QDir::cleanPath(executable).startsWith(QDir::cleanPath(rootPrefix), Qt::CaseInsensitive)) {
        QMessageBox::warning(this, tr("اجرای بازی"), tr("فایل اجرایی پیدا نشد:\n%1").arg(executable));
        return;
    }
    if (!game->executableSha256.isEmpty()) {
        QFile file(executable);
        if (file.open(QIODevice::ReadOnly)) {
            QCryptographicHash hash(QCryptographicHash::Sha256);
            hash.addData(&file);
            if (QString::fromLatin1(hash.result().toHex()).compare(game->executableSha256, Qt::CaseInsensitive) != 0) {
                QMessageBox::warning(this, tr("بررسی فایل"), tr("فایل اجرایی بازی تغییر کرده است؛ بازی را دوباره نصب کنید."));
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
            QMessageBox::warning(this, tr("اجرای بازی"), process->errorString());
            process->deleteLater();
        }
    });

    // IRAUTOX_PATCH_V2: presence is now bound to the actual child process lifecycle.
    process->start();
}'''
cpp = replace_function(cpp, "void MainWindow::launchGame(qint64 gameId)", "void MainWindow::openInstallDirectory(qint64 gameId)", launch_game)

close_event = r'''void MainWindow::closeEvent(QCloseEvent *event)
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
}'''
cpp = replace_function(cpp, "void MainWindow::closeEvent(QCloseEvent *event)", "void MainWindow::changeEvent(QEvent *event)", close_event)

write(cpp_path, cpp)


# --- Theme: real Vazirmatn first, and a much calmer Downloads surface. ---
theme_path = "resources/theme.qss"
theme = read(theme_path)
theme = theme.replace('font-family: "Segoe UI", "Vazirmatn", "Tahoma";',
                      'font-family: "Vazirmatn", "Tahoma";')
marker = "/* IRAUTOX_PATCH_V2_DOWNLOADS */"
if marker not in theme:
    theme += r'''

/* IRAUTOX_PATCH_V2_DOWNLOADS */
QWidget#downloadsPage { background: #171a21; }
QFrame#downloadToolbar {
  background: #1b2028;
  border: 1px solid #292f39;
  border-radius: 9px;
}
QPushButton#downloadControl, QPushButton#downloadCancel {
  min-height: 30px;
  max-height: 30px;
  padding: 0 13px;
  border-radius: 6px;
  background: #242b34;
  border: 1px solid #313944;
  color: #d7dce3;
  font-weight: 600;
}
QPushButton#downloadControl:hover { background: #2c3540; color: #ffffff; }
QPushButton#downloadCancel { background: transparent; color: #d78383; border-color: #4b3033; }
QPushButton#downloadCancel:hover { background: #3a2226; color: #ffb0b0; }
QFrame#downloadItem {
  background: #1b2028;
  border: 1px solid #292f39;
  border-radius: 9px;
}
QLabel#downloadGameIcon {
  background: #11151b;
  border: 1px solid #2a313b;
  border-radius: 7px;
}
QLabel#downloadName { color: #f3f5f7; font-size: 11pt; font-weight: 700; }
QLabel#downloadState { color: #66c0f4; font-size: 9.5pt; font-weight: 700; }
QLabel#downloadMeta, QLabel#downloadEmpty { color: #7f8a99; font-size: 9.5pt; }
QLabel#downloadEmpty { padding: 42px; }
QWidget#downloadsPage QProgressBar {
  min-height: 5px;
  max-height: 5px;
  background: #2a3038;
  border-radius: 2px;
}
QWidget#downloadsPage QProgressBar::chunk { background: #66c0f4; border-radius: 2px; }
QLabel#storeImage {
  background: #11151b;
  border: 1px solid #242b34;
  border-radius: 8px;
  padding: 8px;
}
'''
write(theme_path, theme)

print("IrAutoX preview fixes applied.")
