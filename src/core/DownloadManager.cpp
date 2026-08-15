#include "core/DownloadManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <utility>

namespace irautox {
namespace {
constexpr int kMaxFullRetries = 2;

bool isSuccessfulHttpStatus(int status)
{
    return status == 0 || (status >= 200 && status < 300);
}
}

DownloadManager::DownloadManager(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<DownloadRequest>();
    qRegisterMetaType<State>();
    connect(&m_extractionWatcher, &QFutureWatcher<ArchiveResult>::finished,
            this, &DownloadManager::onExtractionFinished);
}

void DownloadManager::enqueue(const DownloadRequest &request)
{
    const QString scheme = request.url.scheme().toLower();
    if (request.gameId <= 0 || !request.url.isValid() || request.targetDirectory.trimmed().isEmpty()
        || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))) {
        emit taskFailed(request.gameId, tr("اطلاعات دانلود ناقص یا آدرس دانلود ناامن است."));
        return;
    }
    if (m_hasCurrent && m_current.gameId == request.gameId)
        return;
    for (const DownloadRequest &queued : std::as_const(m_queue)) {
        if (queued.gameId == request.gameId)
            return;
    }

    m_queue.enqueue(request);
    emit taskAdded(request);
    emit taskProgress(request.gameId, State::Queued, 0, 0, 0, 0.0, tr("در صف"));
    if (!m_hasCurrent)
        QMetaObject::invokeMethod(this, &DownloadManager::startNext, Qt::QueuedConnection);
}

void DownloadManager::pauseCurrent()
{
    if (!m_hasCurrent || m_state != State::Downloading || !m_reply)
        return;
    m_pausing = true;
    m_state = State::Paused;
    m_reply->abort();
}

void DownloadManager::resumeCurrent()
{
    if (!m_hasCurrent || m_state != State::Paused)
        return;
    probeResumeSupport();
}

void DownloadManager::cancelCurrent()
{
    if (!m_hasCurrent)
        return;
    m_cancelling = true;
    if (m_probeReply)
        m_probeReply->abort();
    if (m_reply) {
        m_reply->abort();
        return;
    }
    removePartial();
    emit taskProgress(m_current.gameId, State::Cancelled, 0, 0, 0, 0.0, tr("لغو شد"));
    m_hasCurrent = false;
    m_cancelling = false;
    startNext();
}

bool DownloadManager::isBusy() const
{
    return m_hasCurrent || !m_queue.isEmpty();
}

qint64 DownloadManager::currentGameId() const
{
    return m_hasCurrent ? m_current.gameId : 0;
}

void DownloadManager::startNext()
{
    if (m_hasCurrent || m_queue.isEmpty())
        return;

    m_current = m_queue.dequeue();
    m_hasCurrent = true;
    m_pausing = false;
    m_cancelling = false;
    m_fullRetryCount = 0;
    m_integrityRetryCount = 0;
    m_restartFromZeroPending = false;

    if (!partialMatchesRequest())
        removePartial();

    if (QFileInfo(partPathFor(m_current.gameId)).size() > 0)
        probeResumeSupport();
    else
        startDownload(false);
}

