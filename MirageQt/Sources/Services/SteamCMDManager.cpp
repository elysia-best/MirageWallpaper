#include "Services/SteamCMDManager.h"

#include "Services/Paths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QStorageInfo>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <functional>
#include <vector>

namespace Mirage {
namespace {

const char* kBootstrapUrl = "https://steamcdn-a.akamaihd.net/client/installer/steamcmd_linux.tar.gz";
constexpr qint64 kMinimumInstallSpace = 150LL * 1024 * 1024;
constexpr int kLoginTimeoutMs = 5 * 60 * 1000;

QString statePath() {
    return Paths::configDir() + "/steamcmd.json";
}

QString markerPath() {
    return Paths::steamCMDDir() + "/.mirage-ready";
}

QString archivePath() {
    return Paths::steamCMDDir() + "/steamcmd_linux.tar.gz";
}

QJsonObject readState() {
    QFile file(statePath());
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

void writeState(const QString& path, const QString& username) {
    QSaveFile file(statePath());
    if (!file.open(QIODevice::WriteOnly)) return;
    QJsonObject object;
    object[QStringLiteral("path")] = path;
    object[QStringLiteral("username")] = username;
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    file.commit();
}

bool containsAny(const QString& haystack, const QStringList& needles) {
    const QString lower = haystack.toLower();
    for (const QString& needle : needles) {
        if (lower.contains(needle.toLower())) return true;
    }
    return false;
}

QString normalizedSteamOutput(QString text) {
    static const QRegularExpression ansi(QStringLiteral("\\x1b\\[[0-?]*[ -/]*[@-~]"));
    text.remove(ansi);
    text.replace(QChar('\r'), QChar('\n'));
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return text.trimmed().toLower();
}

bool isValidWorkshopId(const QString& id) {
    static const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
    return digits.match(id).hasMatch();
}

QString shellOutput(QProcess& process) {
    return QString::fromUtf8(process.readAllStandardError() + process.readAllStandardOutput()).trimmed();
}

} // namespace

class SteamCMDManager::PtySession final : public QObject {
public:
    explicit PtySession(QObject* parent = nullptr)
        : QObject(parent) {}

    ~PtySession() override {
        if (m_pid > 0) {
            ::kill(m_pid, SIGKILL);
            int status = 0;
            ::waitpid(m_pid, &status, WNOHANG);
        }
        if (m_notifier) m_notifier->setEnabled(false);
        if (m_fd >= 0) ::close(m_fd);
    }

    std::function<void(const QByteArray&)> onData;
    std::function<void(int, bool)> onFinished;

    bool start(const QString& program, const QStringList& arguments, const QString& workingDirectory,
               const QString& homeDirectory, QString* error) {
        if (m_pid > 0) return false;

        int master = -1;
        const pid_t pid = ::forkpty(&master, nullptr, nullptr, nullptr);
        if (pid < 0) {
            if (error) *error = QString::fromLocal8Bit(std::strerror(errno));
            return false;
        }

        if (pid == 0) {
            ::chdir(workingDirectory.toLocal8Bit().constData());
            ::setenv("HOME", homeDirectory.toLocal8Bit().constData(), 1);
            ::setenv("STEAMEXE", program.toLocal8Bit().constData(), 1);
            termios attributes{};
            if (::tcgetattr(STDIN_FILENO, &attributes) == 0) {
                attributes.c_lflag &= ~tcflag_t(ECHO | ECHONL);
                ::tcsetattr(STDIN_FILENO, TCSANOW, &attributes);
            }

            std::vector<QByteArray> args;
            args.reserve(arguments.size() + 1);
            args.push_back(program.toLocal8Bit());
            for (const QString& argument : arguments) args.push_back(argument.toLocal8Bit());
            std::vector<char*> argv;
            argv.reserve(args.size() + 1);
            for (QByteArray& argument : args) argv.push_back(argument.data());
            argv.push_back(nullptr);

            if (program.endsWith(QStringLiteral(".sh"))) {
                std::vector<char*> bashArgv;
                bashArgv.reserve(argv.size() + 2);
                bashArgv.push_back(const_cast<char*>("/bin/bash"));
                for (char* argument : argv) bashArgv.push_back(argument);
                ::execv("/bin/bash", bashArgv.data());
            } else {
                ::execv(program.toLocal8Bit().constData(), argv.data());
            }
            _exit(127);
        }

        m_pid = pid;
        m_fd = master;
        const int flags = ::fcntl(m_fd, F_GETFL, 0);
        ::fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);
        m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this, [this](auto) { readAvailable(); });
        return true;
    }

    bool isRunning() const {
        return m_pid > 0 && !m_finished;
    }

    bool write(const QByteArray& data) {
        if (m_fd < 0 || !isRunning()) return false;
        qsizetype offset = 0;
        while (offset < data.size()) {
            const ssize_t result = ::write(m_fd, data.constData() + offset, size_t(data.size() - offset));
            if (result > 0) {
                offset += result;
                continue;
            }
            if (result < 0 && errno == EINTR) continue;
            return false;
        }
        return true;
    }

