#include "Services/SteamSetupViewModel.h"

#include "Services/SteamCMDManager.h"

#include <QClipboard>
#include <QGuiApplication>

namespace Mirage {

namespace {

QString steamInstallStateKey(SteamCMDInstallState state) {
    switch (state) {
    case SteamCMDInstallState::Detecting: return QStringLiteral("detecting");
    case SteamCMDInstallState::Found: return QStringLiteral("found");
    case SteamCMDInstallState::NotFound: return QStringLiteral("notFound");
    case SteamCMDInstallState::Downloading: return QStringLiteral("downloading");
    case SteamCMDInstallState::Extracting: return QStringLiteral("extracting");
    case SteamCMDInstallState::Initializing: return QStringLiteral("initializing");
    case SteamCMDInstallState::Installed: return QStringLiteral("installed");
    case SteamCMDInstallState::Failed: return QStringLiteral("failed");
    }
    return {};
}

QString steamLoginStateKey(SteamLoginState state) {
    switch (state) {
    case SteamLoginState::Idle: return QStringLiteral("idle");
    case SteamLoginState::LoggingIn: return QStringLiteral("loggingIn");
    case SteamLoginState::WaitingForGuard: return QStringLiteral("waitingForGuard");
    case SteamLoginState::Success: return QStringLiteral("success");
    case SteamLoginState::Failed: return QStringLiteral("failed");
    }
    return {};
}

QString steamGuardTypeKey(SteamGuardType type) {
    switch (type) {
    case SteamGuardType::None: return QStringLiteral("");
    case SteamGuardType::Email: return QStringLiteral("email");
    case SteamGuardType::Mobile: return QStringLiteral("mobile");
    case SteamGuardType::MobileConfirm: return QStringLiteral("mobileConfirm");
    }
    return {};
}

} // namespace

SteamSetupViewModel::SteamSetupViewModel(SteamCMDManager* cmd, QObject* parent)
    : QObject(parent)
    , m_cmd(cmd) {
    connect(m_cmd, &SteamCMDManager::installStateChanged, this,
            [this](SteamCMDInstallState state, double progress, const QString& message) {
                m_installState = steamInstallStateKey(state);
                m_installProgress = progress;
                m_installMessage = message;
                emit steamChanged();
            });
    connect(m_cmd, &SteamCMDManager::loginStateChanged, this,
            [this](SteamLoginState state, const QString& message) {
                m_loginState = steamLoginStateKey(state);
                m_loginMessage = message;
                emit steamChanged();
            });
    connect(m_cmd, &SteamCMDManager::guardTypeChanged, this,
            [this](SteamGuardType) { emit steamChanged(); });
    connect(m_cmd, &SteamCMDManager::diagnosticEvent, this,
            [this](const QString& line) {
                m_loginLog.append(line);
                if (m_loginLog.size() > 500)
                    m_loginLog.remove(0, m_loginLog.size() - 500);
                emit steamChanged();
            });
    connect(m_cmd, &SteamCMDManager::steamCMDPathChanged, this,
            [this](const QString&) { emit steamChanged(); });
    connect(m_cmd, &SteamCMDManager::authenticationChanged, this,
            [this](bool, const QString&) { emit steamChanged(); });
    connect(m_cmd, &SteamCMDManager::sessionReusableChanged, this,
            [this](bool) { emit steamChanged(); });
}

QString SteamSetupViewModel::installState() const { return m_installState; }
double SteamSetupViewModel::installProgress() const { return m_installProgress; }
QString SteamSetupViewModel::installMessage() const { return m_installMessage; }
QString SteamSetupViewModel::loginState() const { return m_loginState; }
QString SteamSetupViewModel::loginMessage() const { return m_loginMessage; }
QStringList SteamSetupViewModel::loginLog() const { return m_loginLog; }
QString SteamSetupViewModel::guardType() const { return steamGuardTypeKey(m_cmd->steamGuardType()); }
bool SteamSetupViewModel::sessionReusable() const { return m_cmd->sessionReusable(); }

void SteamSetupViewModel::detect() {
    m_installState = QStringLiteral("detecting");
    emit steamChanged();
    m_cmd->detectSteamCMD();
}

void SteamSetupViewModel::install() {
    m_cmd->installSteamCMD();
}

void SteamSetupViewModel::cancelInstallation() {
    m_cmd->cancelInstallation();
}

void SteamSetupViewModel::login(const QString& username, const QString& password) {
    m_loginLog.clear();
    emit steamChanged();
    m_cmd->login(username, password);
}

void SteamSetupViewModel::submitGuardCode(const QString& code) {
    m_cmd->submitGuardCode(code);
}

void SteamSetupViewModel::confirmMobileLogin() {
    m_cmd->confirmMobileLogin();
}

void SteamSetupViewModel::useSavedSession() {
    m_loginState = QStringLiteral("loggingIn");
    m_loginMessage = QStringLiteral("正在验证已保存的 SteamCMD 会话");
    emit steamChanged();
    m_cmd->refreshSession();
}

void SteamSetupViewModel::cancelLogin() {
    m_cmd->cancelLogin();
}

void SteamSetupViewModel::cancelPendingWork() {
    m_cmd->cancelLogin();
    m_cmd->cancelInstallation();
}

void SteamSetupViewModel::logout() {
    m_cmd->logout();
    m_loginState = QStringLiteral("idle");
    m_loginMessage = QStringLiteral("未登录");
    emit steamChanged();
}

void SteamSetupViewModel::copyLoginLog() {
    QGuiApplication::clipboard()->setText(m_loginLog.join(QStringLiteral("\n")));
}

} // namespace Mirage
