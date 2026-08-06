#include "Services/DisplayBrokerService.h"
#include "Services/MirageController.h"

#include "FluentUI.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDebug>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QUrl>
#include <QWindow>
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

    QSystemTrayIcon tray(QIcon(QStringLiteral(":/appicon.png")), &app);
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        auto* menu = new QMenu;
        auto* openAction = menu->addAction(QStringLiteral("打开 Mirage"));
        QObject::connect(openAction, &QAction::triggered, &app, [&engine] {
            if (engine.rootObjects().isEmpty()) return;
            auto* window = qobject_cast<QWindow*>(engine.rootObjects().constFirst());
            if (!window) return;
            window->show();
            window->raise();
            window->requestActivate();
        });
        auto* importAction = menu->addAction(QStringLiteral("导入壁纸…"));
        QObject::connect(importAction, &QAction::triggered, &app, [&engine] {
            if (engine.rootObjects().isEmpty()) return;
            auto* window = qobject_cast<QWindow*>(engine.rootObjects().constFirst());
            if (!window) return;
            window->show();
            window->raise();
            window->requestActivate();
            QMetaObject::invokeMethod(window, "openImportPanel", Qt::QueuedConnection);
        });
        menu->addSeparator();
        auto* settingsAction = menu->addAction(QStringLiteral("设置…"));
        QObject::connect(settingsAction, &QAction::triggered, &app, [&engine] {
            if (engine.rootObjects().isEmpty()) return;
            auto* window = qobject_cast<QWindow*>(engine.rootObjects().constFirst());
            if (!window) return;
            window->show();
            window->raise();
            window->requestActivate();
            QMetaObject::invokeMethod(window, "openSettingsPanel", Qt::QueuedConnection);
        });
        auto* updateAction = menu->addAction(
            QStringLiteral("检查更新…（TODO：Linux 更新服务）"));
        updateAction->setEnabled(false);
        auto* projectAction = menu->addAction(QStringLiteral("项目主页"));
        QObject::connect(projectAction, &QAction::triggered, &app, [] {
            QDesktopServices::openUrl(
                QUrl(QStringLiteral("https://github.com/laobamac/MirageWallpaper")));
        });
        menu->addSeparator();
        menu->addAction(QStringLiteral("暂停"), &controller,
                        &Mirage::MirageController::pauseWallpapers);
        menu->addAction(QStringLiteral("继续"), &controller,
                        &Mirage::MirageController::resumeWallpapers);
        menu->addAction(QStringLiteral("静音"), &controller,
                        &Mirage::MirageController::muteWallpapers);
        menu->addAction(QStringLiteral("取消静音"), &controller,
                        &Mirage::MirageController::unmuteWallpapers);
        auto* allScreensAction = menu->addAction(QStringLiteral("覆盖到所有显示器"));
        QObject::connect(allScreensAction, &QAction::triggered, &app,
                         [&controller] { controller.applySelected(true); });
        menu->addAction(QStringLiteral("重新渲染当前壁纸"), &controller,
                        &Mirage::MirageController::reloadCurrentWallpaper);
        menu->addAction(QStringLiteral("停止壁纸"), &controller,
                        &Mirage::MirageController::stopWallpapers);
        menu->addAction(QStringLiteral("重置所有已信任壁纸"), &controller,
                        &Mirage::MirageController::resetTrustedWallpapers);
        menu->addSeparator();
        menu->addAction(QStringLiteral("退出 Mirage"), &app, &QCoreApplication::quit);
        tray.setContextMenu(menu);
        tray.show();
    }
    return app.exec();
}