    void terminate() {
        if (m_pid <= 0 || m_finished) return;
        ::kill(m_pid, SIGTERM);
        const pid_t pid = m_pid;
        QTimer::singleShot(5000, this, [pid] {
            if (::kill(pid, 0) == 0) ::kill(pid, SIGKILL);
        });
    }

private:
    void readAvailable() {
        QByteArray data;
        char buffer[8192];
        while (true) {
            const ssize_t count = ::read(m_fd, buffer, sizeof(buffer));
            if (count > 0) {
                data.append(buffer, int(count));
                continue;
            }
            if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) break;
            if (count == 0 || (count < 0 && errno == EIO)) {
                if (m_notifier) m_notifier->setEnabled(false);
                finish();
            }
            break;
        }
        if (!data.isEmpty() && onData) onData(data);
        if (m_pid > 0 && !m_finished) {
            int status = 0;
            if (::waitpid(m_pid, &status, WNOHANG) == m_pid) finishStatus(status);
        }
    }

    void finish() {
        if (m_finished) return;
        int status = 0;
        const pid_t result = ::waitpid(m_pid, &status, WNOHANG);
        if (result == 0) return;
        if (result < 0 && errno != ECHILD) return;
        finishStatus(status);
    }

    void finishStatus(int status) {
        if (m_finished) return;
        m_finished = true;
        const int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        const bool normal = WIFEXITED(status);
        if (m_notifier) m_notifier->setEnabled(false);
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }
        m_pid = -1;
        if (onFinished) onFinished(exitCode, normal);
    }

    pid_t m_pid = -1;
    int m_fd = -1;
    bool m_finished = false;
    QSocketNotifier* m_notifier = nullptr;
};

SteamCMDManager::SteamCMDManager(QObject* parent)
    : QObject(parent) {
    qRegisterMetaType<Mirage::SteamCMDInstallState>();
    qRegisterMetaType<Mirage::SteamGuardType>();
    qRegisterMetaType<Mirage::SteamLoginState>();
    qRegisterMetaType<Mirage::DownloadState>();

    m_loginTimeout = new QTimer(this);
    m_loginTimeout->setSingleShot(true);
    connect(m_loginTimeout, &QTimer::timeout, this, [this] {
        if (!m_loginFlow) return;
        record(QStringLiteral("Steam 登录"), QStringLiteral("登录超时"), {m_loginUsername, m_loginPassword});
        m_loginCancelled = true;
        failLogin(QStringLiteral("Steam 登录长时间无响应，请检查网络后重试"));
    });

    const QJsonObject state = readState();
    m_savedUsername = state.value(QStringLiteral("username")).toString();
    const QString storedPath = state.value(QStringLiteral("path")).toString();
    if (!storedPath.isEmpty() && isReadyLauncher(preferredLauncher(storedPath))) {
        m_steamCMDPath = preferredLauncher(storedPath);
    }
    m_loggedIn = !m_savedUsername.isEmpty() && hasSessionArtifacts();
}

SteamCMDManager::~SteamCMDManager() {
    if (m_installReply) m_installReply->abort();
    if (m_installProcess) m_installProcess->kill();
    terminateSession();
}

QString SteamCMDManager::steamCMDPath() const { return m_steamCMDPath; }
QString SteamCMDManager::savedUsername() const { return m_savedUsername; }
bool SteamCMDManager::isLoggedIn() const { return m_loggedIn; }
SteamGuardType SteamCMDManager::steamGuardType() const { return m_guardType; }
bool SteamCMDManager::sessionReusable() const { return m_loggedIn && !m_savedUsername.isEmpty(); }
QStringList SteamCMDManager::diagnosticEvents() const { return m_diagnosticEvents; }

QString SteamCMDManager::detectSteamCMD() {
    emit installStateChanged(SteamCMDInstallState::Detecting, 0.0, QStringLiteral("检测 SteamCMD"));

    if (!m_steamCMDPath.isEmpty() && isReadyLauncher(m_steamCMDPath)) {
        emit installStateChanged(SteamCMDInstallState::Found, 1.0, m_steamCMDPath);
        return m_steamCMDPath;
    }

    const QStringList candidates = {
        Paths::steamCMDDir() + "/steamcmd.sh",
        QStandardPaths::findExecutable(QStringLiteral("steamcmd")),
        QDir::homePath() + "/steamcmd/steamcmd.sh",
        QStringLiteral("/usr/bin/steamcmd"),
        QStringLiteral("/usr/local/bin/steamcmd"),
    };

    for (const QString& candidate : candidates) {
        if (candidate.isEmpty()) continue;
        const QString launcher = preferredLauncher(candidate);
        if (isReadyLauncher(launcher)) {
            setSteamCMDPath(launcher);
            emit installStateChanged(SteamCMDInstallState::Found, 1.0, launcher);
            return launcher;
        }
    }

    emit installStateChanged(SteamCMDInstallState::NotFound, 0.0, QStringLiteral("未找到 SteamCMD"));
    return {};
}

