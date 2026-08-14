#pragma once

#include <QObject>
#include <QString>

class QLocalServer;

namespace irautox {

class SingleInstance final : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(QString name, QObject *parent = nullptr);

    bool acquire();
    bool sendMessage(const QString &message, int timeoutMs = 1200) const;

signals:
    void messageReceived(const QString &message);

private:
    QString m_name;
    QLocalServer *m_server = nullptr;
};

} // namespace irautox
