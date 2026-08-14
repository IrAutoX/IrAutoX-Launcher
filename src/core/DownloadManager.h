#pragma once

#include "core/ArchiveUtil.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QQueue>
#include <QUrl>

namespace irautox {

struct DownloadRequest {
    qint64 gameId = 0;
    QString gameName;
    QUrl url;
    QString targetDirectory;
    QString executable;
    QString version;
    QString archiveSha256;
    QString executableSha256;
    bool update = false;
};

class DownloadManager final : public QObject {
    Q_OBJECT
public:
    enum class State { Queued, Downloading, Paused, Verifying, Installing, Completed, Failed, Cancelled };
    Q_ENUM(State)

    explicit DownloadManager(QObject *parent = nullptr);
    void enqueue(const DownloadRequest &request);
    void pauseCurrent();
    void resumeCurrent();
    void cancelCurrent();
    bool isBusy() const;
    qint64 currentGameId() const;

signals:
    void taskAdded(const irautox::DownloadRequest &request);
    void taskProgress(qint64 gameId, irautox::DownloadManager::State state, int percent,
                      qint64 received, qint64 total, double bytesPerSecond, const QString &detail);
    void taskCompleted(const irautox::DownloadRequest &request);
    void taskFailed(qint64 gameId, const QString &error);

private slots:
    void startNext();
    void onReadyRead();
    void onProgress(qint64 received, qint64 total);
    void onFinished();
    void onExtractionFinished();

private:
    void startDownload(bool resume);
    void cleanupReply();
    void fail(const QString &message);
    QString partPathFor(qint64 gameId) const;
    bool verifyArchive(QString *error) const;

    QNetworkAccessManager m_network;
    QQueue<DownloadRequest> m_queue;
    DownloadRequest m_current;
    QNetworkReply *m_reply = nullptr;
    QFile m_output;
    QElapsedTimer m_speedTimer;
    QFutureWatcher<ArchiveResult> m_extractionWatcher;
    qint64 m_resumeOffset = 0;
    qint64 m_lastMeasuredBytes = 0;
    bool m_hasCurrent = false;
    bool m_pausing = false;
    bool m_cancelling = false;
    bool m_resumeChecked = false;
    State m_state = State::Queued;
};

} // namespace irautox

Q_DECLARE_METATYPE(irautox::DownloadRequest)
Q_DECLARE_METATYPE(irautox::DownloadManager::State)

