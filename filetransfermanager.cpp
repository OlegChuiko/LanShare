#include "filetransfermanager.h"
#include <QDataStream>
#include <QHostAddress>
#include <QStandardPaths>
#include <QCoreApplication> 
#include <QUrl>              
#include <QDesktopServices>  
#include <QDir>              

static const quint16 TRANSFER_PORT = 45455;
static const qint64 CHUNK_SIZE = 64 * 1024; // 64 KB

FileTransferManager::FileTransferManager(QObject* parent)
    : QObject(parent), targetFile(nullptr), senderSocket(nullptr), sourceFile(nullptr),
    totalBytesExpected(0), bytesReceived(0), isReceivingMessage(false), isSending(false) {

    tcpServer = new QTcpServer(this);
    if (!tcpServer->listen(QHostAddress::Any, TRANSFER_PORT)) {
        qDebug() << "Server Error: Не вдалося запустити сервер";
    }
    connect(tcpServer, &QTcpServer::newConnection, this, &FileTransferManager::onNewConnection);
}

// --- ВІДПРАВКА ПОВІДОМЛЕННЯ (ЧАТ) ---
void FileTransferManager::sendMessage(const QString& ip, const QString& message) {
    QTcpSocket* msgSocket = new QTcpSocket(this);
    msgSocket->connectToHost(ip, TRANSFER_PORT);

    connect(msgSocket, &QTcpSocket::connected, [msgSocket, message]() {
        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);

        // ПРОТОКОЛ:
        // 1. Розмір (0 означає "Це повідомлення")
        // 2. Сам текст
        out << qint64(0);
        out << message;

        msgSocket->write(block);
        msgSocket->disconnectFromHost();
        });

    connect(msgSocket, &QTcpSocket::disconnected, msgSocket, &QTcpSocket::deleteLater);
}

// --- ВІДПРАВКА ФАЙЛУ ---
void FileTransferManager::sendFile(const QString& ip, const QString& filePath) {
    if (isSending) {
        emit errorOccurred("Вже виконується передача файлу");
        return;
    }

    sourceFile = new QFile(filePath);
    if (!sourceFile->open(QIODevice::ReadOnly)) {
        emit errorOccurred("Не вдалося відкрити файл для читання");
        delete sourceFile;
        sourceFile = nullptr;
        return;
    }

    totalBytesToSend = sourceFile->size();
    bytesWrittenTotal = 0;

    senderSocket = new QTcpSocket(this);
    senderSocket->connectToHost(ip, TRANSFER_PORT);

    connect(senderSocket, &QTcpSocket::connected, [this, filePath]() {
        QFileInfo fileInfo(filePath);
        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);

        // ПРОТОКОЛ:
        // 1. Розмір файлу (> 0)
        // 2. Ім'я файлу
        out << totalBytesToSend;
        out << fileInfo.fileName();

        senderSocket->write(block);
        isSending = true;
        });

    connect(senderSocket, &QTcpSocket::bytesWritten, this, &FileTransferManager::onBytesWritten);

    connect(senderSocket, &QTcpSocket::errorOccurred, [this](QAbstractSocket::SocketError) {
        if (isSending) {
            emit errorOccurred("Помилка сокета: " + senderSocket->errorString());
            cancelTransfer();
        }
        });
}

void FileTransferManager::onBytesWritten(qint64 bytes) {
    if (!isSending || !sourceFile) return;
    if (senderSocket->bytesToWrite() > 4 * CHUNK_SIZE) return;
    continueTransfer();
}

void FileTransferManager::continueTransfer() {
    if (!isSending || !sourceFile) return;

    if (!sourceFile->atEnd()) {
        QByteArray chunk = sourceFile->read(CHUNK_SIZE);
        senderSocket->write(chunk);
        bytesWrittenTotal += chunk.size();
        emit progressUpdated(bytesWrittenTotal, totalBytesToSend);
    }
    else {
        senderSocket->disconnectFromHost();
        cancelTransfer();
        emit transferFinished(true, "Файл успішно відправлено");
    }
}

void FileTransferManager::cancelTransfer() {
    isSending = false;
    if (sourceFile) {
        if (sourceFile->isOpen()) sourceFile->close();
        sourceFile->deleteLater();
        sourceFile = nullptr;
    }
    if (senderSocket) {
        senderSocket->abort();
        senderSocket->deleteLater();
        senderSocket = nullptr;
    }
    emit progressUpdated(0, 0);
}

// --- ПРИЙОМ ДАНИХ (ФАЙЛ АБО ПОВІДОМЛЕННЯ) ---
void FileTransferManager::onNewConnection() {
    QTcpSocket* socket = tcpServer->nextPendingConnection();
    connect(socket, &QTcpSocket::readyRead, this, &FileTransferManager::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);

    // Скидання стану прийому
    bytesReceived = 0;
    totalBytesExpected = 0;
    isReceivingMessage = false;

    if (targetFile) {
        targetFile->close();
        delete targetFile;
        targetFile = nullptr;
    }
}

void FileTransferManager::onReadyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    // СЦЕНАРІЙ 1: Ми вже приймаємо тіло файлу
    if (targetFile && targetFile->isOpen()) {
        QByteArray data = socket->readAll();
        targetFile->write(data);
        bytesReceived += data.size();
        emit progressUpdated(bytesReceived, totalBytesExpected);

        if (bytesReceived >= totalBytesExpected) {
            QString fullPath = QFileInfo(targetFile->fileName()).absoluteFilePath();
            QString folderPath = QFileInfo(targetFile->fileName()).absolutePath();
            targetFile->close();

            emit fileReceived(QFileInfo(fullPath).fileName(), fullPath);
            QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath)); // Відкрити папку (Windows)
        }
        return;
    }

    // СЦЕНАРІЙ 2: Нове з'єднання (Заголовок файлу АБО Повідомлення)
    if (totalBytesExpected == 0) {
        QDataStream in(socket);
        in.setVersion(QDataStream::Qt_6_0);

        if (socket->bytesAvailable() < sizeof(qint64)) return; // Чекаємо на розмір

        qint64 headerValue;
        in >> headerValue;

        if (headerValue == 0) {
            // === ЦЕ ПОВІДОМЛЕННЯ ===
            QString msg;
            in >> msg;
            QString senderIp = socket->peerAddress().toString().replace("::ffff:", "");
            emit messageReceived(senderIp, msg);
            isReceivingMessage = true;
        }
        else {
            // === ЦЕ ФАЙЛ ===
            totalBytesExpected = headerValue;
            QString fileName;
            in >> fileName;

            // Збереження в папку завантажень
            QString saveDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
            if (saveDir.isEmpty()) saveDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

            QDir dir(saveDir);
            if (!dir.exists()) dir.mkpath(".");

            QString finalPath = dir.filePath(fileName);
            targetFile = new QFile(finalPath);

            if (!targetFile->open(QIODevice::WriteOnly)) {
                emit errorOccurred("Не вдалося створити файл: " + finalPath);
                return;
            }

            // Якщо в буфері залишились дані (початок файлу), записуємо їх
            if (!in.atEnd()) {
                // Увага: QDataStream вже "з'їв" заголовок, але дані файлу йдуть "сирими" після нього.
                // Тому читаємо решту буфера сокета безпосередньо.
                QByteArray remainingData = socket->readAll();
                targetFile->write(remainingData);
                bytesReceived += remainingData.size();
            }
        }
    }
}