#include "core/DownloadManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>
#include <utility>

namespace irautox {

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
    if (request.gameId <= 0 || !request.url.isValid() || request.targetDirectory.trimmed().isEmpty()) {
        emit taskFailed(request.gameId, tr("اطلاعات دانلود ناقص است."));
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
    startDownload(true);
}

void DownloadManager::cancelCurrent()
{
    if (!m_hasCurrent)
        return;
    m_cancelling = true;
    if (m_reply)
        m_reply->abort();
    else {
        QFile::remove(partPathFor(m_current.gameId));
        emit taskProgress(m_current.gameId, State::Cancelled, 0, 0, 0, 0.0, tr("لغو شد"));
        m_hasCurrent = false;
        m_cancelling = false;
        startNext();
    }
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
    startDownload(true);
}

void DownloadManager::startDownload(bool resume)
{
    cleanupReply();
    const QString partPath = partPathFor(m_current.gameId);
    QDir().mkpath(QFileInfo(partPath).absolutePath());
    m_resumeOffset = resume ? QFileInfo(partPath).size() : 0;
    m_output.setFileName(partPath);
    const QIODevice::OpenMode mode = m_resumeOffset > 0 ? QIODevice::Append : QIODevice::WriteOnly;
    if (!m_output.open(mode)) {
        fail(m_output.errorString());
        return;
    }

    QNetworkRequest request(m_current.url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("IrAutoX-Launcher/%1").arg(QString::fromLatin1(IRAUTOX_VERSION)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    if (m_resumeOffset > 0)
        request.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(m_resumeOffset) + '-');

    m_reply = m_network.get(request);
    m_state = State::Downloading;
    m_pausing = false;
    m_resumeChecked = false;
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
    if (!m_resumeChecked) {
        m_resumeChecked = true;
        const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (m_resumeOffset > 0 && status == 200) {
            m_output.resize(0);
            m_output.seek(0);
            m_resumeOffset = 0;
        }
    }
    const QByteArray data = m_reply->readAll();
    if (m_output.write(data) != data.size()) {
        const QString error = m_output.errorString();
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
                      tr("در حال دانلود"));
}

void DownloadManager::onFinished()
{
    if (!m_reply)
        return;
    const QNetworkReply::NetworkError networkError = m_reply->error();
    const QString networkErrorText = m_reply->errorString();
    m_output.flush();
    m_output.close();
    cleanupReply();

    if (m_pausing) {
        m_pausing = false;
        emit taskProgress(m_current.gameId, State::Paused, 0, 0, 0, 0.0, tr("متوقف شده"));
        return;
    }
    if (m_cancelling) {
        QFile::remove(partPathFor(m_current.gameId));
        emit taskProgress(m_current.gameId, State::Cancelled, 0, 0, 0, 0.0, tr("لغو شد"));
        m_hasCurrent = false;
        m_cancelling = false;
        startNext();
        return;
    }
    if (networkError != QNetworkReply::NoError) {
        fail(networkErrorText);
        return;
    }

    m_state = State::Verifying;
    emit taskProgress(m_current.gameId, m_state, 100, 0, 0, 0.0, tr("بررسی صحت فایل"));
    QString error;
    if (!verifyArchive(&error)) {
        QFile::remove(partPathFor(m_current.gameId));
        fail(error);
        return;
    }

    m_state = State::Installing;
    emit taskProgress(m_current.gameId, m_state, 100, 0, 0, 0.0, tr("در حال نصب"));
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
    QFile::remove(partPathFor(m_current.gameId));
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

void DownloadManager::fail(const QString &message)
{
    if (m_output.isOpen())
        m_output.close();
    cleanupReply();
    m_state = State::Failed;
    emit taskProgress(m_current.gameId, m_state, 0, 0, 0, 0.0, tr("خطا"));
    emit taskFailed(m_current.gameId, message.isEmpty() ? tr("عملیات ناموفق بود.") : message);
    m_hasCurrent = false;
    QMetaObject::invokeMethod(this, &DownloadManager::startNext, Qt::QueuedConnection);
}

QString DownloadManager::partPathFor(qint64 gameId) const
{
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return QDir(cache).filePath(QStringLiteral("downloads/%1.package.part").arg(gameId));
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
        *error = tr("هش SHA-256 فایل دانلودشده با نسخهٔ سرور یکسان نیست.");
        return false;
    }
    return true;
}

} // namespace irautox
