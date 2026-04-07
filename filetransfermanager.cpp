#include "filetransfermanager.h"
#include <QDataStream>
#include <QHostAddress>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QUrl>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>

#ifdef Q_OS_ANDROID
    #include <QJniObject>
    #include <QCoreApplication>
#endif

static const quint16 TRANSFER_PORT = 45455;
static const qint64 CHUNK_SIZE = 64 * 1024; // 64 KB

// --- ПОМІЧНИКИ ДЛЯ ШВИДКОСТІ ---
QString formatSize(qint64 bytes) {
    if (bytes < 1024) return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024) return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024 * 1024 * 1024) return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 1) + " GB";
}

QString formatTime(qint64 seconds) {
    if (seconds == 0) return "Менше секунди";
    if (seconds < 60) return QString::number(seconds) + " сек";
    return QString("%1 хв %2 сек").arg(seconds / 60).arg(seconds % 60);
}

void FileTransferManager::updateStats(qint64 transferred, qint64 total) {
    qint64 elapsedMs = transferTimer.elapsed();

    // ЗАХИСТ ВІД ДІЛЕННЯ НА НУЛЬ
    if (elapsedMs <= 0) {
        elapsedMs = 1; // Уникаємо крашу, штучно ставлячи 1 мс
    }
    if (elapsedMs > 0) {
        qint64 bytesPerSec = (transferred * 1000) / elapsedMs;
        qint64 remainingBytes = total - transferred;
        qint64 remainingSecs = bytesPerSec > 0 ? remainingBytes / bytesPerSec : 0;

        emit transferStatsUpdated(formatSize(bytesPerSec) + "/с", formatTime(remainingSecs));
    }
}

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

// 2. Отримання публічної папки Download
QString getPublicDownloadDirectory() {
    #ifdef Q_OS_ANDROID
        QJniObject dir = QJniObject::callStaticObjectMethod(
            "android/os/Environment",
            "getExternalStoragePublicDirectory",
            "(Ljava/lang/String;)Ljava/io/File;",
            QJniObject::fromString("Download").object<jstring>()
            );

        if (dir.isValid()) {
            QString androidPath = dir.callObjectMethod("getAbsolutePath", "()Ljava/lang/String;").toString();
            if (!androidPath.isEmpty()) {
                return androidPath;
            }
        }
    #endif
        // Для Windows, Mac, Linux або якщо на Android щось пішло не так
        return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
}

