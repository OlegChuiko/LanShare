#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDebug>
#include <QListWidgetItem>
#include <QFileDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 1. Ініціалізуємо менеджери
    m_discoveryManager = new DiscoveryManager(this);
    m_transferManager = new FileTransferManager(this);

    // Зв'язуємо сигнал прогресу з віджетом ProgressBar
    connect(m_transferManager, &FileTransferManager::progressUpdated, ui->progressBar, &QProgressBar::setValue);

    // 2. Логіка виявлення пристроїв (UDP)
    connect(m_discoveryManager, &DiscoveryManager::peerFound, this, [this](const QString &ip, const QString &name) {
        bool exists = false;
        for(int i = 0; i < ui->userListWidget->count(); ++i) {
            if(ui->userListWidget->item(i)->toolTip() == ip) {
                exists = true;
                break;
            }
        }

        if (!exists) {
            QListWidgetItem *item = new QListWidgetItem();
            item->setText(name + " (" + ip + ")");
            item->setToolTip(ip); // Зберігаємо IP для майбутнього TCP з'єднання
            ui->userListWidget->addItem(item);
            ui->statusbar->showMessage("Знайдено новий пристрій: " + name, 3000);
        }
    });

    // 3. Логіка відправки файлу (TCP Клієнт)
    connect(ui->btnSendFile, &QPushButton::clicked, this, [this]() {
        QListWidgetItem *currentItem = ui->userListWidget->currentItem();

        if (!currentItem) {
            QMessageBox::warning(this, "Помилка", "Будь ласка, виберіть отримувача зі списку!");
            return;
        }

        QString targetIp = currentItem->toolTip();
        QString filePath = QFileDialog::getOpenFileName(this,
                                                        tr("Вибрати файл для відправки"),
                                                        QDir::homePath(),
                                                        tr("Всі файли (*.*)"));

        if (!filePath.isEmpty()) {
            ui->statusbar->showMessage("Відправка файлу: " + QFileInfo(filePath).fileName());

            // Запускаємо відправку
            m_transferManager->sendFile(targetIp, filePath);

            ui->statusbar->showMessage("Файл успішно відправлено!", 3000);
            ui->progressBar->setValue(0);
        }
    });

    // 4. Логіка отримання файлу (TCP Сервер) + Візуальне сповіщення
    connect(m_transferManager, &FileTransferManager::fileReceived, this, [this](QString filePath) {
        ui->progressBar->setValue(0); // Скидаємо прогрес

        QString fileName = QFileInfo(filePath).fileName();

        // Створюємо діалогове вікно з пропозицією відкрити файл
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Файл отримано!");
        msgBox.setText("Ви отримали файл: " + fileName);
        msgBox.setInformativeText("Бажаєте відкрити папку з файлом?");
        msgBox.setStandardButtons(QMessageBox::Open | QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);

        if (msgBox.exec() == QMessageBox::Open) {
            // Відкриваємо папку, де лежить файл, у Провіднику Windows / Finder Mac
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(filePath).absolutePath()));
        }

        ui->statusbar->showMessage("Отримано файл: " + fileName, 5000);
    });

    connect(m_transferManager, &FileTransferManager::errorOccurred, this, [this](QString msg) {
        ui->statusbar->showMessage("Помилка: " + msg, 5000);
        QMessageBox::critical(this, "Мережева помилка", msg);
    });

    connect(ui->btnSendMessage, &QPushButton::clicked, this, [this]() {
        QListWidgetItem *item = ui->userListWidget->currentItem();
        QString text = ui->messageInput->text();

        if (item && !text.isEmpty()) {
            m_transferManager->sendMessage(item->toolTip(), text);
            ui->chatBrowser->append("<b>Ви:</b> " + text);
            ui->messageInput->clear();
        } else {
            QMessageBox::warning(this, "Увага", "Виберіть користувача та введіть текст!");
        }
    });

    connect(m_transferManager, &FileTransferManager::messageReceived, this, [this](QString ip, QString text) {
        ui->chatBrowser->append("<b>" + ip + ":</b> " + text);
    });

    connect(m_discoveryManager, &DiscoveryManager::peerRemoved, this, [this](const QString &ip) {
        for (int i = 0; i < ui->userListWidget->count(); ++i) {
            QListWidgetItem *item = ui->userListWidget->item(i);
            if (item->toolTip() == ip) {
                delete ui->userListWidget->takeItem(i); // Видаляємо елемент із вікна
                ui->statusbar->showMessage("Користувач офлайн: " + ip, 3000);
                break;
            }
        }
    });

    // 5. Запуск пошуку
    m_discoveryManager->startBroadcasting();
    ui->statusbar->showMessage("Пошук пристроїв у мережі...", 5000);
}
MainWindow::~MainWindow()
{
    delete ui;
}
