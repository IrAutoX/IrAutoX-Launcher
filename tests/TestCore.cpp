#include "core/ArchiveUtil.h"
#include "core/MessageCodec.h"

#include <QJsonObject>
#include <QtTest>
#include <QtEndian>
#include <cstring>

using namespace irautox;

class TestCore final : public QObject {
    Q_OBJECT

private slots:
    void protocolRoundTrip()
    {
        const QJsonObject input{
            {QStringLiteral("cmd"), QStringLiteral("login")},
            {QStringLiteral("data"), QJsonObject{{QStringLiteral("username"), QStringLiteral("aria")}}}
        };
        const QByteArray encoded = MessageCodec::encode(input);
        MessageCodec decoder;
        QCOMPARE(decoder.append(encoded.left(2)).size(), 0);
        const QList<QJsonObject> messages = decoder.append(encoded.mid(2));
        QCOMPARE(messages.size(), 1);
        QCOMPARE(messages.first(), input);
    }

    void protocolMultipleFrames()
    {
        const QJsonObject first{{QStringLiteral("status"), QStringLiteral("ok")}};
        const QJsonObject second{{QStringLiteral("version"), QStringLiteral("2.0")}};
        MessageCodec decoder;
        const QList<QJsonObject> messages = decoder.append(MessageCodec::encode(first) + MessageCodec::encode(second));
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages.at(0), first);
        QCOMPARE(messages.at(1), second);
    }

    void protocolRejectsOversizedFrame()
    {
        QByteArray frame(4, '\0');
        const quint32 invalid = qToBigEndian(MessageCodec::MaxFrameSize + 1);
        std::memcpy(frame.data(), &invalid, sizeof(invalid));
        MessageCodec decoder;
        QString error;
        QCOMPARE(decoder.append(frame, &error).size(), 0);
        QVERIFY(!error.isEmpty());
    }

    void archivePathValidation_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("safe");
        QTest::newRow("normal") << QStringLiteral("Game/bin/Game.exe") << true;
        QTest::newRow("nested") << QStringLiteral("assets/textures/a.png") << true;
        QTest::newRow("parent") << QStringLiteral("../outside.exe") << false;
        QTest::newRow("deep-parent") << QStringLiteral("Game/../../outside.exe") << false;
        QTest::newRow("absolute") << QStringLiteral("/Windows/System32/file.dll") << false;
        QTest::newRow("drive") << QStringLiteral("C:/Windows/file.dll") << false;
        QTest::newRow("windows-parent") << QStringLiteral("..\\outside.exe") << false;
    }

    void archivePathValidation()
    {
        QFETCH(QString, path);
        QFETCH(bool, safe);
        QCOMPARE(ArchiveUtil::isSafeEntry(path), safe);
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "TestCore.moc"