// --- ВІДПРАВКА ФАЙЛУ ---
void FileTransferManager::sendFile(const QString& ip, const QString& filePath) {
    // 1. Запобігаємо запуску, якщо відправка вже йде
    if (isSending) {
        return;
    }

    // 2. Очищення шляху файлу
    QString realFilePath = filePath;
    if (realFilePath.startsWith("file://")) {
        realFilePath = realFilePath.mid(7);
    }
    // Фікс для деяких Android URI
    if (realFilePath.startsWith("/content://")) {
        realFilePath = realFilePath.mid(1);
    }

    // 3. Відкриття файлу для читання
    sourceFile = new QFile(realFilePath);
    if (!sourceFile->open(QIODevice::ReadOnly)) {
        emit errorOccurred("Не вдалося відкрити файл: " + realFilePath);
        sourceFile->deleteLater();
        sourceFile = nullptr;
        // Затримка перед наступним файлом, щоб уникнути спаму помилками
        QTimer::singleShot(200, this, &FileTransferManager::processNextFile);
        return;
    }

    // Зберігаємо загальний розмір і ОБОВ'ЯЗКОВО скидаємо лічильники
    totalBytesToSend = sourceFile->size();
    bytesWrittenTotal = 0;
    lastUiUpdateTime = 0; // <--- КРИТИЧНО ДЛЯ ДРУГОГО ФАЙЛУ

    // 4. Створення сокета
    senderSocket = new QTcpSocket(this);

    // 5. Очищення при відключенні (успішному або через помилку)
    connect(senderSocket, &QTcpSocket::disconnected, this, [this]() {
        isSending = false;

        if (sourceFile) {
            if (sourceFile->isOpen()) {
                sourceFile->close();
            }
            sourceFile->deleteLater();
            sourceFile = nullptr;
        }

        if (senderSocket) {
            senderSocket->deleteLater();
            senderSocket = nullptr;
        }

        // ВАЖЛИВО: Даємо ОС 200 мс на закриття мережевих дескрипторів перед новим файлом
        QTimer::singleShot(200, this, &FileTransferManager::processNextFile);
    });

    // Обробка помилок
    connect(senderSocket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (senderSocket) {
            emit errorOccurred("Помилка сокета: " + senderSocket->errorString());
            senderSocket->abort(); // Це автоматично викличе сигнал disconnected
        }
    });

    // 6. Дії при УСПІШНОМУ підключенні до Приймача
    connect(senderSocket, &QTcpSocket::connected, this, [this, realFilePath]() {

        QFileInfo fileInfo(realFilePath);
        QString fileName = fileInfo.fileName();

#ifdef Q_OS_ANDROID
        if (realFilePath.startsWith("content://")) {
            QString androidRealName = getAndroidFileName(realFilePath);
            if (!androidRealName.isEmpty()) {
                fileName = androidRealName;
            }
        }
#endif

        if (fileName.isEmpty() || fileName.contains("/")) {
            fileName = "shared_file_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".dat";
        }

        // Формуємо заголовок (Розмір + Ім'я)
        QByteArray headerBlock;
        QDataStream out(&headerBlock, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);
        out << (qint64)totalBytesToSend;
        out << fileName;

        // Слухаємо відповідь від Приймача перед відправкою файлу
        connect(senderSocket, &QTcpSocket::readyRead, this, [this]() {
            QByteArray response = senderSocket->readAll();

            if (response.contains("READY")) {
                isSending = true;
                transferTimer.restart(); // <--- Використовуємо restart для очищення мілісекунд
                lastUiUpdateTime = 0;    // <--- Страховка
                continueTransfer();
            }
        });

        senderSocket->write(headerBlock);
    });

    // 7. Продовжуємо відправляти байти, коли мережевий буфер звільняється
    connect(senderSocket, &QTcpSocket::bytesWritten, this, &FileTransferManager::onBytesWritten);

    // 8. Ініціюємо підключення
    senderSocket->connectToHost(ip, TRANSFER_PORT);
}

void FileTransferManager::onBytesWritten(qint64 bytes) {
    Q_UNUSED(bytes); // Прибираємо попередження компілятора
    if (!isSending || !sourceFile || !senderSocket) return;

    // Якщо буфер ще повний, чекаємо
    if (senderSocket->bytesToWrite() > 4 * CHUNK_SIZE) return;

    continueTransfer();
}

