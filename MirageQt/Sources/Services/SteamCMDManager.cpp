#include "Services/SteamCMDManager.h"

#include "Services/Paths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

namespace Mirage {
namespace {

const char* kBootstrapUrl = "https://steamcdn-a.akamaihd.net/client/installer/steamcmd_linux.tar.gz";

QString statePath() {
    return Paths::configDir() + "/steamcmd.json";
}


QString markerPath() {
    return Paths::steamCMDDir() + "/.mirage-ready";
}

QString archivePath() {
    return Paths::steamCMDDir() + "/steamcmd_linux.tar.gz";
}

QString readTextFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll());
}

void writeState(const QString& path, const QString& username) {
    QSaveFile file(statePath());
    if (!file.open(QIODevice::WriteOnly)) return;
    QJsonObject object;
    object["path"] = path;
    object["username"] = username;
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    file.commit();
}

QJsonObject readState() {
    QFile file(statePath());
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool containsAny(const QString& haystack, const QStringList& needles) {
    const QString lower = haystack.toLower();
    for (const QString& needle : needles) {
        if (lower.contains(needle.toLower())) return true;
    }
    return false;
}

QString normalizedSteamOutput(QString text) {
    // SteamCMD mixes carriage-return progress updates and ANSI control codes
    // into its output. Normalize those before looking for protocol markers.
    static const QRegularExpression ansi(QStringLiteral("\\x1b\\[[0-?]*[ -/]*[@-~]"));
    text.remove(ansi);
    text.replace(QChar('\r'), QChar('\n'));
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return text.trimmed().toLower();
}

QString guardPromptMessage(const QString& output) {
    const QString lower = normalizedSteamOutput(output);
    if (containsAny(lower, {"steam guard code", "two-factor code", "two factor code", "enter the code", "验证码"})) {
        return QStringLiteral("请输入 Steam Guard 验证码");
    }
    if (containsAny(lower, {"please confirm the login in the steam mobile app", "confirm the login in the steam mobile"})) {
        return QStringLiteral("请在手机上确认登录");
    }
    return {};
}

} // namespace

SteamCMDManager::SteamCMDManager(QObject* parent)
    : QObject(parent) {
    qRegisterMetaType<Mirage::SteamCMDInstallState>();
    qRegisterMetaType<Mirage::SteamLoginState>();
    qRegisterMetaType<Mirage::DownloadState>();

    const QJsonObject state = readState();
    m_savedUsername = state.value("username").toString();
    const QString storedPath = state.value("path").toString();
    if (!storedPath.isEmpty() && isReadyLauncher(preferredLauncher(storedPath))) {
        m_steamCMDPath = preferredLauncher(storedPath);
    }
    m_loggedIn = !m_savedUsername.isEmpty() &&
                 hasSessionArtifacts();
}

QString SteamCMDManager::steamCMDPath() const {
    return m_steamCMDPath;
}

QString SteamCMDManager::savedUsername() const {
    return m_savedUsername;
}

bool SteamCMDManager::isLoggedIn() const {
    return m_loggedIn;
}