void DownloadManager::probeResumeSupport()
{
    cleanupProbe();
    const QString partPath = partPathFor(m_current.gameId);
    const qint64 offset = QFileInfo(partPath).size();
    if (offset <= 0) {
        startDownload(false);
        return;
    }

    m_state = State::Probing;
    emit taskProgress(m_current.gameId, m_state, 0, offset, 0, 0.0,
                      tr("بررسی پشتیبانی ادامه دانلود"));

    QNetworkRequest request(m_current.url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("IrAutoX-Launcher/%1").arg(QString::fromLatin1(IRAUTOX_VERSION)));
    request.setRawHeader("Accept", "*/*");
    request.setRawHeader("Accept-Encoding", "identity");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(10000);

    m_probeReply = m_network.head(request);
    connect(m_probeReply, &QNetworkReply::finished, this, [this, offset] {
        if (!m_probeReply)
            return;
        const int status = m_probeReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool networkOk = m_probeReply->error() == QNetworkReply::NoError && isSuccessfulHttpStatus(status);
        const QByteArray acceptRanges = m_probeReply->rawHeader("Accept-Ranges").toLower();
        const qint64 total = m_probeReply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        cleanupProbe();

        if (m_cancelling) {
            removePartial();
            emit taskProgress(m_current.gameId, State::Cancelled, 0, 0, 0, 0.0, tr("لغو شد"));
            m_hasCurrent = false;
            m_cancelling = false;
            startNext();
            return;
        }

        if (networkOk && acceptRanges.contains("bytes") && (total <= 0 || offset < total)) {
            emit taskProgress(m_current.gameId, State::Probing, 0, offset, total, 0.0,
                              tr("سرور Resume را پشتیبانی می‌کند"));
            startDownload(true);
            return;
        }

        removePartial();
        emit taskProgress(m_current.gameId, State::Probing, 0, 0, total, 0.0,
                          tr("Resume پشتیبانی نشد؛ دانلود از صفر شروع می‌شود"));
        startDownload(false);
    });
}

void DownloadManager::startDownload(bool resume)
{
    cleanupReply();
    const QString partPath = partPathFor(m_current.gameId);
    QDir().mkpath(QFileInfo(partPath).absolutePath());

    m_resumeOffset = resume ? QFileInfo(partPath).size() : 0;
    if (!resume && QFileInfo::exists(partPath))
        QFile::remove(partPath);

    m_output.setFileName(partPath);
    const QIODevice::OpenMode mode = m_resumeOffset > 0
        ? (QIODevice::WriteOnly | QIODevice::Append)
        : (QIODevice::WriteOnly | QIODevice::Truncate);
    if (!m_output.open(mode)) {
        fail(m_output.errorString());
        return;
    }
    writePartialMetadata();

    QNetworkRequest request(m_current.url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("IrAutoX-Launcher/%1").arg(QString::fromLatin1(IRAUTOX_VERSION)));
    request.setRawHeader("Accept", "*/*");
    request.setRawHeader("Accept-Encoding", "identity");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(45000);
    if (m_resumeOffset > 0)
        request.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(m_resumeOffset) + '-');

    m_requestWasRange = m_resumeOffset > 0;
    m_responseChecked = false;
    m_restartFromZeroPending = false;
    m_reply = m_network.get(request);
    m_state = State::Downloading;
    m_pausing = false;
    m_lastMeasuredBytes = m_resumeOffset;
    m_speedTimer.restart();

    connect(m_reply, &QNetworkReply::readyRead, this, &DownloadManager::onReadyRead);
    connect(m_reply, &QNetworkReply::downloadProgress, this, &DownloadManager::onProgress);
    connect(m_reply, &QNetworkReply::finished, this, &DownloadManager::onFinished);
}

void DownloadManager::onReadyRead()
{
    if (!m_reply)
        return;

    if (!m_responseChecked) {
        const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 0) {
            m_responseChecked = true;
            if (m_requestWasRange) {
                const QByteArray contentRange = m_reply->rawHeader("Content-Range").toLower();
                const QByteArray expectedPrefix = QByteArray("bytes ") + QByteArray::number(m_resumeOffset) + '-';
                if (status != 206 || !contentRange.startsWith(expectedPrefix)) {
                    m_restartFromZeroPending = true;
                    m_reply->abort();
                    return;
                }
            } else if (!isSuccessfulHttpStatus(status)) {
                return;
            }
        }
    }

    const QByteArray bytes = m_reply->readAll();
    if (bytes.isEmpty())
        return;
    if (m_output.write(bytes) != bytes.size()) {
        const QString error = m_output.errorString();
        m_reply->disconnect(this);
        m_reply->abort();
        fail(error);
    }
}

