#include "core/ArchiveUtil.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QUuid>

namespace irautox {
namespace {
bool runTar(const QStringList &arguments, QByteArray *standardOutput, QString *error)
{
    QProcess process;
#ifdef Q_OS_WIN
    process.start(QStringLiteral("tar.exe"), arguments);
#else
    process.start(QStringLiteral("tar"), arguments);
#endif
    if (!process.waitForStarted(10000)) {
        *error = QObject::tr("ابزار استخراج آرشیو در ویندوز پیدا نشد.");
        return false;
    }
    if (!process.waitForFinished(15 * 60 * 1000)) {
        process.kill();
        *error = QObject::tr("زمان استخراج فایل بیش از حد طولانی شد.");
        return false;
    }
    if (standardOutput)
        *standardOutput = process.readAllStandardOutput();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        *error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (error->isEmpty())
            *error = QObject::tr("آرشیو قابل استخراج نیست.");
        return false;
    }
    return true;
}
}

bool ArchiveUtil::isSafeEntry(const QString &entry)
{
    QString normalized = entry.trimmed();
    normalized.replace(u'\\', u'/');
    if (normalized.isEmpty() || normalized.startsWith(u'/'))
        return false;
    static const QRegularExpression drivePrefix(QStringLiteral("^[A-Za-z]:"));
    if (drivePrefix.match(normalized).hasMatch())
        return false;
    const QStringList segments = normalized.split(u'/', Qt::SkipEmptyParts);
    for (const QString &segment : segments) {
        if (segment == QStringLiteral(".."))
            return false;
    }
    return true;
}

ArchiveResult ArchiveUtil::extractZipTransactional(const QString &archivePath, const QString &targetDirectory)
{
    const QFileInfo archive(archivePath);
    if (!archive.isFile())
        return {false, QObject::tr("فایل دانلودشده پیدا نشد.")};

    const QFileInfo targetInfo(QDir::cleanPath(targetDirectory));
    const QString target = targetInfo.absoluteFilePath();
    if (target.isEmpty() || QDir(target).isRoot())
        return {false, QObject::tr("مسیر نصب ناامن است.")};

    QByteArray listing;
    QString error;
    if (!runTar({QStringLiteral("-tf"), archive.absoluteFilePath()}, &listing, &error))
        return {false, error};
    const QList<QByteArray> entries = listing.split('\n');
    for (const QByteArray &rawEntry : entries) {
        const QString entry = QString::fromUtf8(rawEntry).trimmed();
        if (!entry.isEmpty() && !isSafeEntry(entry))
            return {false, QObject::tr("آرشیو شامل مسیر ناامن است: %1").arg(entry)};
    }

    QDir parent = targetInfo.absoluteDir();
    if (!QDir().mkpath(parent.absolutePath()))
        return {false, QObject::tr("ساخت پوشهٔ نصب ممکن نیست.")};
    const QString suffix = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString stage = parent.filePath(QStringLiteral(".irautox-stage-%1").arg(suffix));
    const QString backup = parent.filePath(QStringLiteral(".irautox-backup-%1").arg(suffix));
    if (!QDir().mkpath(stage))
        return {false, QObject::tr("ساخت پوشهٔ موقت نصب ممکن نیست.")};

    if (!runTar({QStringLiteral("-xf"), archive.absoluteFilePath(), QStringLiteral("-C"), stage}, nullptr, &error)) {
        QDir(stage).removeRecursively();
        return {false, error};
    }

    const bool hadExisting = QFileInfo::exists(target);
    if (hadExisting && !parent.rename(target, backup)) {
        QDir(stage).removeRecursively();
        return {false, QObject::tr("نسخهٔ قبلی بازی قابل جابه‌جایی نیست؛ بازی را ببندید.")};
    }
    if (!parent.rename(stage, target)) {
        if (hadExisting)
            parent.rename(backup, target);
        QDir(stage).removeRecursively();
        return {false, QObject::tr("جایگزینی فایل‌های بازی ناموفق بود.")};
    }
    if (hadExisting)
        QDir(backup).removeRecursively();
    return {true, {}};
}

} // namespace irautox

