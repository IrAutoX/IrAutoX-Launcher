from pathlib import Path

path = Path("src/ui/MainWindow.cpp")
text = path.read_text(encoding="utf-8")

old_target = '''    const QString name = m_currentGame.value(QStringLiteral("name")).toString(tr("Game"));
    const InstalledGame *installed = m_library.find(m_currentGameId);
    const QString target = update && installed
        ? installed->rootPath
        : QDir(m_settings.downloadRoot()).filePath(safeFolderName(name, m_currentGameId));
'''
new_target = '''    const QString name = m_currentGame.value(QStringLiteral("name")).toString(tr("Game"));
    const InstalledGame *installed = m_library.find(m_currentGameId);
    QString target;
    if (update && installed) {
        target = installed->rootPath;
    } else {
        const QString base = QFileDialog::getExistingDirectory(
            this,
            tr("انتخاب پوشه نصب %1").arg(name),
            m_settings.downloadRoot(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (base.isEmpty())
            return;
        const QFileInfo baseInfo(base);
        if (!baseInfo.exists() || !baseInfo.isDir() || QDir(base).isRoot()) {
            StyledMessageBox::warning(this, tr("مسیر نصب"), tr("پوشه انتخاب‌شده برای نصب مناسب نیست."));
            return;
        }
        target = QDir(base).filePath(safeFolderName(name, m_currentGameId));
        const QString appDir = QDir::cleanPath(QCoreApplication::applicationDirPath());
        const QString cleanTarget = QDir::cleanPath(QFileInfo(target).absoluteFilePath());
        if (cleanTarget.compare(appDir, Qt::CaseInsensitive) == 0
            || cleanTarget.startsWith(appDir + QDir::separator(), Qt::CaseInsensitive)) {
            StyledMessageBox::warning(this, tr("مسیر نصب"), tr("بازی را داخل پوشه خود Launcher نصب نکنید."));
            return;
        }
    }
'''
if old_target in text:
    text = text.replace(old_target, new_target, 1)
elif 'tr("انتخاب پوشه نصب %1")' not in text:
    raise SystemExit("install target block not found")

old_cache = '''    QPixmap cached;
    const bool hasCached = cached.load(cachePath);
    if (hasCached)
        display(cached);
    const QFileInfo cacheInfo(cachePath);
    const bool fresh = hasCached && cacheInfo.lastModified().secsTo(QDateTime::currentDateTime()) < 21600;
    if (fresh)
        return;
'''
new_cache = '''    QPixmap cached;
    const bool hasCached = cached.load(cachePath);
    if (hasCached)
        display(cached);

    const QString refreshKey = gameKey + QLatin1Char(':') + assetName;
    const bool sourceAvailable = !value.isEmpty();
    const bool refreshThisSession = sourceAvailable && !m_assetRefreshStarted.contains(refreshKey);
    if (refreshThisSession)
        m_assetRefreshStarted.insert(refreshKey);
    if (hasCached && !refreshThisSession)
        return;
'''
if old_cache in text:
    text = text.replace(old_cache, new_cache, 1)
elif 'refreshThisSession' not in text:
    raise SystemExit("asset cache block not found")

old_games_tail = '''        renderStore();
        renderLibrary();
        if (!m_startupGameName.isEmpty()) { const QString pending = m_startupGameName; m_startupGameName.clear(); QTimer::singleShot(0, this, [this, pending] { handleProtocolUrl(QStringLiteral("irautox://") + pending.section(QLatin1Char('|'), 0, 0) + QLatin1Char('/') + pending.section(QLatin1Char('|'), 1)); }); }
        return;
    }
'''
new_games_tail = '''        renderStore();
        renderLibrary();
        m_requestedDetailAssets.clear();
        for (auto it = m_games.constBegin(); it != m_games.constEnd(); ++it) {
            if (it.key() <= 0 || m_requestedDetailAssets.contains(it.key()))
                continue;
            m_requestedDetailAssets.insert(it.key());
            m_client.sendCommand(QStringLiteral("get_game_details"), {{QStringLiteral("game_id"), it.key()}});
        }
        if (!m_startupGameName.isEmpty()) { const QString pending = m_startupGameName; m_startupGameName.clear(); QTimer::singleShot(0, this, [this, pending] { handleProtocolUrl(QStringLiteral("irautox://") + pending.section(QLatin1Char('|'), 0, 0) + QLatin1Char('/') + pending.section(QLatin1Char('|'), 1)); }); }
        return;
    }
'''
if old_games_tail in text:
    text = text.replace(old_games_tail, new_games_tail, 1)
elif 'm_requestedDetailAssets.clear();' not in text:
    raise SystemExit("games response tail not found")

old_game_response = '''    if (message.contains(QStringLiteral("game"))) {
        const QJsonObject game = message.value(QStringLiteral("game")).toObject();
        const qint64 id = jsonId(game.value(QStringLiteral("id")));
        if (id > 0) {
            m_games.insert(id, game);
            precacheGameAssets(game);
        }
        updateDetail(game, message.value(QStringLiteral("reviews")).toArray());
        return;
    }
'''
new_game_response = '''    if (message.contains(QStringLiteral("game"))) {
        const QJsonObject game = message.value(QStringLiteral("game")).toObject();
        const qint64 id = jsonId(game.value(QStringLiteral("id")));
        if (id > 0) {
            m_games.insert(id, game);
            precacheGameAssets(game);
            renderStore();
            renderLibrary();
        }
        if (id > 0 && id == m_currentGameId && m_pages && m_pages->currentIndex() == static_cast<int>(DetailPage))
            updateDetail(game, message.value(QStringLiteral("reviews")).toArray());
        return;
    }
'''
if old_game_response in text:
    text = text.replace(old_game_response, new_game_response, 1)
elif 'id == m_currentGameId && m_pages' not in text:
    raise SystemExit("game response block not found")

path.write_text(text, encoding="utf-8")