void SteamCMDManager::installSteamCMD() {
    if (m_installReply || m_installProcess || m_session) {
        emit installStateChanged(SteamCMDInstallState::Failed, 0.0, QStringLiteral("SteamCMD 正在执行其他任务"));
        return;
    }
    const QStorageInfo storage(Paths::steamCMDDir());
    if (storage.isValid() && storage.bytesAvailable() < kMinimumInstallSpace) {
        emit installStateChanged(SteamCMDInstallState::Failed, 0.0, QStringLiteral("可用磁盘空间不足，请至少预留 150 MB"));
        return;
    }

    m_installCancelled = false;
    QDir().mkpath(Paths::steamCMDDir());
    QFile::remove(markerPath());
    QFile::remove(archivePath());
    record(QStringLiteral("SteamCMD 安装"), QStringLiteral("开始下载 Linux SteamCMD bootstrap"));

    QNetworkRequest request(QUrl(QString::fromLatin1(kBootstrapUrl)));
    m_installReply = m_network.get(request);
    emit installStateChanged(SteamCMDInstallState::Downloading, 0.0, QStringLiteral("下载 SteamCMD"));
    connect(m_installReply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        const double progress = total > 0 ? qBound(0.0, double(received) / double(total), 1.0) : 0.0;
        emit installStateChanged(SteamCMDInstallState::Downloading, progress, QStringLiteral("下载 SteamCMD"));
    });
    connect(m_installReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply* reply = m_installReply;
        m_installReply = nullptr;
        const QByteArray bytes = reply->readAll();
        const QString networkError = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
        reply->deleteLater();

        if (m_installCancelled) {
            emit installStateChanged(SteamCMDInstallState::Failed, 0.0, QStringLiteral("SteamCMD 安装已取消"));
            return;
        }
        if (!networkError.isEmpty() || bytes.isEmpty()) {
            const QString message = networkError.isEmpty() ? QStringLiteral("安装包为空") : networkError;
            record(QStringLiteral("SteamCMD 安装"), QStringLiteral("下载失败：%1").arg(message));
            emit installStateChanged(SteamCMDInstallState::Failed, 0.0, message);
            return;
        }

        QFile archive(archivePath());
        if (!archive.open(QIODevice::WriteOnly | QIODevice::Truncate) || archive.write(bytes) != bytes.size()) {
            emit installStateChanged(SteamCMDInstallState::Failed, 0.0, QStringLiteral("写入安装包失败"));
            return;
        }
        archive.close();

        QProcess listing;
        listing.start(QStringLiteral("/usr/bin/tar"), {QStringLiteral("-tzf"), archivePath()});
        const bool listingFinished = listing.waitForFinished(120000);
        const QByteArray listingStdout = listing.readAllStandardOutput();
        const QByteArray listingStderr = listing.readAllStandardError();
        if (!listingFinished || listing.exitStatus() != QProcess::NormalExit || listing.exitCode() != 0) {
            const QString message = QStringLiteral("SteamCMD 安装包无法校验：%1")
                                        .arg(QString::fromUtf8(listingStderr + listingStdout).trimmed().left(200));
            record(QStringLiteral("SteamCMD 安装"), message);
            emit installStateChanged(SteamCMDInstallState::Failed, 0.0, message);
            return;
        }
        const QStringList entries = QString::fromUtf8(listingStdout).split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
        bool hasLauncher = false;
        bool hasScript = false;
        for (QString entry : entries) {
            entry = entry.trimmed();
            while (entry.startsWith(QStringLiteral("./"))) entry.remove(0, 2);
            const QStringList parts = entry.split('/', Qt::SkipEmptyParts);
            if (entry.startsWith('/') || parts.contains(QStringLiteral(".."))) {
                emit installStateChanged(SteamCMDInstallState::Failed, 0.0, QStringLiteral("SteamCMD 安装包内容异常"));
                return;
            }
            hasLauncher |= entry == QStringLiteral("steamcmd") || entry.endsWith(QStringLiteral("/steamcmd"));
            hasScript |= entry == QStringLiteral("steamcmd.sh") || entry.endsWith(QStringLiteral("/steamcmd.sh"));
        }
        if (!hasLauncher || !hasScript) {
            emit installStateChanged(SteamCMDInstallState::Failed, 0.0, QStringLiteral("SteamCMD 安装包内容异常"));
            return;
        }

        emit installStateChanged(SteamCMDInstallState::Extracting, 0.0, QStringLiteral("解压 SteamCMD"));
        auto* tar = new QProcess(this);
        m_installProcess = tar;
        tar->setProgram(QStringLiteral("/usr/bin/tar"));
        tar->setArguments({QStringLiteral("-xzf"), archivePath(), QStringLiteral("-C"), Paths::steamCMDDir()});
        connect(tar, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, tar](int exitCode, QProcess::ExitStatus status) {
                    m_installProcess = nullptr;
                    const QString output = shellOutput(*tar);
                    tar->deleteLater();
                    if (m_installCancelled) {
                        emit installStateChanged(SteamCMDInstallState::Failed, 0.0, QStringLiteral("SteamCMD 安装已取消"));
                        return;
                    }
                    if (status != QProcess::NormalExit || exitCode != 0) {
                        emit installStateChanged(SteamCMDInstallState::Failed, 0.0, QStringLiteral("解压失败：%1").arg(output.left(200)));
                        return;
                    }

                    const QString launcher = Paths::steamCMDDir() + "/steamcmd.sh";
                    QFile::setPermissions(launcher, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                                                    QFileDevice::ReadGroup | QFileDevice::ExeGroup);
                    if (!isUsableLauncher(launcher)) {
                        emit installStateChanged(SteamCMDInstallState::Failed, 0.0, QStringLiteral("解压后未找到 steamcmd.sh"));
                        return;
                    }
                    emit installStateChanged(SteamCMDInstallState::Initializing, 0.0, QStringLiteral("初始化 SteamCMD"));
                    auto* init = new QProcess(this);
                    m_installProcess = init;
                    init->setProgram(QStringLiteral("/bin/bash"));
                    init->setArguments({launcher, QStringLiteral("+quit")});
                    init->setWorkingDirectory(Paths::steamCMDDir());
                    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
                    env.insert(QStringLiteral("HOME"), Paths::steamCMDDir() + "/home");
                    init->setProcessEnvironment(env);
                    connect(init, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                            [this, init, launcher](int initCode, QProcess::ExitStatus initStatus) {
                                m_installProcess = nullptr;
                                const QString output = shellOutput(*init);
                                init->deleteLater();
                                record(QStringLiteral("SteamCMD 安装"), output);
                                if (m_installCancelled) {
                                    emit installStateChanged(SteamCMDInstallState::Failed, 0.0, QStringLiteral("SteamCMD 安装已取消"));
                                    return;
                                }
                                if (initStatus != QProcess::NormalExit || initCode != 0) {
                                    emit installStateChanged(SteamCMDInstallState::Failed, 0.0, QStringLiteral("SteamCMD 初始化失败"));
                                    return;
                                }
                                QFile marker(markerPath());
                                if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) marker.write("ready");
                                setSteamCMDPath(launcher);
                                QFile::remove(archivePath());
                                emit installStateChanged(SteamCMDInstallState::Installed, 1.0, launcher);
                            });
                    init->start();
                });
        tar->start();
    });
}

