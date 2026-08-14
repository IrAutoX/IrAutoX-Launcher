#pragma once

#include <QString>

namespace irautox {

struct ArchiveResult {
    bool ok = false;
    QString error;
};

class ArchiveUtil final {
public:
    static bool isSafeEntry(const QString &entry);
    static ArchiveResult extractZipTransactional(const QString &archivePath, const QString &targetDirectory);
};

} // namespace irautox

Q_DECLARE_METATYPE(irautox::ArchiveResult)

