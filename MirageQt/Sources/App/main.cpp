#include "Services/DisplayBrokerService.h"
#include "Services/MirageController.h"
#include "StatusBar.h"

#include "FluentUI.h"

#include <QApplication>
#include <QDebug>
#include <QIcon>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

Q_IMPORT_QML_PLUGIN(FluentUIPlugin)

int main(int argc, char** argv) {
    // RichHTMLText 的 WebEngineView（远程富标签）需要 WebEngine 初始化；
    // 必须在 QCoreApplication/QGuiApplication 创建之前调用
    // （对齐 QtWebEngineQuick 的初始化要求，之后调用仅剩 deprecated 行为）。
    QtWebEngineQuick::initialize();
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
    // 桌面窗口事实（焦点/全屏）由 broker 宿主回调上报，应用据此按播放规则
    // 驱动渲染器（对齐 macOS 的应用侧播放策略）。
    QObject::connect(&displayBroker, &Mirage::DisplayBrokerService::windowStateChanged,
                     &controller, &Mirage::MirageController::handleWindowState);
    QQmlApplicationEngine engine;
    FluentUI::registerTypes(&engine);
    engine.rootContext()->setContextProperty(QStringLiteral("mirage"), &controller);
    const QUrl url(QStringLiteral("qrc:/MirageQt/Main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
                     [url](QObject* object, const QUrl& objectUrl) {
                         if (!object && objectUrl == url) QCoreApplication::exit(-1);
                     }, Qt::QueuedConnection);
    engine.load(url);

    Mirage::StatusBar statusBar(&engine, &controller, &app);
    app.setQuitOnLastWindowClosed(!statusBar.start());
    return app.exec();
}