QString SteamCMDManager::detectSteamCMD() {
    emit installStateChanged(SteamCMDInstallState::Detecting, 0.0, QStringLiteral("检测 SteamCMD"));

    if (!m_steamCMDPath.isEmpty() && isReadyLauncher(m_steamCMDPath)) {
        emit installStateChanged(SteamCMDInstallState::Found, 1.0, m_steamCMDPath);
        return m_steamCMDPath;
    }

    const QStringList candidates = {
        Paths::steamCMDDir() + "/steamcmd.sh",
        QStandardPaths::findExecutable("steamcmd"),
        QDir::homePath() + "/steamcmd/steamcmd.sh",
        "/usr/bin/steamcmd",
        "/usr/local/bin/steamcmd",
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
    if (m_installReply || m_installProcess) {
        emit installStateChanged(SteamCMDInstallState::Failed, 0.0, QStringLiteral("SteamCMD 正在安装"));
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
        const double progress = total > 0 ? double(received) / double(total) : 0.0;
        emit installStateChanged(SteamCMDInstallState::Downloading, progress, QStringLiteral("下载 SteamCMD"));
    });

    connect(m_installReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply* reply = m_installReply;
        m_installReply = nullptr;

        if (m_installCancelled) {
            reply->deleteLater();
            emit installStateChanged(SteamCMDInstallState::Failed, 0.0, QStringLiteral("SteamCMD 安装已取消"));
            return;
        }

        const QByteArray bytes = reply->readAll();
        const QString error = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
        reply->deleteLater();
        if (!error.isEmpty() || bytes.isEmpty()) {
            record(QStringLiteral("SteamCMD 安装"), QStringLiteral("下载失败：%1").arg(error));
            emit installStateChanged(SteamCMDInstallState::Failed, 0.0, error.isEmpty() ? QStringLiteral("安装包为空") : error);
            return;
        }

        QFile archive(archivePath());
        if (!archive.open(QIODevice::WriteOnly | QIODevice::Truncate) || archive.write(bytes) != bytes.size()) {
            emit installStateChanged(SteamCMDInstallState::Failed, 0.0, QStringLiteral("写入安装包失败"));
            return;
        }
        archive.close();

        emit installStateChanged(SteamCMDInstallState::Extracting, 0.0, QStringLiteral("解压 SteamCMD"));
        auto* tar = new QProcess(this);
        m_installProcess = tar;
        tar->setProgram("/usr/bin/tar");
        tar->setArguments({"-xzf", archivePath(), "-C", Paths::steamCMDDir()});
        connect(tar, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, tar](int exitCode, QProcess::ExitStatus status) {
            m_installProcess = nullptr;
            const QString stderrText = QString::fromUtf8(tar->readAllStandardError());
            tar->deleteLater();
            if (m_installCancelled) {
                emit installStateChanged(SteamCMDInstallState::Failed, 0.0, QStringLiteral("SteamCMD 安装已取消"));
                return;
            }
            if (status != QProcess::NormalExit || exitCode != 0) {
                emit installStateChanged(SteamCMDInstallState::Failed, 0.0, QStringLiteral("解压失败：%1").arg(stderrText.left(200)));
                return;
            }

            const QString launcher = Paths::steamCMDDir() + "/steamcmd.sh";
            QFile::setPermissions(launcher, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                                            QFileDevice::ReadGroup | QFileDevice::ExeGroup);
            emit installStateChanged(SteamCMDInstallState::Initializing, 0.0, QStringLiteral("初始化 SteamCMD"));

            auto* init = new QProcess(this);
            m_installProcess = init;
            init->setProgram(launcher);
            init->setWorkingDirectory(Paths::steamCMDDir());
            init->setArguments({"+quit"});
            connect(init, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, init, launcher](int initCode, QProcess::ExitStatus initStatus) {
                m_installProcess = nullptr;
                const QString stderrText = QString::fromUtf8(init->readAllStandardError());
                const QString stdoutText = QString::fromUtf8(init->readAllStandardOutput());
                init->deleteLater();
                record(QStringLiteral("SteamCMD 安装"), redact(stdoutText + "\n" + stderrText));
                if (initStatus != QProcess::NormalExit || initCode != 0) {
                    emit installStateChanged(SteamCMDInstallState::Failed, 0.0, QStringLiteral("SteamCMD 初始化失败"));
                    return;
                }
                QFile marker(markerPath());
                if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) marker.write("ready");
                setSteamCMDPath(launcher);
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
    if (account.isEmpty()) {
        emit loginStateChanged(SteamLoginState::Failed, QStringLiteral("请输入 Steam 账户名"));
        return;
    }
    if (m_steamCMDPath.isEmpty() && detectSteamCMD().isEmpty()) {
        emit loginStateChanged(SteamLoginState::Failed, QStringLiteral("SteamCMD 未安装"));
        return;
    }
    if (m_loginProcess && m_loginProcess->state() != QProcess::NotRunning) {
        emit loginStateChanged(SteamLoginState::Failed, QStringLiteral("SteamCMD 正在登录"));
        return;
    }

    m_loginCancelled = false;
    m_loginProcess = createSteamCMDProcess({"+login", account, "+quit"}, this);
    if (!m_loginProcess) {
        emit loginStateChanged(SteamLoginState::Failed, QStringLiteral("启动 SteamCMD 失败"));
        return;
    }

    QProcess* process = m_loginProcess;
    QString* output = new QString;
    bool* passwordSent = new bool(false);
    emit loginStateChanged(SteamLoginState::LoggingIn, QStringLiteral("正在登录 Steam"));

    connect(process, &QProcess::readyReadStandardOutput, this, [this, process, account, password, output, passwordSent] {
        if (m_loginProcess != process) return;
        const QString chunk = QString::fromUtf8(process->readAllStandardOutput());
        *output += chunk;
        record(QStringLiteral("Steam 登录"), chunk, {account, password});
        if (!*passwordSent && containsAny(*output, {"password", "passwort", "密码"})) {
            process->write((password + "\n").toUtf8());
            *passwordSent = true;
        }
        if (const QString guardPrompt = guardPromptMessage(*output); !guardPrompt.isEmpty()) {
            emit loginStateChanged(SteamLoginState::WaitingForGuard, guardPrompt);
        }
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, process, account, password, output] {
        if (m_loginProcess != process) return;
        const QString chunk = QString::fromUtf8(process->readAllStandardError());
        *output += chunk;
        record(QStringLiteral("Steam 登录"), chunk, {account, password});
        if (const QString guardPrompt = guardPromptMessage(*output); !guardPrompt.isEmpty()) {
            emit loginStateChanged(SteamLoginState::WaitingForGuard, guardPrompt);
        }
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, account, output, passwordSent](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart || m_loginProcess != process) return;
                *output += QString::fromUtf8(process->readAllStandardOutput());
                *output += QString::fromUtf8(process->readAllStandardError());
                record(QStringLiteral("Steam 登录"), QStringLiteral("SteamCMD 启动失败：%1").arg(process->errorString()), {account});
                finishLoginProcess(-1, QProcess::CrashExit, account, *output);
                delete output;
                delete passwordSent;
            });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, account, output, passwordSent](int exitCode, QProcess::ExitStatus status) {
                if (m_loginProcess != process) return;
                *output += QString::fromUtf8(process->readAllStandardOutput());
                *output += QString::fromUtf8(process->readAllStandardError());
                finishLoginProcess(exitCode, status, account, *output);
                delete output;
                delete passwordSent;
            });
    process->start();
}

