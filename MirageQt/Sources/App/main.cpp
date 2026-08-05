#include "Services/DisplayBrokerService.h"
#include "Services/MirageController.h"

#include "FluentUI.h"

#include <QApplication>
#include <QDebug>
#include <QIcon>
#include <QtQml/qqmlextensionplugin.h>
#include <QQmlApplicationEngine>
#include <QQmlContext>

Q_IMPORT_QML_PLUGIN(FluentUIPlugin)

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("MirageQt"));
    QCoreApplication::setOrganizationName(QStringLiteral("Mirage"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    app.setWindowIcon(QIcon(QStringLiteral(":/appicon.png")));

    Mirage::DisplayBrokerService displayBroker;
    QString brokerError;
    if (!displayBroker.start(&brokerError)) {
        qWarning().noquote() << brokerError;
    }

    Mirage::MirageController controller;
    QQmlApplicationEngine engine;
    FluentUI::registerTypes(&engine);
    engine.rootContext()->setContextProperty(QStringLiteral("mirage"), &controller);
    const QUrl url(QStringLiteral("qrc:/MirageQt/Main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
                     [url](QObject* object, const QUrl& objectUrl) {
                         if (!object && objectUrl == url) QCoreApplication::exit(-1);
                     }, Qt::QueuedConnection);
    engine.load(url);
    return app.exec();
}
