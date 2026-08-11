#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace Mirage {

class SteamCMDManager;

// SteamCMD 安装/登录流程的视图状态层：对应 macOS SteamSetup/SteamSetupViewModel.swift。
// 持有安装/登录状态的字符串缓存（供 QML 直接展示），监听 SteamCMDManager 的
// 信号刷新缓存，并向其转发用户操作。由 MirageController 聚合为 QML 的
// mirage.steam* 门面。
class SteamSetupViewModel : public QObject {
    Q_OBJECT
public:
    explicit SteamSetupViewModel(SteamCMDManager* cmd, QObject* parent = nullptr);

    QString installState() const;
    double installProgress() const;
    QString installMessage() const;
    QString loginState() const;
    QString loginMessage() const;
    QStringList loginLog() const;
    QString guardType() const;
    bool sessionReusable() const;

    Q_INVOKABLE void detect();
    Q_INVOKABLE void install();
    Q_INVOKABLE void cancelInstallation();
    Q_INVOKABLE void login(const QString& username, const QString& password);
    Q_INVOKABLE void submitGuardCode(const QString& code);
    Q_INVOKABLE void confirmMobileLogin();
    Q_INVOKABLE void useSavedSession();
    Q_INVOKABLE void cancelLogin();
    Q_INVOKABLE void cancelPendingWork();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void copyLoginLog();

signals:
    // 任一缓存状态变化（安装/登录/日志/守卫/会话）。
    void steamChanged();

private:
    SteamCMDManager* m_cmd;
    QString m_installState = QStringLiteral("detecting");
    double m_installProgress = 0.0;
    QString m_installMessage;
    QString m_loginState = QStringLiteral("idle");
    QString m_loginMessage;
    QStringList m_loginLog;
};

} // namespace Mirage
