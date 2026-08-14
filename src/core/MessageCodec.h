#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace irautox {

class MessageCodec final {
public:
    static constexpr quint32 MaxFrameSize = 64U * 1024U * 1024U;

    static QByteArray encode(const QJsonObject &message);
    QList<QJsonObject> append(const QByteArray &bytes, QString *error = nullptr);
    void clear();

private:
    QByteArray m_buffer;
};

} // namespace irautox

