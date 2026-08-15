#include "core/PresenceMonitor.h"

#include "core/GameLibrary.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>

#include <iterator>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <tlhelp32.h>
#endif

namespace irautox {
namespace {

QString normalizedPath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toLower();
}

#ifdef Q_OS_WIN
QHash<QString, QString> runningProcesses()
{
    QHash<QString, QString> result;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return result;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            const QString exeName = QString::fromWCharArray(entry.szExeFile).toLower();
            QString fullPath;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (process) {
                wchar_t buffer[32768]{};
                DWORD size = static_cast<DWORD>(std::size(buffer));
                if (QueryFullProcessImageNameW(process, 0, buffer, &size))
                    fullPath = normalizedPath(QString::fromWCharArray(buffer, static_cast<int>(size)));
                CloseHandle(process);
            }
            if (!fullPath.isEmpty())
                result.insert(fullPath, exeName);
            else if (!result.values().contains(exeName))
                result.insert(QStringLiteral("name:") + exeName, exeName);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}
#endif

}

PresenceMonitor::PresenceMonitor(GameLibrary *library, QObject *parent)
    : QObject(parent), m_library(library)
{
    m_timer.setInterval(1800);
    connect(&m_timer, &QTimer::timeout, this, &PresenceMonitor::scan);
}

void PresenceMonitor::start()
{
    if (!m_timer.isActive())
        m_timer.start();
    QTimer::singleShot(0, this, &PresenceMonitor::scan);
}

void PresenceMonitor::stop()
{
    m_timer.stop();
    if (m_activeGameId > 0) {
        const qint64 elapsed = qMax<qint64>(0, QDateTime::currentSecsSinceEpoch() - m_startedAt);
        emit presenceChanged(m_activeGameId, false, elapsed, QStringLiteral("process"));
    }
    m_activeGameId = 0;
    m_startedAt = 0;
    m_lastHeartbeatAt = 0;
}

void PresenceMonitor::scan()
{
    if (!m_library)
        return;
    qint64 detected = 0;
#ifdef Q_OS_WIN
    const QHash<QString, QString> running = runningProcesses();
    for (auto it = m_library->games().constBegin(); it != m_library->games().constEnd(); ++it) {
        const InstalledGame &game = it.value();
        const QString absolute = normalizedPath(QDir(game.rootPath).absoluteFilePath(game.executable));
        const QString exeName = QFileInfo(game.executable).fileName().toLower();
        if (running.contains(absolute) || running.contains(QStringLiteral("name:") + exeName)) {
            detected = game.id;
            break;
        }
    }
#endif
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (detected == m_activeGameId) {
        if (m_activeGameId > 0 && now - m_lastHeartbeatAt >= 20) {
            emit presenceChanged(m_activeGameId, true, qMax<qint64>(0, now - m_startedAt), QStringLiteral("process"));
            m_lastHeartbeatAt = now;
        }
        return;
    }
    if (m_activeGameId > 0)
        emit presenceChanged(m_activeGameId, false, qMax<qint64>(0, now - m_startedAt), QStringLiteral("process"));
    m_activeGameId = detected;
    m_startedAt = detected > 0 ? now : 0;
    m_lastHeartbeatAt = detected > 0 ? now : 0;
    if (m_activeGameId > 0)
        emit presenceChanged(m_activeGameId, true, 0, QStringLiteral("process"));
}

}