void SteamCMDManager::submitGuardCode(const QString& code) {
    const QString trimmed = code.trimmed();
    if (trimmed.isEmpty()) return;
    if (!m_loginProcess || m_loginProcess->state() == QProcess::NotRunning) {
        emit loginStateChanged(SteamLoginState::Failed, QStringLiteral("Steam Guard 会话已结束，请重新登录"));
        return;
    }
    m_loginProcess->write((trimmed + "\n").toUtf8());
    record(QStringLiteral("Steam 登录"), QStringLiteral("已安全提交 Steam Guard 验证码"));
    emit loginStateChanged(SteamLoginState::LoggingIn, QStringLiteral("正在验证 Steam Guard 验证码"));
}

void SteamCMDManager::cancelLogin() {
    if (m_loginProcess && m_loginProcess->state() != QProcess::NotRunning) {
        m_loginCancelled = true;
        record(QStringLiteral("Steam 登录"), QStringLiteral("登录已取消"));
        m_loginProcess->kill();
    }
}

void SteamCMDManager::logout() {
    m_savedUsername.clear();
    m_loggedIn = false;
    const QString root = Paths::steamCMDDir();
    const QStringList sessionDirectories = {
        root + "/config",
        root + "/steam/config",
        root + "/steam/userdata",
        root + "/home/Steam/config",
        root + "/home/Steam/userdata",
        root + "/home/.steam/steam/config",
        root + "/home/.steam/steam/userdata",
        root + "/home/.local/share/Steam/config",
        root + "/home/.local/share/Steam/userdata",
    };
    for (const QString& directory : sessionDirectories) QDir(directory).removeRecursively();

    for (const QString& directory : {root, root + "/steam", root + "/home/Steam",
                                     root + "/home/.steam/steam", root + "/home/.local/share/Steam"}) {
        QDir dir(directory);
        for (const QString& file : dir.entryList({QStringLiteral("ssfn*")}, QDir::Files)) {
            QFile::remove(dir.filePath(file));
        }
    }
    writeState(m_steamCMDPath, {});
    emit authenticationChanged(false, QStringLiteral("未登录"));
}

