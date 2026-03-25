import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore

ApplicationWindow {
    id: window
    visible: true
    width: 400
    height: 700 
    title: "Lan Share Mobile"

    readonly property color primaryColor: "#2196F3"
    readonly property color accentColor: "#1976D2"
    readonly property color bgColor: "#F5F7FA"
    readonly property color cardColor: "#FFFFFF"
    readonly property color textColor: "#263238"
    readonly property color subTextColor: "#78909C"

    property string selectedDeviceIp: ""

    // Фон всього додатку
    background: Rectangle { color: bgColor }

    // --- ШАПКА ДОДАТКУ (HEADER) ---
    header: Rectangle {
        height: 60
        color: primaryColor
        
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            
            Text {
                text: "Lan Share"
                color: "white"
                font.pixelSize: 22
                font.bold: true
                Layout.fillWidth: true
            }
            // Індикатор (іконка) Wi-Fi для краси
            Text {
                text: "📡"
                font.pixelSize: 20
            }
        }
    }

    // --- ДІАЛОГ ОТРИМАННЯ ФАЙЛУ ---
    Dialog {
        id: receivedDialog
        anchors.centerIn: parent
        width: parent.width * 0.85
        modal: true
        title: "Файл отримано! 🎉"
        standardButtons: Dialog.Ok

        property string fullPath: ""

        contentItem: ColumnLayout {
            spacing: 15
            Text {
                text: "Файл успішно збережено:"
                font.bold: true
                color: textColor
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#E0E0E0"
            }
            Text {
                text: receivedDialog.fullPath
                font.pixelSize: 13
                color: subTextColor
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Text {
                text: "Папка відкриється автоматично."
                font.pixelSize: 11
                color: primaryColor
                Layout.topMargin: 5
            }
        }
    }

    // --- ВИБІР ФАЙЛУ ---
    FileDialog {
        id: filePicker
        title: "Оберіть файл для передачі"
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: {
            let path = selectedFile.toString().replace("file:///", "");
            // Запуск передачі
            fileTransferManager.sendFile(selectedDeviceIp, path);
            transferPopup.open(); // Відкриваємо красивий попап прогресу
        }
    }

    // --- ОСНОВНИЙ КОНТЕНТ ---
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Список пристроїв
        ListView {
            id: deviceList
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 15
            clip: true
            spacing: 12
            
            model: discoveryManager.peers 

            // Анімація додавання нових пристроїв
            add: Transition {
                NumberAnimation { property: "opacity"; from: 0; to: 1.0; duration: 400 }
                NumberAnimation { property: "scale"; from: 0.9; to: 1.0; duration: 400 }
            }

            delegate: ItemDelegate {
                width: deviceList.width
                height: 80

                background: Rectangle {
                    color: parent.down ? "#E3F2FD" : cardColor
                    radius: 12
                    border.color: parent.down ? primaryColor : "transparent"
                    border.width: 1
                    
                    Rectangle {
                        anchors.fill: parent
                        anchors.topMargin: 4
                        z: -1
                        color: "#10000000"
                        radius: 12
                        visible: !parent.down
                    }
                }

                contentItem: RowLayout {
                    spacing: 15
                    
                    // Аватар з першою літерою імені
                    Rectangle {
                        width: 50; height: 50; radius: 25
                        color: "#E1F5FE"
                        Text { 
                            anchors.centerIn: parent; 
                            // Беремо першу букву імені для аватара
                            text: modelData.name.charAt(0).toUpperCase()
                            font.pixelSize: 24 
                            font.bold: true
                            color: primaryColor
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        
                        // ЗМІНА 6: Показуємо ІМ'Я як головний текст
                        Text { 
                            text: modelData.name 
                            font.bold: true
                            font.pixelSize: 16 
                            color: textColor
                        }
                        
                        // IP показуємо дрібним шрифтом знизу
                        RowLayout {
                            spacing: 5
                            Rectangle { width: 6; height: 6; radius: 3; color: "#4CAF50" }
                            Text { 
                                text: modelData.ip // Доступ до IP для відображення
                                font.pixelSize: 12
                                color: subTextColor 
                            }
                        }
                    }

                    Text {
                        text: "›"
                        font.pixelSize: 24
                        color: "#B0BEC5"
                    }
                }

                onClicked: {
                    // ЗМІНА 7: Передаємо IP для з'єднання
                    window.selectedDeviceIp = modelData.ip;
                    filePicker.open();
                }
            }

            // ПУСТИЙ СТАН (коли нікого немає)
            Text {
                anchors.centerIn: parent
                visible: deviceList.count === 0
                text: "🔍 Немає пристроїв поблизу...\nНатисніть «Шукати» внизу"
                horizontalAlignment: Text.AlignHCenter
                color: subTextColor
                font.pixelSize: 16
                lineHeight: 1.5
            }
        }

        // --- КНОПКА ПОШУКУ (Внизу, фіксована) ---
        Rectangle {
            Layout.fillWidth: true
            height: 90
            color: "transparent"
            
            Button {
                id: findButton
                anchors.centerIn: parent
                width: parent.width * 0.9
                height: 50
                
                contentItem: RowLayout {
                    spacing: 10
                    // Центрування контенту в RowLayout
                    Item { Layout.fillWidth: true } 
                    Text { text: "🔍"; font.pixelSize: 18; color: "white" }
                    Text {
                        text: "ШУКАТИ ПРИСТРОЇ"
                        font.bold: true
                        font.pixelSize: 14
                        color: "white"
                    }
                    Item { Layout.fillWidth: true }
                }

                background: Rectangle {
                    color: findButton.down ? accentColor : primaryColor
                    radius: 25
                    // Тінь кнопки
                    Rectangle {
                        anchors.fill: parent
                        anchors.topMargin: 4
                        z: -1
                        color: "#402196F3"
                        radius: 25
                    }
                }

                onClicked: discoveryManager.startBroadcasting()
            }
        }
    }

    // --- POPUP ПРОГРЕСУ (Замість зсуву екрану) ---
    Popup {
        id: transferPopup
        anchors.centerIn: parent
        width: 300
        height: 180
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose // Забороняємо закривати кліком повз

        background: Rectangle {
            color: cardColor
            radius: 16
            border.color: "#E0E0E0"
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 20

            Text {
                text: "🚀 Відправка файлу..."
                font.bold: true
                font.pixelSize: 18
                color: textColor
                Layout.alignment: Qt.AlignHCenter
            }

            // Крутий прогрес-бар
            ProgressBar {
                id: progressBar
                Layout.fillWidth: true
                value: 0
                
                background: Rectangle {
                    implicitWidth: 200
                    implicitHeight: 8
                    color: "#E0E0E0"
                    radius: 4
                }
                contentItem: Item {
                    implicitWidth: 200
                    implicitHeight: 8
                    Rectangle {
                        width: progressBar.visualPosition * parent.width
                        height: 8
                        radius: 4
                        color: primaryColor
                    }
                }
            }

            Text {
                text: (progressBar.value * 100).toFixed(0) + "%"
                color: subTextColor
                Layout.alignment: Qt.AlignHCenter
            }
            
            Button {
                text: "Скасувати"
                flat: true
                Layout.alignment: Qt.AlignHCenter
                onClicked: {
                    // Викликаємо нову функцію C++
                    fileTransferManager.cancelTransfer()
                    transferPopup.close()
                    console.log("Передачу скасовано користувачем")
                }
            }
        }
    }

    // --- ЛОГІКА (CONNECTIONS) ---
    Connections {
        target: fileTransferManager
    
        function onProgressUpdated(sent, total) {
            if (total > 0) progressBar.value = sent / total;
        }

        function onTransferFinished(success, message) {
            transferPopup.close();
            progressBar.value = 0;
            console.log("Статус:", message);
        }
    
        // Додаємо обробку помилок, щоб закрити вікно
        function onErrorOccurred(error) {
            transferPopup.close();
            progressBar.value = 0;
            console.log("Помилка:", error);
            // Тут можна показати MessageDialog з помилкою
        }

        function onFileReceived(fileName, fullPath) {
            receivedDialog.fullPath = fullPath;
            receivedDialog.open();
        }
    }
}