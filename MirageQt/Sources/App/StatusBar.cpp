#include "StatusBar.h"

#include "Services/MirageController.h"

#include <QAction>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QIcon>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QUrl>
#include <QWindow>

namespace Mirage {

StatusBar::StatusBar(QQmlApplicationEngine* engine,
                     MirageController* controller,
                     QObject* parent)
    : QObject(parent)
    , m_engine(engine)
    , m_controller(controller)
    , m_tray(QIcon(QStringLiteral(":/appicon.png")), this) {
    m_tray.setToolTip(QStringLiteral("Mirage"));
}

bool StatusBar::start() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return false;

    auto* openAction = m_menu.addAction(QStringLiteral("打开 Mirage"));
    connect(openAction, &QAction::triggered, this, &StatusBar::showMainWindow);

    auto* importAction = m_menu.addAction(QStringLiteral("导入壁纸…"));
    connect(importAction, &QAction::triggered, this, [this] {
        invokeMainWindowAction("openImportPanel");
    });

    m_menu.addSeparator();

    auto* settingsAction = m_menu.addAction(QStringLiteral("设置…"));
    connect(settingsAction, &QAction::triggered, this, [this] {
        invokeMainWindowAction("openSettingsPanel");
    });

    auto* updateAction = m_menu.addAction(QStringLiteral("检查更新…（TODO：Linux 更新服务）"));
    updateAction->setEnabled(false);

    m_menu.addSeparator();

    auto* projectAction = m_menu.addAction(QStringLiteral("项目主页"));
    connect(projectAction, &QAction::triggered, this, [] {
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("https://github.com/laobamac/MirageWallpaper")));
    });

    m_menu.addSeparator();
    m_menu.addAction(QStringLiteral("静音"), m_controller, &MirageController::muteWallpapers);
    m_menu.addAction(QStringLiteral("暂停"), m_controller, &MirageController::pauseWallpapers);

    auto* allScreensAction = m_menu.addAction(QStringLiteral("覆盖到所有显示器"));
    connect(allScreensAction, &QAction::triggered, this, [this] {
        m_controller->applySelected(true);
    });

    m_menu.addAction(QStringLiteral("停止壁纸"), m_controller, &MirageController::stopWallpapers);

    m_menu.addSeparator();
    auto* quitAction = m_menu.addAction(QStringLiteral("退出 Mirage"));
    connect(quitAction, &QAction::triggered, QCoreApplication::instance(), &QCoreApplication::quit);

    m_tray.setContextMenu(&m_menu);
    connect(&m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                    showMainWindow();
            });
    m_tray.show();
    return true;
}

QWindow* StatusBar::mainWindow() const {
    if (m_engine == nullptr || m_engine->rootObjects().isEmpty()) return nullptr;
    return qobject_cast<QWindow*>(m_engine->rootObjects().constFirst());
}

void StatusBar::showMainWindow() {
    QWindow* window = mainWindow();
    if (window == nullptr) return;

    if (window->visibility() == QWindow::Minimized)
        window->showNormal();
    else
        window->show();
    window->raise();
    window->requestActivate();
}

void StatusBar::invokeMainWindowAction(const char* action) {
    QWindow* window = mainWindow();
    if (window == nullptr) return;

    showMainWindow();
    QMetaObject::invokeMethod(window, action, Qt::QueuedConnection);
}

} // namespace Mirage
