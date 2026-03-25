#ifndef FILETRANSFERMANAGER_H
#define FILETRANSFERMANAGER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QTimer>

class FileTransferManager : public QObject {
    Q_OBJECT
public:
    explicit FileTransferManager(QObject* parent = nullptr);

    // --- ФУНКЦІЇ ВІДПРАВКИ ---
    Q_INVOKABLE void sendFile(const QString& ip, const QString& filePath);
    Q_INVOKABLE void sendMessage(const QString& ip, const QString& message); // ПОВЕРНУЛИ
    Q_INVOKABLE void cancelTransfer();

signals:
    // --- СИГНАЛИ ---
    void progressUpdated(qint64 bytesProcessed, qint64 totalBytes);
    void errorOccurred(const QString& error);
    void transferFinished(bool success, const QString& message);
    void fileReceived(const QString& fileName, const QString& fullPath);
    void messageReceived(const QString& fromIp, const QString& message); // ПОВЕРНУЛИ

private slots:
    void onNewConnection();
    void onReadyRead();

    // Слоти для асинхронної відправки файлів
    void continueTransfer();
    void onBytesWritten(qint64 bytes);

private:
    QTcpServer* tcpServer;

    // Змінні для ПРИЙОМУ
    QFile* targetFile;
    qint64 totalBytesExpected;
    qint64 bytesReceived;
    bool isReceivingMessage; // Прапорець: чи читаємо ми зараз чат-повідомлення?

    // Змінні для ВІДПРАВКИ
    QTcpSocket* senderSocket;
    QFile* sourceFile;
    qint64 totalBytesToSend;
    qint64 bytesWrittenTotal;
    bool isSending;
};

#endif