#include "discoverymanager.h"
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHostInfo>
#include <QDebug>

static const quint16 PORT = 45454;

DiscoveryManager::DiscoveryManager(QObject* parent) : QObject(parent) {
    udpSocket = new QUdpSocket(this);
    broadcastTimer = new QTimer(this);
    timeoutTimer = new QTimer(this);

    udpSocket->bind(PORT, QUdpSocket::ShareAddress);
    connect(udpSocket, &QUdpSocket::readyRead, this, &DiscoveryManager::processPendingDatagrams);
    connect(broadcastTimer, &QTimer::timeout, this, &DiscoveryManager::sendBroadcast);

    // ЛОГІКА ОЧИЩЕННЯ
    connect(timeoutTimer, &QTimer::timeout, this, [this]() {
        QDateTime now = QDateTime::currentDateTime();
        auto it = lastSeen.begin();
        while (it != lastSeen.end()) {
            if (it.value().secsTo(now) > 10) {
                QString ipToRemove = it.key();
                it = lastSeen.erase(it);

                // ЗМІНА 4: Шукаємо та видаляємо об'єкт зі списку за IP
                for (int i = 0; i < m_peers.size(); ++i) {
                    if (m_peers[i].toMap()["ip"].toString() == ipToRemove) {
                        m_peers.removeAt(i);
                        break;
                    }
                }

                emit peerRemoved(ipToRemove);
                emit peersChanged();
            }
            else {
                ++it;
            }
        }
        });

    timeoutTimer->start(5000);
}

void DiscoveryManager::startBroadcasting() {
    qDebug() << "C++: Запуск трансляції пошуку...";
    if (broadcastTimer) {
        broadcastTimer->start(3000);
    }
}

void DiscoveryManager::sendBroadcast() {
    QJsonObject json;
    json["type"] = "discovery";
    json["user"] = QHostInfo::localHostName();

    QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);

    // ПРОХОДИМО ПО ВСІХ МЕРЕЖЕВИХ ІНТЕРФЕЙСАХ
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& interface : interfaces) {
        // Пропускаємо вимкнені та loopback (127.0.0.1) інтерфейси
        if (!interface.isValid() ||
            (interface.flags() & QNetworkInterface::IsLoopBack) ||
            !(interface.flags() & QNetworkInterface::IsUp)) {
            continue;
        }

        // Відправляємо пакет на кожну активну Broadcast-адресу цього інтерфейсу
        for (const QNetworkAddressEntry& entry : interface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && !entry.broadcast().isNull()) {
                udpSocket->writeDatagram(data, entry.broadcast(), PORT);
                // qDebug() << "Broadcast sent to:" << entry.broadcast().toString() << "on interface:" << interface.humanReadableName();
            }
        }
    }
}

void DiscoveryManager::processPendingDatagrams() {
    while (udpSocket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udpSocket->receiveDatagram();
        QJsonDocument doc = QJsonDocument::fromJson(datagram.data());

        if (!doc.isNull() && doc.isObject()) {
            QJsonObject obj = doc.object();
            if (obj["type"].toString() == "discovery") {
                QString name = obj["user"].toString();
                // ЗМІНА 5: Очищуємо IP одразу тут
                QString ip = datagram.senderAddress().toString().replace("::ffff:", "");

                // Якщо це новий пристрій (перевіряємо по lastSeen)
                if (!lastSeen.contains(ip)) {
                    QVariantMap peer;
                    peer["name"] = name;
                    peer["ip"] = ip;

                    m_peers.append(peer);
                    emit peersChanged();

                    // Відправляємо сигнал (для логів)
                    emit peerFound(ip, name);
                }

                // Оновлюємо час останньої активності
                lastSeen[ip] = QDateTime::currentDateTime();
            }
        }
    }
}