void SteamCMDManager::cancelInstallation() {
    m_installCancelled = true;
    if (m_installReply) m_installReply->abort();
    if (m_installProcess) m_installProcess->kill();
}

void SteamCMDManager::login(const QString& username, const QString& password) {
    const QString account = username.trimmed();
    if (account.isEmpty() || password.isEmpty()) {
        emit loginStateChanged(SteamLoginState::Failed, QStringLiteral("请输入用户名和密码"));
        return;
    }
    if (m_steamCMDPath.isEmpty() && detectSteamCMD().isEmpty()) {
        emit loginStateChanged(SteamLoginState::Failed, QStringLiteral("SteamCMD 未安装"));
        return;
    }
    if (m_session && m_session->isRunning()) {
        emit loginStateChanged(SteamLoginState::Failed, QStringLiteral("SteamCMD 正在执行其他任务"));
        return;
    }

    terminateSession();
    m_loginCancelled = false;
    m_loginUsername = account;
    m_loginPassword = password;
    m_loginFlow = true;
    m_passwordSent = false;
    m_sessionOutput.clear();
    m_pendingLine.clear();
    m_guardWaiting = false;
    setGuardType(SteamGuardType::None);
    emit loginStateChanged(SteamLoginState::LoggingIn, QStringLiteral("正在登录 Steam"));
    record(QStringLiteral("Steam 登录"), QStringLiteral("开始 SteamCMD 登录（密码不会写入命令行或日志）"), {account, password});
    startSession(account, true);
    if (m_session) m_loginTimeout->start(kLoginTimeoutMs);
}

void SteamCMDManager::submitGuardCode(const QString& code) {
    const QString trimmed = code.trimmed();
    if (m_guardType == SteamGuardType::MobileConfirm) {
        confirmMobileLogin();
        return;
    }
    if (trimmed.isEmpty()) return;
    if (!m_session || !m_session->isRunning() || !m_guardWaiting) {
        emit loginStateChanged(SteamLoginState::Failed, QStringLiteral("Steam Guard 会话已结束，请重新登录"));
        return;
    }
    if (!m_session->write((trimmed + QLatin1Char('\n')).toUtf8())) {
        failLogin(QStringLiteral("提交 Steam Guard 验证码失败"));
        return;
    }
    m_guardWaiting = false;
    emit loginStateChanged(SteamLoginState::LoggingIn, QStringLiteral("正在验证 Steam Guard 验证码"));
    record(QStringLiteral("Steam 登录"), QStringLiteral("已安全提交 Steam Guard 验证码"));
}

void SteamCMDManager::confirmMobileLogin() {
    if (m_guardType != SteamGuardType::MobileConfirm || !m_session || !m_session->isRunning()) return;
    m_guardWaiting = false;
    emit loginStateChanged(SteamLoginState::LoggingIn, QStringLiteral("等待手机确认登录"));
    record(QStringLiteral("Steam 登录"), QStringLiteral("等待 Steam 手机确认"));
}

void SteamCMDManager::cancelLogin() {
    if (!m_loginFlow) return;
    m_loginCancelled = true;
    record(QStringLiteral("Steam 登录"), QStringLiteral("登录已取消"));
    failLogin(QStringLiteral("登录已取消"));
}

void SteamCMDManager::logout() {
    cancelAllDownloads();
    m_loginCancelled = true;
    terminateSession();
    m_loginFlow = false;
    m_loginUsername.clear();
    m_loginPassword.clear();
    m_savedUsername.clear();
    m_loggedIn = false;
    m_sessionReady = false;
    setGuardType(SteamGuardType::None);
    clearLocalSessionArtifacts();
    writeState(m_steamCMDPath, {});
    emit authenticationChanged(false, QStringLiteral("未登录"));
    emit sessionReusableChanged(false);
}

void SteamCMDManager::refreshSession() {
    if (!m_loggedIn || m_savedUsername.isEmpty() || m_steamCMDPath.isEmpty()) return;
    if (m_session && m_session->isRunning()) {
        if (m_sessionReady)
            emit loginStateChanged(SteamLoginState::Success, QStringLiteral("已使用验证有效的本机 SteamCMD 会话"));
        return;
    }
    m_sessionOutput.clear();
    m_pendingLine.clear();
    m_loginFlow = false;
    m_passwordSent = true;
    startSession(m_savedUsername, false);
}

