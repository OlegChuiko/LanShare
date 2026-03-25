#ifndef DISCOVERYMANAGER_H
#define DISCOVERYMANAGER_H

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QHash>
#include <QDateTime>
#include <QVariant> // Додаємо для роботи з новими даними

class DiscoveryManager : public QObject {
    Q_OBJECT
        // ЗМІНА 1: Тепер це QVariantList замість QStringList
        Q_PROPERTY(QVariantList peers READ peers NOTIFY peersChanged)

public:
    explicit DiscoveryManager(QObject* parent = nullptr);

    // ЗМІНА 2: Повертаємо список об'єктів
    QVariantList peers() const { return m_peers; }

    Q_INVOKABLE void startBroadcasting();

signals:
    void peersChanged();
    void peerFound(const QString& ip, const QString& name);
    void peerRemoved(const QString& ip);

private slots:
    void sendBroadcast();
    void processPendingDatagrams();

private:
    QUdpSocket* udpSocket;
    QTimer* broadcastTimer;
    QTimer* timeoutTimer;

    // ЗМІНА 3: Зберігаємо складні дані
    QVariantList m_peers;

    QHash<QString, QDateTime> lastSeen;
};

#endif