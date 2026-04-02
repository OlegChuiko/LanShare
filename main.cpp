#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext> // ÎÁÎÂ'ßÇÊÎÂÎ äëÿ setContextProperty


// Ï³äêëþ÷àºìî òâî¿ ìåíåäæåðè
#include "discoverymanager.h"
#include "filetransfermanager.h"

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);

    // Ñòâîðþºìî îá'ºêòè C++
    DiscoveryManager discoveryManager;
    FileTransferManager fileTransferManager; // Ñòâîðþºìî ôàéëîâèé ìåíåäæåð

    QQmlApplicationEngine engine;

    // --- ÐÅªÑÒÐÀÖ²ß ÄËß QML (ÊÐÈÒÈ×ÍÎ ÂÀÆËÈÂÎ) ---
    // Òåïåð QML çíàòèìå ¿õ ï³ä ³ìåíàìè "discoveryManager" òà "fileTransferManager"
    engine.rootContext()->setContextProperty("discoveryManager", &discoveryManager);
    engine.rootContext()->setContextProperty("fileTransferManager", &fileTransferManager);
    // ---------------------------------------------

    const QUrl url("qrc:/qt/qml/Lan_Share/main.qml");
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject* obj, const QUrl& objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}