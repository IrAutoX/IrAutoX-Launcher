from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
cpp_path = ROOT / "src/ui/MainWindow.cpp"
h_path = ROOT / "src/ui/MainWindow.h"
cpp = cpp_path.read_text(encoding="utf-8")
h = h_path.read_text(encoding="utf-8")

cpp = cpp.replace("QNetworkRequest request(QUrl(value));", "QNetworkRequest request{QUrl(value)};")
cpp = cpp.replace("QNetworkRequest req(QUrl(value));", "QNetworkRequest req{QUrl(value)};")

old = '''void MainWindow::setStartupGameId(qint64 gameId)
{
    m_startupGameId = gameId;
}
'''
new = '''void MainWindow::setStartupGameId(qint64 gameId)
{
    if (gameId <= 0)
        return;
    if (!m_user.isEmpty()) {
        QTimer::singleShot(0, this, [this, gameId] { checkAndLaunch(gameId); });
        return;
    }
    m_startupGameId = gameId;
}
'''
if old in cpp:
    cpp = cpp.replace(old, new, 1)

signature = "void MainWindow::applyGameIcon(QLabel *label, const QJsonObject &game, const QSize &size, bool circular)"
next_signature = "QFrame *MainWindow::createWindowBar(QWidget *parent)"
start = cpp.find(signature)
end = cpp.find(next_signature, start)
if start >= 0 and end > start:
    body = r'''void MainWindow::applyGameIcon(QLabel *label, const QJsonObject &game, const QSize &size, bool circular)
{
    if (!label)
        return;

    QString value;
    const QJsonValue iconValue = game.value(QStringLiteral("icon"));
    if (iconValue.isString())
        value = iconValue.toString().trimmed();
    else if (iconValue.isObject())
        value = iconValue.toObject().value(QStringLiteral("url")).toString().trimmed();
    if (value.isEmpty())
        value = game.value(QStringLiteral("icon_url")).toString().trimmed();
    if (value.isEmpty())
        value = game.value(QStringLiteral("logo")).toString().trimmed();
    if (value.isEmpty())
        value = game.value(QStringLiteral("icon_base64")).toString().trimmed();

    if (value.startsWith(QStringLiteral("//")))
        value.prepend(QStringLiteral("https:"));
    else if (value.startsWith(QLatin1Char('/')))
        value.prepend(QStringLiteral("https://irautox.ir"));
    else if (value.startsWith(QStringLiteral("icons/"), Qt::CaseInsensitive) ||
             value.startsWith(QStringLiteral("uploads/"), Qt::CaseInsensitive) ||
             value.startsWith(QStringLiteral("games/"), Qt::CaseInsensitive))
        value.prepend(QStringLiteral("https://irautox.ir/"));

    const qint64 gameId = jsonId(game.value(QStringLiteral("id")));
    const QByteArray keyData = value.toUtf8();
    const QString fallbackKey = QString::fromLatin1(QCryptographicHash::hash(keyData, QCryptographicHash::Sha256).toHex().left(24));
    const QString cacheKey = gameId > 0 ? QString::number(gameId) : fallbackKey;
    const QString cacheDir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                                 .filePath(QStringLiteral("cache/game-icons"));
    QDir().mkpath(cacheDir);
    const QString cachePath = QDir(cacheDir).filePath(cacheKey + QStringLiteral(".img"));

    QPointer<QLabel> safeLabel(label);
    auto display = [safeLabel, size, circular](const QPixmap &source) {
        if (!safeLabel || source.isNull())
            return;
        safeLabel->setPixmap(circular ? circularPixmap(source, size)
                                      : source.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    };

    QPixmap cached;
    const bool hasCached = cached.load(cachePath);
    if (hasCached)
        display(cached);

    const bool remote = value.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
                        value.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
    if (remote) {
        if (!hasCached)
            label->setPixmap(QIcon(QStringLiteral(":/logo.svg")).pixmap(size));
        QNetworkRequest request{QUrl(value)};
        request.setTransferTimeout(10000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("IrAutoX-Launcher/%1").arg(QString::fromLatin1(IRAUTOX_VERSION)));
        QNetworkReply *reply = m_assetNetwork->get(request);
        connect(reply, &QNetworkReply::finished, this, [reply, safeLabel, size, circular, cachePath] {
            const QByteArray bytes = reply->readAll();
            const bool ok = reply->error() == QNetworkReply::NoError;
            reply->deleteLater();
            if (!ok || !safeLabel || bytes.isEmpty())
                return;
            QPixmap pix;
            if (!pix.loadFromData(bytes))
                return;
            QSaveFile cache(cachePath);
            if (cache.open(QIODevice::WriteOnly)) {
                cache.write(bytes);
                cache.commit();
            }
            safeLabel->setPixmap(circular ? circularPixmap(pix, size)
                                          : pix.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
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

    if (!hasCached)
        label->setPixmap(QIcon(QStringLiteral(":/logo.svg")).pixmap(size));
}

'''
    cpp = cpp[:start] + body + cpp[end:]

if "#include <QSize>" not in h:
    h = h.replace("#include <QPoint>\n", "#include <QPoint>\n#include <QSize>\n")

cpp_path.write_text(cpp, encoding="utf-8", newline="\n")
h_path.write_text(h, encoding="utf-8", newline="\n")