void DownloadManager::onProgress(qint64 received, qint64 total)
{
    const qint64 current = m_resumeOffset + received;
    const qint64 fullSize = total > 0 ? m_resumeOffset + total : 0;
    const int percent = fullSize > 0 ? static_cast<int>((current * 100) / fullSize) : 0;
    double speed = 0.0;
    const qint64 elapsed = m_speedTimer.elapsed();
    if (elapsed >= 500) {
        speed = (current - m_lastMeasuredBytes) * 1000.0 / static_cast<double>(elapsed);
        m_lastMeasuredBytes = current;
        m_speedTimer.restart();
    }
    emit taskProgress(m_current.gameId, State::Downloading, percent, current, fullSize, speed,
                      m_requestWasRange ? tr("در حال ادامه دانلود") : tr("در حال دانلود"));
}

void DownloadManager::onFinished()
{
    if (!m_reply)
        return;

    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = m_reply->error();
    const QString networkErrorText = m_reply->errorString();
    const QByteArray remaining = m_reply->readAll();
    if (!remaining.isEmpty() && m_output.isOpen())
        m_output.write(remaining);
    if (m_output.isOpen()) {
        m_output.flush();
        m_output.close();
    }
    cleanupReply();

    if (m_pausing) {
        m_pausing = false;
        emit taskProgress(m_current.gameId, State::Paused, 0,
                          QFileInfo(partPathFor(m_current.gameId)).size(), 0, 0.0, tr("متوقف شده"));
        return;
    }

    if (m_cancelling) {
        removePartial();
        emit taskProgress(m_current.gameId, State::Cancelled, 0, 0, 0, 0.0, tr("لغو شد"));
        m_hasCurrent = false;
        m_cancelling = false;
        startNext();
        return;
    }

    if (m_restartFromZeroPending || (m_requestWasRange && status != 206)) {
        retryFromZero(tr("پاسخ Range سرور معتبر نبود؛ دانلود از صفر شروع شد."), false);
        return;
    }

    if (networkError != QNetworkReply::NoError || !isSuccessfulHttpStatus(status)) {
        const QString detail = status > 0
            ? tr("HTTP %1 - %2").arg(status).arg(networkErrorText)
            : networkErrorText;
        retryFromZero(detail, true);
        return;
    }

    m_state = State::Verifying;
    emit taskProgress(m_current.gameId, m_state, 100, 0, 0, 0.0, tr("بررسی صحت فایل"));
    QString error;
    if (!verifyArchive(&error)) {
        if (m_integrityRetryCount < 1) {
            ++m_integrityRetryCount;
            retryFromZero(tr("فایل ناقص بود؛ یک دانلود کامل جدید شروع شد."), false);
            return;
        }
        fail(error);
        return;
    }

    m_state = State::Installing;
    emit taskProgress(m_current.gameId, m_state, 100, 0, 0, 0.0, tr("در حال جایگزینی امن فایل‌ها"));
    const QString archive = partPathFor(m_current.gameId);
    const QString target = m_current.targetDirectory;
    m_extractionWatcher.setFuture(QtConcurrent::run([archive, target] {
        return ArchiveUtil::extractZipTransactional(archive, target);
    }));
}

void DownloadManager::onExtractionFinished()
{
    const ArchiveResult result = m_extractionWatcher.result();
    if (!result.ok) {
        fail(result.error);
        return;
    }

    removePartial();
    m_state = State::Completed;
    emit taskProgress(m_current.gameId, m_state, 100, 0, 0, 0.0, tr("نصب شد"));
    emit taskCompleted(m_current);
    m_hasCurrent = false;
    startNext();
}

void DownloadManager::cleanupReply()
{
    if (!m_reply)
        return;
    m_reply->disconnect(this);
    m_reply->deleteLater();
    m_reply = nullptr;
}

void DownloadManager::cleanupProbe()
{
    if (!m_probeReply)
        return;
    m_probeReply->disconnect(this);
    m_probeReply->deleteLater();
    m_probeReply = nullptr;
}