void FileTransferManager::continueTransfer() {
    if (!isSending || !sourceFile || !senderSocket) return;

    if (!sourceFile->atEnd()) {
        QByteArray chunk = sourceFile->read(CHUNK_SIZE);
        if (chunk.isEmpty()) return;

        senderSocket->write(chunk);
        bytesWrittenTotal += chunk.size();

        // Тротлінг UI
        qint64 currentElapsed = transferTimer.elapsed();
        if (currentElapsed - lastUiUpdateTime > 100 || bytesWrittenTotal >= totalBytesToSend) {
            emit progressUpdated(bytesWrittenTotal, totalBytesToSend);
            updateStats(bytesWrittenTotal, totalBytesToSend);
            lastUiUpdateTime = currentElapsed;
        }
    }
    else {
        // Файл повністю вичитано з диску!
        isSending = false; // Зупиняємо цикл

        // КРИТИЧНИЙ ФІКС: Чекаємо, поки байти реально підуть у мережу, щоб файл не побився
        if (senderSocket->bytesToWrite() == 0) {
            senderSocket->disconnectFromHost();
        } else {
            connect(senderSocket, &QTcpSocket::bytesWritten, this, [this]() {
                if (senderSocket && senderSocket->bytesToWrite() == 0) {
                    senderSocket->disconnectFromHost();
                }
            }, Qt::UniqueConnection);
        }
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
}

void FileTransferManager::onReadyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    // СЦЕНАРІЙ 1: Ми приймаємо тіло файлу
    if (socket->property("isFileSocket").toBool() && totalBytesExpected > 0 && targetFile && targetFile->isOpen()) {
        qint64 bytesRemaining = totalBytesExpected - bytesReceived;
        qint64 bytesToRead = qMin(socket->bytesAvailable(), bytesRemaining);
        QByteArray data = socket->read(bytesToRead);

        targetFile->write(data);
        bytesReceived += data.size();

        // === ТРОТЛІНГ UI ===
        // Оновлюємо UI лише раз на 100 мс АБО коли файл завантажено повністю (щоб точно показати 100%)
        qint64 currentElapsed = transferTimer.elapsed();
        if (currentElapsed - lastUiUpdateTime > 100 || bytesReceived >= totalBytesExpected) {
            emit progressUpdated(bytesReceived, totalBytesExpected);
            updateStats(bytesReceived, totalBytesExpected);
            lastUiUpdateTime = currentElapsed; // Запам'ятовуємо час останнього оновлення
        }

        // Якщо файл повністю отримано
        if (bytesReceived >= totalBytesExpected) {
            QString fullPath = QFileInfo(targetFile->fileName()).absoluteFilePath();

            targetFile->close();
            delete targetFile;
            targetFile = nullptr;
            totalBytesExpected = 0;
            socket->setProperty("isFileSocket", false);

            emit fileReceived(QFileInfo(fullPath).fileName(), fullPath);

            socket->disconnectFromHost();
        }
        return;
    }

    // СЦЕНАРІЙ 2: Чекаємо на заголовок (Повідомлення або старт Файлу)
    QDataStream in(socket);
    in.setVersion(QDataStream::Qt_6_0);
    in.startTransaction();

    qint64 headerValue;
    QString stringValue;
    in >> headerValue >> stringValue;

    if (!in.commitTransaction()) {
        return;
    }

    if (headerValue == 0) {
        // === ЦЕ ПОВІДОМЛЕННЯ ===
        QString senderIp = socket->peerAddress().toString().replace("::ffff:", "");
        emit messageReceived(senderIp, stringValue);
        socket->disconnectFromHost();
    } else {
        // === ЦЕ ФАЙЛ ===
        socket->setProperty("isFileSocket", true);
        totalBytesExpected = headerValue;
        QString fileName = stringValue;
        bytesReceived = 0;
        transferTimer.start();

        QString saveDir = getPublicDownloadDirectory();
        QDir dir(saveDir);
        if (!dir.exists()) dir.mkpath(".");

        QString finalPath = dir.filePath(fileName);
        QDir().mkpath(QFileInfo(finalPath).absolutePath());

        int i = 1;
        while (QFile::exists(finalPath)) {
            QFileInfo checkInfo(finalPath);
            finalPath = checkInfo.absolutePath() + "/" +
                        checkInfo.baseName() + "_" +
                        QString::number(i++) + "." +
                        checkInfo.completeSuffix();
        }

        targetFile = new QFile(finalPath);

        if (!targetFile->open(QIODevice::WriteOnly)) {
            emit errorOccurred("Доступ заборонено! Перевірте дозволи: " + finalPath);
            totalBytesExpected = 0;
            socket->setProperty("isFileSocket", false);
            return;
        }

        bytesReceived = 0;
        lastUiUpdateTime = 0; // ОБОВ'ЯЗКОВО СКИДАЄМО ЛІЧИЛЬНИК UI ДЛЯ НОВОГО ФАЙЛУ
        transferTimer.restart(); // Використовуй restart() замість start()

        // НАЙГОЛОВНІШЕ: Файл створено. Кажемо Відправнику "Я ГОТОВИЙ, кидай байти!"
        socket->write("READY");
    }
}

void FileTransferManager::sendFiles(const QString& ip, const QStringList& paths) {
    currentTargetIp = ip;

    for (QString path : paths) {
        // Очистка шляху
        if (path.startsWith("file://")) path.remove("file://");
        if (path.startsWith("/content://")) path.remove(0, 1);

        QFileInfo info(path);
        if (info.isDir()) {
            // Якщо це ПАПКА - витягуємо з неї ВСІ файли (включаючи підпапки)
            QDirIterator it(path, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                fileQueue.append(it.next());
            }
        } else {
            // Якщо це просто файл
            fileQueue.append(path);
        }
    }

    if (!isSending && !fileQueue.isEmpty()) {
        processNextFile();
    }
}

void FileTransferManager::processNextFile() {
    if (fileQueue.isEmpty()) {
        emit transferFinished(true, "Всі файли успішно відправлено!");
        return;
    }

    // Беремо перший файл з черги
    QString filePath = fileQueue.takeFirst();
    sendFile(currentTargetIp, filePath); // Викликаємо твій старий метод
}