void SteamCMDManager::downloadItem(const QString& workshopId) {
    if (workshopId.isEmpty()) return;
    if (m_steamCMDPath.isEmpty() && detectSteamCMD().isEmpty()) {
        publishDownloadState(workshopId, DownloadStateKind::Failed, -1.0, QStringLiteral("SteamCMD 未安装"));
        return;
    }
    if (!m_loggedIn || m_savedUsername.isEmpty()) {
        publishDownloadState(workshopId, DownloadStateKind::Failed, -1.0, QStringLiteral("Steam 会话未验证"));
        return;
    }
    if (m_downloadProcesses.contains(workshopId)) {
        publishDownloadState(workshopId, DownloadStateKind::Failed, -1.0, QStringLiteral("该项目已在下载"));
        return;
    }

    auto* process = createSteamCMDProcess({"+login", m_savedUsername,
                                           "+workshop_download_item", "431960", workshopId,
                                           "+quit"},
                                          this);
    if (!process) {
        publishDownloadState(workshopId, DownloadStateKind::Failed, -1.0, QStringLiteral("启动 SteamCMD 失败"));
        return;
    }

    m_downloadProcesses.insert(workshopId, process);
    connect(process, &QProcess::started, this, [this, process, workshopId] {
        if (m_downloadProcesses.value(workshopId) != process) return;
        publishDownloadState(workshopId, DownloadStateKind::Starting, -1.0, QStringLiteral("开始下载"));
    });

    connect(process, &QProcess::readyReadStandardOutput, this, [this, process, workshopId] {
        if (m_downloadProcesses.value(workshopId) != process) return;
        const QString chunk = QString::fromUtf8(process->readAllStandardOutput());
        record(QStringLiteral("创意工坊下载"), chunk, {m_savedUsername});
        const QRegularExpression percentRe(QStringLiteral("(\\d+(?:\\.\\d+)?)\\s*%"));
        const auto match = percentRe.match(chunk);
        if (match.hasMatch()) {
            publishDownloadState(workshopId, DownloadStateKind::Downloading, match.captured(1).toDouble() / 100.0, QStringLiteral("下载中"));
        } else if (chunk.contains("Validating", Qt::CaseInsensitive)) {
            publishDownloadState(workshopId, DownloadStateKind::Validating, -1.0, QStringLiteral("校验中"));
        }
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, process, workshopId] {
        if (m_downloadProcesses.value(workshopId) != process) return;
        record(QStringLiteral("创意工坊下载"), QString::fromUtf8(process->readAllStandardError()), {m_savedUsername});
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, workshopId](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart || m_downloadProcesses.value(workshopId) != process) return;
                m_downloadProcesses.remove(workshopId);
                const QString message = QStringLiteral("无法启动 SteamCMD：%1").arg(process->errorString());
                process->deleteLater();
                if (m_cancelledDownloads.remove(workshopId)) {
                    publishDownloadState(workshopId, DownloadStateKind::Cancelled, -1.0, QStringLiteral("已取消"));
                } else {
                    publishDownloadState(workshopId, DownloadStateKind::Failed, -1.0, message);
                }
            });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, workshopId](int exitCode, QProcess::ExitStatus status) {
                if (m_downloadProcesses.value(workshopId) != process) return;
                m_downloadProcesses.remove(workshopId);
                process->deleteLater();
                if (m_cancelledDownloads.remove(workshopId)) {
                    publishDownloadState(workshopId, DownloadStateKind::Cancelled, -1.0, QStringLiteral("已取消"));
                } else if (status == QProcess::NormalExit && exitCode == 0) {
                    publishDownloadState(workshopId, DownloadStateKind::Completed, 1.0, QStringLiteral("下载完成"));
                } else {
                    publishDownloadState(workshopId, DownloadStateKind::Failed, -1.0, QStringLiteral("下载失败"));
                }
            });
    process->start();
}

void SteamCMDManager::cancelDownload(const QString& workshopId) {
    if (QProcess* process = m_downloadProcesses.value(workshopId)) {
        m_cancelledDownloads.insert(workshopId);
        process->kill();
    }
}

void SteamCMDManager::setSteamCMDPath(const QString& path) {
    m_steamCMDPath = QDir::cleanPath(path);
    writeState(m_steamCMDPath, m_savedUsername);
    emit steamCMDPathChanged(m_steamCMDPath);
}

bool SteamCMDManager::isUsableLauncher(const QString& path) const {
    const QFileInfo info(path);
    return info.exists() && (info.isExecutable() || path.endsWith(".sh"));
}

bool SteamCMDManager::isReadyLauncher(const QString& path) const {
    if (!isUsableLauncher(path)) return false;
    const QString clean = QDir::cleanPath(path);
    const QString managedRoot = QDir::cleanPath(Paths::steamCMDDir()) + "/";
    return !clean.startsWith(managedRoot) || QFileInfo::exists(markerPath());
}

QString SteamCMDManager::preferredLauncher(const QString& path) const {
    if (QFileInfo(path).fileName() == "steamcmd") {
        const QString script = QFileInfo(path).absolutePath() + "/steamcmd.sh";
        if (QFileInfo::exists(script)) return script;
    }
    return path;
}

