#include "filetransfermanager.h"
#include <QDataStream>
#include <QHostAddress>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QUrl>
#include <QDesktopServices>
#include <QDir>

#ifdef Q_OS_ANDROID
    #include <QJniObject>
    #include <QCoreApplication>
#endif

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

#ifdef Q_OS_ANDROID
    QString getAndroidFileName(const QString &contentUri) {
        // 1. Конвертуємо рядок у Java-об'єкт Uri
        QJniObject jUriString = QJniObject::fromString(contentUri);
        QJniObject uri = QJniObject::callStaticObjectMethod("android/net/Uri",
                                                            "parse",
                                                            "(Ljava/lang/String;)Landroid/net/Uri;",
                                                            jUriString.object<jstring>());

        // 2. Отримуємо Context та ContentResolver
        QJniObject context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid()) return "";

        QJniObject contentResolver = context.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
        if (!contentResolver.isValid()) return "";

        // 3. Робимо запит до бази даних (Cursor query)
        QJniObject cursor = contentResolver.callObjectMethod(
            "query",
            "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Landroid/database/Cursor;",
            uri.object(), nullptr, nullptr, nullptr, nullptr
            );

        QString fileName;
        if (cursor.isValid()) {
            // Переходимо до першого запису в таблиці
            if (cursor.callMethod<jboolean>("moveToFirst")) {
                // Шукаємо колонку "_display_name" (це стандартна колонка Android для імен файлів)
                QJniObject columnName = QJniObject::fromString("_display_name");
                jint columnIndex = cursor.callMethod<jint>("getColumnIndex", "(Ljava/lang/String;)I", columnName.object<jstring>());

                if (columnIndex != -1) {
                    QJniObject nameObj = cursor.callObjectMethod("getString", "(I)Ljava/lang/String;", columnIndex);
                    if (nameObj.isValid()) {
                        fileName = nameObj.toString();
                    }
                }
            }
            cursor.callMethod<void>("close"); // Обов'язково закриваємо Cursor, щоб не було витоку пам'яті
        }
        return fileName;
    }
#endif

// --- ВІДПРАВКА ФАЙЛУ ---
void FileTransferManager::sendFile(const QString& ip, const QString& filePath) {
    if (isSending) {
        emit errorOccurred("Вже виконується передача файлу");
        return;
    }

    // --- СУПЕР-ОЧИСТКА ШЛЯХУ ---
    QString realFilePath = filePath;

    if (realFilePath.startsWith("file://")) {
        realFilePath.remove("file://");
    }

    if (realFilePath.startsWith("/content://")) {
        realFilePath.remove(0, 1);
    }
    // ----------------------------

    sourceFile = new QFile(realFilePath);
    if (!sourceFile->open(QIODevice::ReadOnly)) {
        emit errorOccurred("Не вдалося відкрити файл: " + realFilePath);
        delete sourceFile;
        sourceFile = nullptr;
        return;
    }

    totalBytesToSend = sourceFile->size();
    bytesWrittenTotal = 0;

    senderSocket = new QTcpSocket(this);
    senderSocket->connectToHost(ip, TRANSFER_PORT);

    connect(senderSocket, &QTcpSocket::connected, [this, realFilePath]() {
        QFileInfo fileInfo(realFilePath);
        QString fileName = fileInfo.fileName();

        // --- ВИТЯГУЄМО СПРАВЖНЄ ІМ'Я НА ANDROID ---
#ifdef Q_OS_ANDROID
    if (realFilePath.startsWith("content://")) {
        QString androidRealName = getAndroidFileName(realFilePath);
        if (!androidRealName.isEmpty()) {
            fileName = androidRealName;
        }
    }
#endif

        // Страховка на випадок, якщо щось піде не так
        if (fileName.isEmpty() || fileName.contains("/")) {
            fileName = "shared_file.dat";
        }

        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);

        out << totalBytesToSend;
        out << fileName;

        senderSocket->write(block);
        isSending = true;
    });

    connect(senderSocket, &QTcpSocket::bytesWritten, this, &FileTransferManager::onBytesWritten);

    connect(senderSocket, &QTcpSocket::errorOccurred, [this](QAbstractSocket::SocketError) {
        emit errorOccurred("Помилка сокета: " + senderSocket->errorString());
        cancelTransfer();
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
    if (totalBytesExpected > 0 && targetFile && targetFile->isOpen()) {
        QByteArray data = socket->readAll();
        targetFile->write(data);
        bytesReceived += data.size();

        emit progressUpdated(bytesReceived, totalBytesExpected);

        // Перевіряємо, чи завантажили ми файл повністю
        if (bytesReceived >= totalBytesExpected) {
            QString fullPath = QFileInfo(targetFile->fileName()).absoluteFilePath();
            QString folderPath = QFileInfo(targetFile->fileName()).absolutePath();

            targetFile->close();
            delete targetFile;
            targetFile = nullptr;
            totalBytesExpected = 0; // Скидаємо стан для наступних передач!

            emit fileReceived(QFileInfo(fullPath).fileName(), fullPath);
            QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath));
        }
        return; // Виходимо, бо ми обробили шматок файлу
    }

    // СЦЕНАРІЙ 2: Чекаємо на заголовок (Повідомлення або старт Файлу)
    QDataStream in(socket);
    in.setVersion(QDataStream::Qt_6_0);

    // Запускаємо транзакцію!
    in.startTransaction();

    qint64 headerValue;
    QString stringValue;
    in >> headerValue >> stringValue;

    // Якщо дані ще не долетіли повністю, commitTransaction поверне false.
    // Ми просто виходимо з функції і чекаємо наступного сигналу readyRead.
    if (!in.commitTransaction()) {
        return;
    }

    // --- Якщо ми дійшли сюди, значить весь заголовок 100% прочитано! ---

    if (headerValue == 0) {
        // === ЦЕ ПОВІДОМЛЕННЯ ===
        QString senderIp = socket->peerAddress().toString().replace("::ffff:", "");
        emit messageReceived(senderIp, stringValue);

    } else {
        // === ЦЕ ФАЙЛ ===
        totalBytesExpected = headerValue;
        QString fileName = stringValue;
        bytesReceived = 0;

        QString saveDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);

        if (saveDir.isEmpty()) {
            saveDir = "/storage/emulated/0/Download";
        }

        QDir dir(saveDir);
        if (!dir.exists()) dir.mkpath(".");

        QString finalPath = dir.filePath(fileName);
        int i = 1;
        while (QFile::exists(finalPath)) {
            QFileInfo checkInfo(fileName);
            finalPath = dir.filePath(checkInfo.baseName() + "_" + QString::number(i++) + "." + checkInfo.completeSuffix());
        }

        targetFile = new QFile(finalPath);

        if (!targetFile->open(QIODevice::WriteOnly)) {
            // Якщо помилка тут - значить точно немає дозволу на запис в пам'ять
            emit errorOccurred("Доступ заборонено! Перевірте дозволи додатка: " + finalPath);
            totalBytesExpected = 0;
            return;
        }

        // ВАЖЛИВО: Оскільки ми використовували транзакцію, QDataStream забрав
        // рівно стільки байт, скільки займає заголовок. Якщо відправник уже
        // встиг надіслати перші байти самого файлу, вони все ще лежать у сокеті!
        if (socket->bytesAvailable() > 0) {
            QByteArray remainingData = socket->readAll();
            targetFile->write(remainingData);
            bytesReceived += remainingData.size();
            emit progressUpdated(bytesReceived, totalBytesExpected);
        }
    }
}