void DownloadManager::retryFromZero(const QString &reason, bool countRetry)
{
    if (m_output.isOpen())
        m_output.close();
    cleanupReply();
    cleanupProbe();
    removePartial();

    if (countRetry) {
        if (m_fullRetryCount >= kMaxFullRetries) {
            fail(reason.isEmpty() ? tr("دانلود پس از چند تلاش ناموفق بود.") : reason);
            return;
        }
        ++m_fullRetryCount;
    }

    emit taskProgress(m_current.gameId, State::Queued, 0, 0, 0, 0.0,
                      reason.isEmpty() ? tr("شروع مجدد دانلود از صفر") : reason);
    const int delay = countRetry ? 600 * m_fullRetryCount : 150;
    QTimer::singleShot(delay, this, [this] {
        if (m_hasCurrent && !m_cancelling)
            startDownload(false);
    });
}

void DownloadManager::fail(const QString &message)
{
    if (m_output.isOpen())
        m_output.close();
    cleanupReply();
    cleanupProbe();
    removePartial();
    m_state = State::Failed;
    emit taskProgress(m_current.gameId, m_state, 0, 0, 0, 0.0, tr("خطا"));
    emit taskFailed(m_current.gameId, message.isEmpty() ? tr("عملیات ناموفق بود.") : message);
    m_hasCurrent = false;
    QMetaObject::invokeMethod(this, &DownloadManager::startNext, Qt::QueuedConnection);
}

void DownloadManager::removePartial()
{
    QFile::remove(partPathFor(m_current.gameId));
    QFile::remove(partMetaPathFor(m_current.gameId));
}

bool DownloadManager::partialMatchesRequest() const
{
    const QString partPath = partPathFor(m_current.gameId);
    if (!QFileInfo::exists(partPath) || QFileInfo(partPath).size() <= 0)
        return false;

    QFile meta(partMetaPathFor(m_current.gameId));
    if (!meta.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject object = QJsonDocument::fromJson(meta.readAll()).object();
    return object.value(QStringLiteral("url")).toString() == m_current.url.toString(QUrl::FullyEncoded)
        && object.value(QStringLiteral("version")).toString() == m_current.version
        && object.value(QStringLiteral("sha256")).toString().compare(m_current.archiveSha256.trimmed(), Qt::CaseInsensitive) == 0;
}

void DownloadManager::writePartialMetadata() const
{
    QSaveFile file(partMetaPathFor(m_current.gameId));
    if (!file.open(QIODevice::WriteOnly))
        return;
    const QJsonObject object{
        {QStringLiteral("game_id"), m_current.gameId},
        {QStringLiteral("url"), m_current.url.toString(QUrl::FullyEncoded)},
        {QStringLiteral("version"), m_current.version},
        {QStringLiteral("sha256"), m_current.archiveSha256.trimmed().toLower()}
    };
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    file.commit();
}

QString DownloadManager::partPathFor(qint64 gameId) const
{
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return QDir(cache).filePath(QStringLiteral("downloads/%1/package.part").arg(gameId));
}

QString DownloadManager::partMetaPathFor(qint64 gameId) const
{
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return QDir(cache).filePath(QStringLiteral("downloads/%1/package.meta.json").arg(gameId));
}

bool DownloadManager::verifyArchive(QString *error) const
{
    if (m_current.archiveSha256.trimmed().isEmpty())
        return true;

    QFile file(partPathFor(m_current.gameId));
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        *error = tr("خواندن فایل برای بررسی صحت ناموفق بود.");
        return false;
    }
    const QString actual = QString::fromLatin1(hash.result().toHex());
    if (actual.compare(m_current.archiveSha256.trimmed(), Qt::CaseInsensitive) != 0) {
        *error = tr("هش SHA-256 فایل دانلودشده با نسخه سرور یکسان نیست.");
        return false;
    }
    return true;
}

} // namespace irautox