void SteamCMDManager::record(const QString& category, const QString& message, const QStringList& secrets) {
    const QString safe = redact(message.trimmed(), secrets);
    if (safe.isEmpty()) return;
    const QString line = QStringLiteral("[%1] [%2] %3")
                             .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), category, safe);
    emit diagnosticEvent(line);
}

QString SteamCMDManager::redact(QString text, const QStringList& secrets) const {
    for (const QString& secret : secrets) {
        if (!secret.isEmpty()) text.replace(secret, QStringLiteral("[已隐藏]"));
    }
    const QVector<QRegularExpression> patterns = {
        QRegularExpression(QStringLiteral("(?i)(key|api[_-]?key|token|access[_-]?token|refresh[_-]?token|password)\\s*[=:]\\s*[^\\s&]+")),
        QRegularExpression(QStringLiteral("(?i)([?&](?:key|token|access_token|password)=)[^&\\s]+")),
    };
    for (const auto& re : patterns) {
        text.replace(re, QStringLiteral("\\1[已隐藏]"));
    }
    return text;
}

QProcess* SteamCMDManager::createSteamCMDProcess(const QStringList& arguments, QObject* owner) {
    if (m_steamCMDPath.isEmpty()) return nullptr;
    auto* process = new QProcess(owner);
    process->setProgram(m_steamCMDPath);
    process->setArguments(arguments);
    process->setWorkingDirectory(Paths::steamCMDDir());
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("HOME", Paths::steamCMDDir() + "/home");
    env.insert("STEAMEXE", m_steamCMDPath);
    process->setProcessEnvironment(env);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    return process;
}

bool SteamCMDManager::hasSessionArtifacts() const {
    const QString root = Paths::steamCMDDir();
    return QFileInfo::exists(root + "/config/config.vdf") ||
           QFileInfo::exists(root + "/steam/config/config.vdf") ||
           QFileInfo::exists(root + "/home/Steam/config/config.vdf") ||
           QFileInfo::exists(root + "/home/.steam/steam/config/config.vdf") ||
           QFileInfo::exists(root + "/home/.local/share/Steam/config/config.vdf");
}

bool SteamCMDManager::isLoginSuccessful(const QString& output) const {
    const QString lower = normalizedSteamOutput(output);
    const bool hasFailure = containsAny(lower, {"login failure", "invalid password", "account logon denied", "access denied"});
    if (hasFailure) return false;

    const bool loggedIn = lower.contains(QStringLiteral("logging in user"));
    const bool reachedPostLogon = lower.contains(QStringLiteral("waiting for client config")) ||
                                  lower.contains(QStringLiteral("waiting for user info")) ||
                                  lower.contains(QStringLiteral("waiting for compat in post-logon")) ||
                                  lower.contains(QStringLiteral("logged in ok")) ||
                                  lower.contains(QStringLiteral("login successful"));
    return loggedIn && reachedPostLogon;
}

void SteamCMDManager::finishLoginProcess(int exitCode, QProcess::ExitStatus status, const QString& username, QString output) {
    if (m_loginProcess) {
        m_loginProcess->deleteLater();
        m_loginProcess = nullptr;
    }

    const bool cancelled = m_loginCancelled;
    m_loginCancelled = false;
    const QString normalized = normalizedSteamOutput(output);
    const bool success = !cancelled && status == QProcess::NormalExit && exitCode == 0 &&
                         (isLoginSuccessful(output) ||
                          (hasSessionArtifacts() && !containsAny(normalized, {"login failure", "invalid password", "account logon denied", "access denied"})));
    if (success) {
        m_savedUsername = username;
        m_loggedIn = true;
        writeState(m_steamCMDPath, m_savedUsername);
        emit authenticationChanged(true, QStringLiteral("已登录 %1").arg(username));
        emit loginStateChanged(SteamLoginState::Success, QStringLiteral("登录成功"));
    } else {
        m_loggedIn = false;
        const QString message = cancelled ? QStringLiteral("登录已取消") : QStringLiteral("登录失败或需要重新验证");
        emit authenticationChanged(false, message);
        emit loginStateChanged(SteamLoginState::Failed, message);
    }
}

void SteamCMDManager::publishDownloadState(const QString& workshopId, DownloadStateKind kind, double percent, const QString& message) {
    DownloadState state;
    state.kind = kind;
    state.percent = percent;
    state.message = message;
    emit downloadStateChanged(workshopId, state);
}

} // namespace Mirage