void SteamCMDManager::downloadItem(const QString& workshopId) {
    if (!isValidWorkshopId(workshopId)) return;
    if (m_steamCMDPath.isEmpty() && detectSteamCMD().isEmpty()) {
        publishDownloadState(workshopId, DownloadStateKind::Failed, -1.0, QStringLiteral("SteamCMD 未安装"));
        return;
    }
    if (!m_loggedIn || m_savedUsername.isEmpty()) {
        publishDownloadState(workshopId, DownloadStateKind::Failed, -1.0, QStringLiteral("Steam 会话未验证，请重新登录后再下载"));
        return;
    }
    if (m_activeDownload == workshopId || m_downloadQueue.contains(workshopId)) {
        publishDownloadState(workshopId, DownloadStateKind::Failed, -1.0, QStringLiteral("该项目已在下载"));
        return;
    }
    m_downloadQueue.append(workshopId);
    publishDownloadState(workshopId, DownloadStateKind::Queued, -1.0, QStringLiteral("等待 SteamCMD 按顺序下载"));
    startNextDownload();
}

void SteamCMDManager::cancelDownload(const QString& workshopId) {
    if (m_activeDownload == workshopId) {
        m_cancelledDownloads.insert(workshopId);
        terminateSession();
        finishDownload(false, QStringLiteral("已取消"));
        return;
    }
    if (m_downloadQueue.removeOne(workshopId)) {
        publishDownloadState(workshopId, DownloadStateKind::Cancelled, -1.0, QStringLiteral("已取消"));
    }
}

void SteamCMDManager::cancelAllDownloads() {
    const QStringList queued = m_downloadQueue;
    m_downloadQueue.clear();
    if (!m_activeDownload.isEmpty()) {
        m_cancelledDownloads.insert(m_activeDownload);
        terminateSession();
        finishDownload(false, QStringLiteral("已取消"));
    }
    for (const QString& id : queued) publishDownloadState(id, DownloadStateKind::Cancelled, -1.0, QStringLiteral("已取消"));
    if (m_session && !m_loginFlow) terminateSession();
}

void SteamCMDManager::setSteamCMDPath(const QString& path) {
    const QString clean = QDir::cleanPath(path);
    if (m_steamCMDPath == clean) return;
    m_steamCMDPath = clean;
    writeState(m_steamCMDPath, m_savedUsername);
    emit steamCMDPathChanged(m_steamCMDPath);
}

bool SteamCMDManager::isUsableLauncher(const QString& path) const {
    const QFileInfo info(path);
    return info.exists() && info.isFile() && (info.isExecutable() || path.endsWith(QStringLiteral(".sh")));
}

bool SteamCMDManager::isReadyLauncher(const QString& path) const {
    if (!isUsableLauncher(path)) return false;
    const QString clean = QDir::cleanPath(path);
    const QString managedRoot = QDir::cleanPath(Paths::steamCMDDir()) + QLatin1Char('/');
    return !clean.startsWith(managedRoot) || QFileInfo::exists(markerPath());
}

QString SteamCMDManager::preferredLauncher(const QString& path) const {
    if (QFileInfo(path).fileName() == QStringLiteral("steamcmd")) {
        const QString script = QFileInfo(path).absolutePath() + "/steamcmd.sh";
        if (QFileInfo::exists(script)) return script;
    }
    return path;
}

void SteamCMDManager::record(const QString& category, const QString& message, const QStringList& secrets) {
    const QString safe = redact(message, secrets).trimmed();
    if (safe.isEmpty()) return;
    const QString line = QStringLiteral("[%1] [%2] %3")
                             .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), category, safe);
    m_diagnosticEvents.append(line);
    if (m_diagnosticEvents.size() > 500) m_diagnosticEvents.remove(0, m_diagnosticEvents.size() - 500);
    emit diagnosticEvent(line);
}

QString SteamCMDManager::redact(QString text, const QStringList& secrets) const {
    for (const QString& secret : secrets) {
        if (!secret.isEmpty()) text.replace(secret, QStringLiteral("[已隐藏]"));
    }
    text.remove(QRegularExpression(QStringLiteral("\\x1b\\[[0-?]*[ -/]*[@-~]")));
    const QVector<QRegularExpression> patterns = {
        QRegularExpression(QStringLiteral("(?i)(key|api[_-]?key|token|access[_-]?token|refresh[_-]?token|password)\\s*[=:]\\s*[^\\s&]+")),
        QRegularExpression(QStringLiteral("(?i)([?&](?:key|token|access_token|password)=)[^&\\s]+")),
    };
    for (const QRegularExpression& pattern : patterns) text.replace(pattern, QStringLiteral("\\1[已隐藏]"));
    return text;
}

bool SteamCMDManager::hasSessionArtifacts() const {
    const QString root = Paths::steamCMDDir();
    const QStringList candidates = {
        root + "/config/config.vdf",
        root + "/steam/config/config.vdf",
        root + "/home/Steam/config/config.vdf",
        root + "/home/.steam/steam/config/config.vdf",
        root + "/home/.local/share/Steam/config/config.vdf",
        root + "/home/.local/share/Steam/steam/config/config.vdf",
    };
    for (const QString& path : candidates) if (QFileInfo::exists(path)) return true;
    return false;
}

bool SteamCMDManager::isLoginSuccessful(const QString& output) const {
    const QString lower = normalizedSteamOutput(output);
    if (hasLoginFailure(lower)) return false;
    const bool started = lower.contains(QStringLiteral("logging in user")) ||
                         lower.contains(QStringLiteral("login user"));
    const bool completed = lower.contains(QStringLiteral("waiting for user info")) ||
                           lower.contains(QStringLiteral("waiting for client config")) ||
                           lower.contains(QStringLiteral("logged in ok")) ||
                           lower.contains(QStringLiteral("login successful")) ||
                           lower.contains(QStringLiteral("successfully logged"));
    return completed && (started || lower.contains(QStringLiteral("logged in ok")));
}

