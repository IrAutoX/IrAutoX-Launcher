#include "core/MessageCodec.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QtEndian>
#include <cstring>

namespace irautox {

QByteArray MessageCodec::encode(const QJsonObject &message)
{
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    QByteArray frame(static_cast<qsizetype>(sizeof(quint32)), Qt::Uninitialized);
    const quint32 size = qToBigEndian(static_cast<quint32>(payload.size()));
    std::memcpy(frame.data(), &size, sizeof(size));
    frame.append(payload);
    return frame;
}

QList<QJsonObject> MessageCodec::append(const QByteArray &bytes, QString *error)
{
    m_buffer.append(bytes);
    QList<QJsonObject> messages;

    while (m_buffer.size() >= static_cast<qsizetype>(sizeof(quint32))) {
        quint32 networkSize = 0;
        std::memcpy(&networkSize, m_buffer.constData(), sizeof(networkSize));
        const quint32 payloadSize = qFromBigEndian(networkSize);
        if (payloadSize == 0 || payloadSize > MaxFrameSize) {
            if (error)
                *error = QStringLiteral("Invalid protocol frame size: %1").arg(payloadSize);
            clear();
            return {};
        }

        const qsizetype frameSize = static_cast<qsizetype>(sizeof(quint32)) + payloadSize;
        if (m_buffer.size() < frameSize)
            break;

        const QByteArray payload = m_buffer.mid(static_cast<qsizetype>(sizeof(quint32)), payloadSize);
        m_buffer.remove(0, frameSize);

        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            if (error)
                *error = QStringLiteral("Invalid JSON response: %1").arg(parseError.errorString());
            continue;
        }
        messages.push_back(document.object());
    }
    return messages;
}

void MessageCodec::clear()
{
    m_buffer.clear();
}

} // namespace irautox

