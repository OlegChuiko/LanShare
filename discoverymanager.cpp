#include "discoverymanager.h"
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHostInfo>
#include <QDebug>

// --- ДОДАНІ БІБЛІОТЕКИ ДЛЯ ANDROID ---
#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif
// -------------------------------------

static const quint16 PORT = 45454;

// --- НОВА ФУНКЦІЯ ДЛЯ ОТРИМАННЯ ІМЕНІ ПРИСТРОЮ ---
QString getDeviceName() {
#ifdef Q_OS_ANDROID
    // Отримуємо марку та модель через Java API
    QJniObject manufacturer = QJniObject::getStaticObjectField("android/os/Build", "MANUFACTURER", "Ljava/lang/String;");
    QJniObject model = QJniObject::getStaticObjectField("android/os/Build", "MODEL", "Ljava/lang/String;");

    QString strManufacturer = manufacturer.toString();
    QString strModel = model.toString();

    // Форматуємо, щоб не було дублювання (наприклад, "Samsung Samsung Galaxy")
    if (strModel.startsWith(strManufacturer, Qt::CaseInsensitive)) {
        if (!strModel.isEmpty()) strModel[0] = strModel[0].toUpper();
        return strModel;
    } else {
        if (!strManufacturer.isEmpty()) strManufacturer[0] = strManufacturer[0].toUpper();
        return strManufacturer + " " + strModel;
    }
#else
    // Якщо це Windows/Linux/macOS, залишаємо стандартне ім'я
    return QHostInfo::localHostName();
#endif
}
// -------------------------------------------------


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

                // Шукаємо та видаляємо об'єкт зі списку за IP
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

    // --- ЗМІНЕНИЙ РЯДОК: Замість QHostInfo::localHostName() використовуємо нашу функцію ---
    json["user"] = getDeviceName();

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
            }
        }
    }
}

void DiscoveryManager::processPendingDatagrams() {
    // Отримуємо список усіх IP-адрес нашого комп'ютера
    const QList<QHostAddress> localAddresses = QNetworkInterface::allAddresses();

    while (udpSocket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udpSocket->receiveDatagram();

        // Перевіряємо, чи відправник - це не ми самі
        if (localAddresses.contains(datagram.senderAddress())) {
            continue; // Це наш власний пакет, ігноруємо його і йдемо далі
        }

        QJsonDocument doc = QJsonDocument::fromJson(datagram.data());

        if (!doc.isNull() && doc.isObject()) {
            QJsonObject obj = doc.object();
            if (obj["type"].toString() == "discovery") {
                QString name = obj["user"].toString();
                QString ip = datagram.senderAddress().toString().replace("::ffff:", "");

                // Якщо це новий пристрій (перевіряємо по lastSeen)
                if (!lastSeen.contains(ip)) {
                    QVariantMap peer;
                    peer["name"] = name;
                    peer["ip"] = ip;

                    m_peers.append(peer);
                    emit peersChanged();
                    emit peerFound(ip, name);
                }

                // Оновлюємо час останньої активності
                lastSeen[ip] = QDateTime::currentDateTime();
            }
        }
    }
}