bool SteamCMDManager::hasLoginFailure(const QString& output) const {
    return containsAny(output, {
        QStringLiteral("login failure"), QStringLiteral("invalid password"),
        QStringLiteral("account logon denied"), QStringLiteral("access denied"),
        QStringLiteral("cached credentials not found"), QStringLiteral("no cached credentials"),
        QStringLiteral("two-factor code mismatch"), QStringLiteral("failed to login"),
    });
}

SteamGuardType SteamCMDManager::guardTypeFor(const QString& output) const {
    const QString lower = normalizedSteamOutput(output);
    if (containsAny(lower, {QStringLiteral("please confirm the login in the steam mobile app"),
                            QStringLiteral("confirm the login in the steam mobile"),
                            QStringLiteral("approve the login in the steam mobile")})) {
        return SteamGuardType::MobileConfirm;
    }
    if (containsAny(lower, {QStringLiteral("steam guard mobile authenticator"),
                            QStringLiteral("mobile authenticator code"),
                            QStringLiteral("steam guard code from your phone")})) {
        return SteamGuardType::Mobile;
    }
    if (containsAny(lower, {QStringLiteral("steam guard code"), QStringLiteral("two-factor code"),
                            QStringLiteral("two factor code"), QStringLiteral("code sent to your email"),
                            QStringLiteral("enter the code"), QStringLiteral("验证码")})) {
        return SteamGuardType::Email;
    }
    return SteamGuardType::None;
}

QString SteamCMDManager::guardMessage(SteamGuardType type) const {
    switch (type) {
    case SteamGuardType::Email: return QStringLiteral("请输入发送到邮箱的 Steam Guard 验证码");
    case SteamGuardType::Mobile: return QStringLiteral("请输入 Steam 手机令牌验证码");
    case SteamGuardType::MobileConfirm: return QStringLiteral("请在手机上确认登录");
    case SteamGuardType::None: break;
    }
    return {};
}

void SteamCMDManager::handleSessionData(const QByteArray& data) {
    const QString chunk = QString::fromUtf8(data);
    m_sessionOutput += chunk;
    if (m_sessionOutput.size() > 256 * 1024) m_sessionOutput.remove(0, m_sessionOutput.size() - 256 * 1024);
    m_pendingLine += chunk;

    const QStringList lines = m_pendingLine.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::KeepEmptyParts);
    m_pendingLine = lines.isEmpty() ? QString() : lines.last();
    for (int i = 0; i + 1 < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        if (line.isEmpty()) continue;
        const QString category = m_activeDownload.isEmpty() ? QStringLiteral("Steam 登录") : QStringLiteral("创意工坊下载");
        record(category, line, {m_loginUsername, m_savedUsername, m_loginPassword});
        if (!m_activeDownload.isEmpty()) handleDownloadOutput(line);
        const SteamGuardType type = guardTypeFor(line);
        if (m_loginFlow && type != SteamGuardType::None && !m_guardWaiting) {
            m_guardWaiting = true;
            setGuardType(type);
            emit loginStateChanged(SteamLoginState::WaitingForGuard, guardMessage(type));
        }
    }

    const QString prompt = normalizedSteamOutput(m_pendingLine + m_sessionOutput.right(2048));
    if (m_loginFlow && !m_passwordSent && containsAny(prompt, {QStringLiteral("password:"), QStringLiteral("password"), QStringLiteral("密码"), QStringLiteral("passwort")})) {
        m_passwordSent = m_session && m_session->write((m_loginPassword + QLatin1Char('\n')).toUtf8());
        if (!m_passwordSent) failLogin(QStringLiteral("无法提交 Steam 密码"));
    }
    if (m_loginFlow && !m_guardWaiting) {
        const SteamGuardType type = guardTypeFor(prompt);
        if (type != SteamGuardType::None) {
            m_guardWaiting = true;
            setGuardType(type);
            emit loginStateChanged(SteamLoginState::WaitingForGuard, guardMessage(type));
        }
    }
    if (hasLoginFailure(m_sessionOutput)) {
        if (m_loginFlow) failLogin(QStringLiteral("登录失败，需要重新验证"));
        else {
            m_loggedIn = false;
            m_sessionReady = false;
            emit authenticationChanged(false, QStringLiteral("Steam 缓存会话不可用，请重新登录"));
            emit loginStateChanged(SteamLoginState::Failed, QStringLiteral("Steam 缓存会话不可用，请重新登录"));
            terminateSession();
        }
        return;
    }
    if (!m_guardWaiting && isLoginSuccessful(m_sessionOutput)) {
        if (m_loginFlow) finishLoginSuccess();
        else if (!m_sessionReady) {
            m_sessionReady = true;
            emit authenticationChanged(true, QStringLiteral("Steam 会话已验证"));
            emit loginStateChanged(SteamLoginState::Success, QStringLiteral("已使用验证有效的本机 SteamCMD 会话"));
            startNextDownload();
        }
    }
}

void SteamCMDManager::failLogin(const QString& message) {
    const bool wasFlow = m_loginFlow;
    m_loginFlow = false;
    m_loginPassword.clear();
    m_passwordSent = false;
    if (m_loginTimeout) m_loginTimeout->stop();
    m_loggedIn = false;
    m_sessionReady = false;
    m_guardWaiting = false;
    setGuardType(SteamGuardType::None);
    emit authenticationChanged(false, message);
    if (wasFlow) emit loginStateChanged(SteamLoginState::Failed, message);
    terminateSession();
}

