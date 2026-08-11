#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace Mirage {

class SteamCMDPtySession;

enum class SteamCMDInstallState {
    Detecting,
    Found,
    NotFound,
    Downloading,
    Extracting,
    Initializing,
    Installed,
    Failed,
};

enum class SteamGuardType {
    None,
    Email,
    Mobile,
    MobileConfirm,
};

enum class SteamLoginState {
    Idle,
    LoggingIn,
    WaitingForGuard,
    Success,
    Failed,
};

enum class DownloadStateKind {
    Queued,
    Starting,
    Downloading,
    Validating,
    Completed,
    Failed,
    Cancelled,
};

struct DownloadState {
    DownloadStateKind kind = DownloadStateKind::Queued;
    double percent = -1.0;
    QString message;
    qint64 bytesReceived = 0;
    qint64 totalBytes = 0;
};

class SteamCMDManager : public QObject {
    Q_OBJECT

public:
    explicit SteamCMDManager(QObject* parent = nullptr);
    ~SteamCMDManager() override;

    QString steamCMDPath() const;
    QString savedUsername() const;
    bool isLoggedIn() const;
    SteamGuardType steamGuardType() const;
    bool sessionReusable() const;
    QStringList diagnosticEvents() const;

    QString detectSteamCMD();

public slots:
    void installSteamCMD();
    void cancelInstallation();
    void login(const QString& username, const QString& password);
    void submitGuardCode(const QString& code);
    void confirmMobileLogin();
    void cancelLogin();
    void logout();
    void refreshSession();
    void downloadItem(const QString& workshopId);
    void cancelDownload(const QString& workshopId);
    void cancelAllDownloads();

signals:
    void installStateChanged(Mirage::SteamCMDInstallState state, double progress, const QString& message);
    void loginStateChanged(Mirage::SteamLoginState state, const QString& message);
    void guardTypeChanged(Mirage::SteamGuardType type);
    void downloadStateChanged(const QString& workshopId, Mirage::DownloadState state);
    void steamCMDPathChanged(const QString& path);
    void authenticationChanged(bool loggedIn, const QString& message);
    void sessionReusableChanged(bool reusable);
    void diagnosticEvent(const QString& line);

private:
    void setSteamCMDPath(const QString& path);
    bool isUsableLauncher(const QString& path) const;
    bool isReadyLauncher(const QString& path) const;
    QString preferredLauncher(const QString& path) const;
    void record(const QString& category, const QString& message, const QStringList& secrets = {});
    QString redact(QString text, const QStringList& secrets = {}) const;
    bool hasSessionArtifacts() const;
    bool isLoginSuccessful(const QString& output) const;
    bool hasLoginFailure(const QString& output) const;
    SteamGuardType guardTypeFor(const QString& output) const;
    QString guardMessage(SteamGuardType type) const;
    void setGuardType(SteamGuardType type);
    void handleSessionData(const QByteArray& data);
    void handleSessionFinished(int exitCode, bool normalExit);
    void failLogin(const QString& message);
    void finishLoginSuccess();
    void startSession(const QString& username, bool loginFlow);
    void startNextDownload();
    void handleDownloadOutput(const QString& line);
    void finishDownload(bool success, const QString& message = {});
    bool validateDownloadedItem(const QString& workshopId, QString* error = nullptr) const;
    void publishDownloadState(const QString& workshopId, DownloadStateKind kind, double percent,
                              const QString& message, qint64 bytesReceived = 0, qint64 totalBytes = 0);
    void terminateSession();
    void clearLocalSessionArtifacts();

    QString m_steamCMDPath;
    QString m_savedUsername;
    QString m_loginUsername;
    QString m_loginPassword;
    QString m_sessionOutput;
    QString m_pendingLine;
    bool m_loggedIn = false;
    bool m_sessionReady = false;
    bool m_loginFlow = false;
    bool m_passwordSent = false;
    bool m_loginCancelled = false;
    bool m_installCancelled = false;
    bool m_guardWaiting = false;
    SteamGuardType m_guardType = SteamGuardType::None;
    QTimer* m_loginTimeout = nullptr;
    QNetworkReply* m_installReply = nullptr;
    QProcess* m_installProcess = nullptr;
    SteamCMDPtySession* m_session = nullptr;
    QStringList m_downloadQueue;
    QSet<QString> m_cancelledDownloads;
    QString m_activeDownload;
    QStringList m_diagnosticEvents;
    QNetworkAccessManager m_network;
};

} // namespace Mirage

Q_DECLARE_METATYPE(Mirage::SteamCMDInstallState)
Q_DECLARE_METATYPE(Mirage::SteamGuardType)
Q_DECLARE_METATYPE(Mirage::SteamLoginState)
Q_DECLARE_METATYPE(Mirage::DownloadState)