void SteamCMDManager::finishLoginSuccess() {
    if (!m_loginFlow) return;
    m_loginFlow = false;
    if (m_loginTimeout) m_loginTimeout->stop();
    m_savedUsername = m_loginUsername;
    m_loginPassword.clear();
    m_passwordSent = true;
    m_loggedIn = true;
    m_sessionReady = true;
    m_guardWaiting = false;
    setGuardType(SteamGuardType::None);
    writeState(m_steamCMDPath, m_savedUsername);
    emit sessionReusableChanged(true);
    emit authenticationChanged(true, QStringLiteral("已登录 %1").arg(m_savedUsername));
    emit loginStateChanged(SteamLoginState::Success, QStringLiteral("登录成功"));
}

void SteamCMDManager::startSession(const QString& username, bool loginFlow) {
    if (m_session) return;
    if (m_steamCMDPath.isEmpty()) {
        if (loginFlow) failLogin(QStringLiteral("SteamCMD 未安装"));
        else {
            m_loggedIn = false;
            emit authenticationChanged(false, QStringLiteral("SteamCMD 未安装"));
        }
        return;
    }
    auto* session = new PtySession(this);
    m_session = session;
    m_sessionReady = false;
    m_loginFlow = loginFlow;
    m_sessionOutput.clear();
    m_pendingLine.clear();
    const QStringList arguments = {QStringLiteral("+login"), username};
    session->onData = [this](const QByteArray& data) { handleSessionData(data); };
    session->onFinished = [this](int exitCode, bool normalExit) { handleSessionFinished(exitCode, normalExit); };
    QString error;
    if (!session->start(m_steamCMDPath, arguments, Paths::steamCMDDir(), Paths::steamCMDDir() + "/home", &error)) {
        m_session = nullptr;
        session->deleteLater();
        if (loginFlow) failLogin(QStringLiteral("启动 SteamCMD 失败：%1").arg(error));
        else {
            m_loggedIn = false;
            emit authenticationChanged(false, QStringLiteral("Steam 会话启动失败：%1").arg(error));
            if (!m_downloadQueue.isEmpty()) {
                const QString id = m_downloadQueue.takeFirst();
                publishDownloadState(id, DownloadStateKind::Failed, -1.0, QStringLiteral("Steam 会话启动失败"));
            }
        }
        return;
    }
    record(QStringLiteral("Steam 登录"), loginFlow ? QStringLiteral("SteamCMD 登录终端已启动")
                                                     : QStringLiteral("正在验证已保存 SteamCMD 会话"),
           {m_loginUsername, m_savedUsername});
}

void SteamCMDManager::startNextDownload() {
    if (!m_activeDownload.isEmpty() || m_downloadQueue.isEmpty()) return;
    if (!m_loggedIn || m_savedUsername.isEmpty()) return;
    if (!m_session || !m_session->isRunning() || !m_sessionReady) {
        if (!m_session) startSession(m_savedUsername, false);
        return;
    }

    m_activeDownload = m_downloadQueue.takeFirst();
    publishDownloadState(m_activeDownload, DownloadStateKind::Starting, -1.0, QStringLiteral("开始下载"));
    record(QStringLiteral("创意工坊下载"), QStringLiteral("开始下载项目 %1").arg(m_activeDownload), {m_savedUsername});
    if (!m_session->write(QStringLiteral("workshop_download_item 431960 %1\n").arg(m_activeDownload).toUtf8())) {
        finishDownload(false, QStringLiteral("无法向 SteamCMD 发送下载命令"));
        return;
    }
    publishDownloadState(m_activeDownload, DownloadStateKind::Downloading, -1.0, QStringLiteral("下载中"));
}

void SteamCMDManager::handleDownloadOutput(const QString& line) {
    if (m_activeDownload.isEmpty()) return;
    const QString lower = normalizedSteamOutput(line);
    if (hasLoginFailure(lower)) {
        finishDownload(false, QStringLiteral("Steam 会话不可用，请重新登录"));
        m_loggedIn = false;
        m_sessionReady = false;
        emit authenticationChanged(false, QStringLiteral("Steam 会话不可用，请重新登录"));
        terminateSession();
        return;
    }

    static const QRegularExpression percentRe(QStringLiteral("(\\d+(?:\\.\\d+)?)\\s*%"));
    const auto match = percentRe.match(line);
    if (match.hasMatch()) {
        const double percent = qBound(0.0, match.captured(1).toDouble() / 100.0, 0.99);
        publishDownloadState(m_activeDownload, DownloadStateKind::Downloading, percent, QStringLiteral("下载中"));
    }
    if (containsAny(lower, {QStringLiteral("validating"), QStringLiteral("checking files"), QStringLiteral("verifying"), QStringLiteral("校验")})) {
        publishDownloadState(m_activeDownload, DownloadStateKind::Validating, -1.0, QStringLiteral("校验中"));
    }
    if (containsAny(lower, {QStringLiteral("success. downloaded item"), QStringLiteral("success! downloaded item"),
                            QStringLiteral("downloaded item %1").arg(m_activeDownload)})) {
        finishDownload(true, QStringLiteral("下载完成"));
        return;
    }
    if (containsAny(lower, {QStringLiteral("error!"), QStringLiteral("failed downloading"),
                            QStringLiteral("download failed"), QStringLiteral("timeout"),
                            QStringLiteral("no subscription"), QStringLiteral("access denied")})) {
        finishDownload(false, QStringLiteral("SteamCMD 下载失败"));
    }
}

void SteamCMDManager::finishDownload(bool success, const QString& message) {
    if (m_activeDownload.isEmpty()) return;
    const QString id = m_activeDownload;
    m_activeDownload.clear();
    const bool cancelled = m_cancelledDownloads.remove(id) > 0;
    if (cancelled || !success) {
        publishDownloadState(id, cancelled ? DownloadStateKind::Cancelled : DownloadStateKind::Failed,
                             -1.0, cancelled ? QStringLiteral("已取消") : (message.isEmpty() ? QStringLiteral("下载失败") : message));
    } else {
        publishDownloadState(id, DownloadStateKind::Validating, -1.0, QStringLiteral("校验下载文件"));
        QString validationError;
        if (validateDownloadedItem(id, &validationError)) {
            publishDownloadState(id, DownloadStateKind::Completed, 1.0, QStringLiteral("下载完成"), 0, 0);
            record(QStringLiteral("创意工坊下载"), QStringLiteral("项目 %1 已通过 project.json 校验").arg(id), {m_savedUsername});
        } else {
            publishDownloadState(id, DownloadStateKind::Failed, -1.0,
                                 validationError.isEmpty() ? QStringLiteral("下载文件校验失败") : validationError);
        }
    }
    startNextDownload();
}

bool SteamCMDManager::validateDownloadedItem(const QString& workshopId, QString* error) const {
    if (!isValidWorkshopId(workshopId)) {
        if (error) *error = QStringLiteral("创意工坊项目 ID 无效");
        return false;
    }
    QStringList roots = Paths::steamCMDContentDirs();
    roots.append(Paths::defaultSteamWorkshopDirs());
    for (const QString& root : roots) {
        const QString directory = QDir::cleanPath(root + "/" + workshopId);
        QFile file(directory + "/project.json");
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (document.isObject() && !document.object().isEmpty()) return true;
    }
    if (error) *error = QStringLiteral("下载完成但未找到有效 project.json");
    return false;
}

void SteamCMDManager::terminateSession() {
    PtySession* session = m_session;
    if (!session) return;
    m_session = nullptr;
    m_sessionReady = false;
    session->onData = {};
    session->onFinished = [session](int, bool) { session->deleteLater(); };
    session->terminate();
}

void SteamCMDManager::clearLocalSessionArtifacts() {
    const QString root = Paths::steamCMDDir();
    const QStringList sessionDirectories = {
        root + "/config", root + "/steam/config", root + "/steam/userdata",
        root + "/home/Steam/config", root + "/home/Steam/userdata",
        root + "/home/.steam/steam/config", root + "/home/.steam/steam/userdata",
        root + "/home/.local/share/Steam/config", root + "/home/.local/share/Steam/userdata",
        root + "/home/.local/share/Steam/steam/config", root + "/home/.local/share/Steam/steam/userdata",
    };
    for (const QString& directory : sessionDirectories) QDir(directory).removeRecursively();
    for (const QString& directory : {root, root + "/steam", root + "/home/Steam",
                                     root + "/home/.steam/steam", root + "/home/.local/share/Steam"}) {
        const QDir dir(directory);
        for (const QString& file : dir.entryList({QStringLiteral("ssfn*")}, QDir::Files)) QFile::remove(dir.filePath(file));
    }
}

void SteamCMDManager::publishDownloadState(const QString& workshopId, DownloadStateKind kind, double percent,
                                            const QString& message, qint64 bytesReceived, qint64 totalBytes) {
    DownloadState state;
    state.kind = kind;
    state.percent = percent;
    state.message = message;
    state.bytesReceived = bytesReceived;
    state.totalBytes = totalBytes;
    emit downloadStateChanged(workshopId, state);
}

void SteamCMDManager::handleSessionFinished(int exitCode, bool normalExit) {
    PtySession* session = m_session;
    m_session = nullptr;
    m_sessionReady = false;
    if (m_loginTimeout) m_loginTimeout->stop();
    if (session) session->deleteLater();

    const bool cancelled = m_loginCancelled;
    m_loginCancelled = false;
    if (m_loginFlow) {
        if (cancelled) failLogin(QStringLiteral("登录已取消"));
        else if (!m_loggedIn) failLogin(normalExit && exitCode == 0 ? QStringLiteral("登录失败，需要重新验证")
                                                                    : QStringLiteral("SteamCMD 登录进程已结束"));
        return;
    }
    if (!m_activeDownload.isEmpty()) {
        finishDownload(false, cancelled ? QStringLiteral("已取消") : QStringLiteral("SteamCMD 会话已结束"));
    } else if (!m_downloadQueue.isEmpty()) {
        const QString id = m_downloadQueue.takeFirst();
        publishDownloadState(id, DownloadStateKind::Failed, -1.0, QStringLiteral("SteamCMD 会话已结束"));
    } else if (m_loggedIn) {
        m_loggedIn = false;
        emit authenticationChanged(false, QStringLiteral("Steam 会话已结束，请重新登录"));
        emit loginStateChanged(SteamLoginState::Failed, QStringLiteral("Steam 会话已结束，请重新登录"));
    }
}

void SteamCMDManager::setGuardType(SteamGuardType type) {
    if (m_guardType == type) return;
    m_guardType = type;
    emit guardTypeChanged(type);
}

} // namespace Mirage
