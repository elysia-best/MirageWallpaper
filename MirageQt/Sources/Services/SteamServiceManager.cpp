#include "SteamServiceManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

namespace Mirage {

// 服务端事件 type 值（SteamService/Protocol.cs）。
static const char* kTypeResponse = "response";
static const char* kTypeAuthState = "authState";
static const char* kTypeDownloadState = "downloadState";

// 认证失败错误码（Program.cs / SteamSession.cs）。
static const char* kAuthFailedCode = "AUTH_FAILED";
static const char* kAuthCancelledCode = "AUTH_CANCELLED";

// 会话持久化的 QSettings 组名（对齐上游 keychain 账户命名：
// refresh-token:<username> 与 guard-data:<username>，用户名小写）。
static const char* kSettingsGroup = "SteamSession";

namespace {

QString settingsKey(const QString& username) {
    return username.toLower();
}

// 读取 JSON 事件里的字符串字段（缺失返回空串）。
QString jsonString(const QJsonObject& obj, const char* key) {
    return obj.value(QLatin1String(key)).toString();
}

qint64 jsonInt64(const QJsonObject& obj, const char* key) {
    return static_cast<qint64>(obj.value(QLatin1String(key)).toDouble(0.0));
}

double jsonDouble(const QJsonObject& obj, const char* key) {
    return obj.value(QLatin1String(key)).toDouble(0.0);
}

} // namespace

SteamServiceManager::SteamServiceManager(QObject* parent)
    : QObject(parent) {}

SteamServiceManager::~SteamServiceManager() {
    stop();
}

// 启动服务进程：定位二进制 → QProcess 启动 → 发送 hello 握手。
// 握手完成前状态为 Starting，收到 hello 响应后转为 Connected。
void SteamServiceManager::start() {
    if (m_process != nullptr) return;
    launchProcess();
}

void SteamServiceManager::launchProcess() {
    const QString executable = locateServiceExecutable();
    if (executable.isEmpty()) {
        m_lastError = QStringLiteral("SteamService 二进制未找到（设置 MIRAGE_STEAM_SERVICE_PATH 或使用打包版本）");
        emit serviceStateChanged(ServiceState::Failed);
        return;
    }

    m_stopping = false;
    m_stdoutBuffer.clear();
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->setReadChannel(QProcess::StandardOutput);

    // 启动参数：<dotnet> <assembly>。运行时目录由环境 DOTNET_ROOT 提供，
    // 使 framework-dependent 的 MirageSteamService.dll 找到 shared runtime。
    const QString assembly = QFileInfo(executable).dir().absolutePath() +
                             QStringLiteral("/app/MirageSteamService.dll");
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString runtimeRoot = QFileInfo(executable).dir().absolutePath() +
                                QStringLiteral("/runtime");
    env.insert(QStringLiteral("DOTNET_ROOT"), runtimeRoot);
    m_process->setProcessEnvironment(env);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &SteamServiceManager::handleStdout);
    connect(m_process, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus status) {
                handleFinished(exitCode, status);
            });

    m_process->start(executable, { assembly });
    if (! m_process->waitForStarted(3000)) {
        m_lastError = QStringLiteral("无法启动 SteamService：%1")
                          .arg(m_process->errorString());
        m_process->deleteLater();
        m_process = nullptr;
        emit serviceStateChanged(ServiceState::Failed);
        return;
    }
    emit serviceStateChanged(ServiceState::Starting);

    // hello 握手；服务端回复 {type:"hello", version, maxConcurrentDownloads}。
    sendCommand(QStringLiteral("hello"));
}

// 停止：终止进程（finished 处理器里 m_stopping 已置位，不触发重启）。
void SteamServiceManager::stop() {
    m_stopping = true;
    if (m_process != nullptr) {
        // 先关 stdin 让服务端 while 循环读 EOF 后自行退出；超时则强杀。
        m_process->closeWriteChannel();
        if (! m_process->waitForFinished(2000)) {
            m_process->terminate();
            if (! m_process->waitForFinished(2000)) m_process->kill();
        }
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_pendingRequests.clear();
    m_downloadHandlers.clear();
    emit serviceStateChanged(ServiceState::Stopped);
}

void SteamServiceManager::handleStdout() {
    if (m_process == nullptr) return;
    m_stdoutBuffer += m_process->readAllStandardOutput();
    int newline = 0;
    while ((newline = m_stdoutBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_stdoutBuffer.left(newline);
        m_stdoutBuffer.remove(0, newline + 1);
        if (line.trimmed().isEmpty()) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (! doc.isObject()) continue;  // 非 JSON 行（如 dotnet 警告）忽略
        handleLine(doc.object());
    }
}

// 进程意外退出（非主动停止）→ 清空回调并安排重启。
void SteamServiceManager::handleFinished(int /*exitCode*/, QProcess::ExitStatus status) {
    if (m_process != nullptr) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    for (auto it = m_pendingRequests.begin(); it != m_pendingRequests.end(); ++it) {
        if (it.value()) {
            it.value()(false, QStringLiteral("SteamService 进程已退出"), QString(), QJsonObject());
        }
    }
    m_pendingRequests.clear();
    m_downloadHandlers.clear();
    if (m_stopping) {
        emit serviceStateChanged(ServiceState::Stopped);
        return;
    }
    // 未主动停止：进程崩溃/被杀后自动重启并尝试恢复会话（对齐上游
    // scheduleRestoreRetry 的意图；无退避上限，避免服务恢复后失联）。
    m_lastError = QStringLiteral("SteamService 进程退出（status=%1），正在重启")
                      .arg(status == QProcess::CrashExit ? "crash" : "exit");
    scheduleRestart();
}

void SteamServiceManager::scheduleRestart() {
    ++m_restartCount;
    // 延迟重启，避免崩溃风暴；200ms * 重试次数，上限 2s。
    const int delay = qMin(200 * m_restartCount, 2000);
    QTimer::singleShot(delay, this, [this] {
        if (m_stopping) return;
        launchProcess();
        if (m_process != nullptr) restoreSessionIfNeeded();
    });
}

void SteamServiceManager::handleLine(const QJsonObject& event) {
    const QString type = jsonString(event, "type");
    if (type == kTypeResponse) {
        handleResponse(event);
    } else if (type == kTypeAuthState) {
        handleAuthEvent(event);
    } else if (type == kTypeDownloadState) {
        handleDownloadEvent(event);
    }
    // "hello"/"pong" 走 response 路径（requestId 回显）。
}

void SteamServiceManager::handleResponse(const QJsonObject& event) {
    const QString requestId = jsonString(event, "requestId");
    if (requestId.isEmpty()) {
        // hello 握手无 requestId 时用空键处理。
        if (m_process != nullptr) emit serviceStateChanged(ServiceState::Connected);
        return;
    }
    auto it = m_pendingRequests.find(requestId);
    if (it == m_pendingRequests.end()) return;
    RequestCallback cb = it.value();
    m_pendingRequests.erase(it);

    // hello 成功 → 进入 Connected，并触发会话恢复检查。
    const bool success = event.value(QLatin1String("success")).toBool(false);
    const QString message = jsonString(event, "message");
    const QString errorCode = jsonString(event, "errorCode");
    const QJsonObject data = event.value(QLatin1String("data")).toObject();
    if (cb) {
        cb(success, message, errorCode, data);
    } else if (requestId == QLatin1String("0") && success) {
        emit serviceStateChanged(ServiceState::Connected);
        restoreSessionIfNeeded();
    }
}

// 发送一条命令：组装 {command, requestId, ...fields} 写一行到 stdin。
// 有回调时按 requestId 登记；无回调的 hello 用固定 requestId "0"。
void SteamServiceManager::sendCommand(const QString& command, const QJsonObject& fields,
                                      RequestCallback cb) {
    if (m_process == nullptr) {
        if (cb) cb(false, QStringLiteral("SteamService 未运行"), QString(), QJsonObject());
        return;
    }
    QJsonObject payload = fields;
    payload.insert(QStringLiteral("command"), command);
    if (cb) {
        const QString requestId = QString::number(m_nextRequestId++);
        payload.insert(QStringLiteral("requestId"), requestId);
        m_pendingRequests.insert(requestId, cb);
    } else {
        payload.insert(QStringLiteral("requestId"), QStringLiteral("0"));
    }
    m_process->write(QJsonDocument(payload).toJson(QJsonDocument::Compact) + '\n');
}

void SteamServiceManager::handleAuthEvent(const QJsonObject& event) {
    const QString state = jsonString(event, "state");
    const QString username = jsonString(event, "accountName");
    const QString detail = jsonString(event, "message");
    const QString errorCode = jsonString(event, "errorCode");
    const QString steamId = jsonString(event, "steamId");

    if (state == QLatin1String("connecting") || state == QLatin1String("authenticating")) {
        m_loginState = LoginState::LoggingIn;
        m_lastError.clear();
        emit loginStateChanged();
    } else if (state == QLatin1String("qr")) {
        m_qrChallengeUrl = jsonString(event, "challengeUrl");
        m_loginState = LoginState::WaitingForQR;
        m_lastError.clear();
        emit loginStateChanged();
    } else if (state == QLatin1String("waitingMobile")) {
        m_guardType = GuardType::MobileConfirm;
        m_loginState = LoginState::WaitingForGuard;
        emit loginStateChanged();
    } else if (state == QLatin1String("mobileCode")) {
        m_guardType = GuardType::Mobile;
        m_loginState = LoginState::WaitingForGuard;
        emit loginStateChanged();
    } else if (state == QLatin1String("emailCode")) {
        m_guardType = GuardType::Email;
        m_loginState = LoginState::WaitingForGuard;
        emit loginStateChanged();
    } else if (state == QLatin1String("loggedIn")) {
        const bool wasRestoring = m_restoringSession;
        m_restoringSession = false;
        const QString resolvedUsername = ! username.isEmpty() ? username : m_accountName;
        const QString refreshToken = jsonString(event, "refreshToken");
        const QString guardData = jsonString(event, "guardData");

        // 持久化会话（对齐上游 keychain 持久化语义，仅登录成功时写入）。
        QSettings settings;
        settings.beginGroup(QLatin1String(kSettingsGroup));
        if (! refreshToken.isEmpty()) {
            settings.setValue(settingsKey(resolvedUsername) + QStringLiteral(":refreshToken"),
                              refreshToken);
        } else if (! wasRestoring) {
            settings.remove(settingsKey(resolvedUsername) + QStringLiteral(":refreshToken"));
        }
        if (! guardData.isEmpty()) {
            settings.setValue(settingsKey(resolvedUsername) + QStringLiteral(":guardData"),
                              guardData);
        }
        settings.endGroup();
        settings.sync();

        if (! resolvedUsername.isEmpty()) m_accountName = resolvedUsername;
        m_loggedIn = true;
        m_steamId = steamId;
        m_loginState = LoginState::Success;
        m_lastError.clear();
        emit loginStateChanged();
        emit authenticationChanged(true, m_accountName, m_steamId);
    } else if (state == QLatin1String("failed")) {
        m_restoringSession = false;
        QString message = ! detail.isEmpty() ? detail : QStringLiteral("Steam 登录失败");
        if (errorCode == QLatin1String(kAuthFailedCode)) {
            message = QStringLiteral("保存的 Steam 会话已失效，请重新登录");
            clearSavedSession();
        }
        m_loggedIn = false;
        m_steamId.clear();
        m_loginState = LoginState::Failed;
        m_lastError = message;
        emit loginStateChanged();
        emit authenticationChanged(false, m_accountName, m_steamId);
    } else if (state == QLatin1String("loggedOut")) {
        if (errorCode == QLatin1String(kAuthCancelledCode)) return;  // 取消登录不视为登出
        const QString message = errorCode.isEmpty() && detail.isEmpty()
            ? QStringLiteral("需要登录 Steam")
            : detail;
        m_loggedIn = false;
        m_steamId.clear();
        m_loginState = LoginState::Idle;
        m_lastError = message;
        emit loginStateChanged();
        emit authenticationChanged(false, m_accountName, m_steamId);
    }
}

void SteamServiceManager::handleDownloadEvent(const QJsonObject& event) {
    const QString taskId = jsonString(event, "taskId");
    const QString state = jsonString(event, "state");
    auto it = m_downloadHandlers.find(taskId);
    if (it == m_downloadHandlers.end()) return;
    DownloadCallback cb = it.value();

    DownloadState result;
    result.kind = DownloadStateKind::Queued;
    result.message = jsonString(event, "message");
    result.bytesReceived = jsonInt64(event, "receivedBytes");
    result.totalBytes = jsonInt64(event, "totalBytes");
    result.bytesPerSecond = jsonDouble(event, "bytesPerSecond");
    result.outputPath = jsonString(event, "outputPath");
    if (result.totalBytes > 0) {
        result.percent = static_cast<double>(result.bytesReceived) * 100.0 /
                         static_cast<double>(result.totalBytes);
    }

    if (state == QLatin1String("connecting")) {
        result.kind = DownloadStateKind::Connecting;
    } else if (state == QLatin1String("downloading")) {
        result.kind = DownloadStateKind::Downloading;
    } else if (state == QLatin1String("resolving") || state == QLatin1String("validating")) {
        result.kind = DownloadStateKind::Resolving;
    } else if (state == QLatin1String("completed")) {
        result.kind = DownloadStateKind::Completed;
        m_downloadHandlers.erase(it);
    } else if (state == QLatin1String("cancelled")) {
        result.kind = DownloadStateKind::Cancelled;
        m_downloadHandlers.erase(it);
    } else if (state == QLatin1String("failed")) {
        result.kind = DownloadStateKind::Failed;
        m_downloadHandlers.erase(it);
    }

    emit downloadStateChanged(taskId, result.kind, result.bytesReceived,
                              result.totalBytes, result.bytesPerSecond,
                              result.outputPath, result.message);
    if (cb) cb(result);
}

// ---- 认证命令 ----

void SteamServiceManager::restoreSessionIfNeeded() {
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    // 找到任一已保存的 refresh-token:<username> 账户（用户名小写存储）。
    QString savedUsername;
    QString refreshToken;
    const QStringList keys = settings.childKeys();
    for (const QString& key : keys) {
        if (key.endsWith(QLatin1String(":refreshToken"))) {
            savedUsername = key.left(key.size() - QStringLiteral(":refreshToken").size());
            refreshToken = settings.value(key).toString();
            break;
        }
    }
    settings.endGroup();
    if (savedUsername.isEmpty() || refreshToken.isEmpty()) return;
    m_restoringSession = true;
    m_accountName = savedUsername;
    m_loginState = LoginState::LoggingIn;
    emit loginStateChanged();
    QJsonObject fields;
    fields.insert(QStringLiteral("username"), savedUsername);
    fields.insert(QStringLiteral("refreshToken"), refreshToken);
    sendCommand(QStringLiteral("restoreSession"), fields,
                [this](bool, const QString&, const QString&, const QJsonObject&) {
                    // 服务端已异步开始恢复；最终成败经 authState 事件上报。
                });
}

void SteamServiceManager::loginWithQR() {
    m_restoringSession = false;
    m_qrChallengeUrl.clear();
    m_loginState = LoginState::LoggingIn;
    emit loginStateChanged();
    sendCommand(QStringLiteral("loginQr"), QJsonObject(),
                [this](bool ok, const QString& message, const QString& errorCode,
                       const QJsonObject&) {
                    if (! ok) {
                        m_loginState = LoginState::Failed;
                        m_lastError = message;
                        emit loginStateChanged();
                    }
                });
}

void SteamServiceManager::login(const QString& username, const QString& password) {
    m_restoringSession = false;
    m_loginState = LoginState::LoggingIn;
    emit loginStateChanged();
    QJsonObject fields;
    fields.insert(QStringLiteral("username"), username);
    fields.insert(QStringLiteral("password"), password);
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    const QString guardData = settings.value(settingsKey(username) + QStringLiteral(":guardData"))
                                  .toString();
    settings.endGroup();
    if (! guardData.isEmpty()) fields.insert(QStringLiteral("guardData"), guardData);
    sendCommand(QStringLiteral("loginPassword"), fields,
                [this](bool ok, const QString& message, const QString& errorCode,
                       const QJsonObject&) {
                    if (! ok) {
                        m_loginState = LoginState::Failed;
                        m_lastError = message;
                        emit loginStateChanged();
                    }
                });
}

bool SteamServiceManager::submitGuardCode(const QString& code) {
    if (code.isEmpty()) return false;
    sendCommand(QStringLiteral("submitChallenge"), QJsonObject{ { QStringLiteral("code"), code } });
    return true;
}

void SteamServiceManager::cancelLogin() {
    sendCommand(QStringLiteral("cancelLogin"));
}

void SteamServiceManager::logout() {
    m_loggedIn = false;
    m_loginState = LoginState::Idle;
    clearSavedSession();
    emit loginStateChanged();
    emit authenticationChanged(false, m_accountName, m_steamId);
    sendCommand(QStringLiteral("logout"));
}

void SteamServiceManager::clearSavedSession() {
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    for (const QString& key : settings.childKeys()) settings.remove(key);
    settings.endGroup();
    settings.sync();
}

bool SteamServiceManager::hasSavedSession() const {
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    const bool has = ! settings.childKeys().isEmpty();
    settings.endGroup();
    return has;
}

// ---- 订阅命令 ----

void SteamServiceManager::fetchSubscriptions(
    int startIndex, std::function<void(bool, const SubscriptionPage&, const QString&)> cb) {
    QJsonObject fields;
    fields.insert(QStringLiteral("startIndex"), qMax(0, startIndex));
    sendCommand(QStringLiteral("listSubscriptions"), fields,
                [cb](bool ok, const QString& message, const QString&, const QJsonObject& data) {
                    SubscriptionPage page;
                    if (ok) {
                        page.total = static_cast<int>(data.value(QLatin1String("total")).toInt(0));
                        page.startIndex =
                            static_cast<int>(data.value(QLatin1String("startIndex")).toInt(0));
                        const QJsonArray items = data.value(QLatin1String("items")).toArray();
                        for (const QJsonValue& value : items) {
                            const QJsonObject item = value.toObject();
                            SubscriptionItem entry;
                            entry.publishedFileId = jsonString(item, "workshopId");
                            entry.subscribedAt = jsonInt64(item, "subscribedAt");
                            entry.updatedAt = jsonInt64(item, "updatedAt");
                            entry.contentHash = jsonString(item, "contentHash");
                            entry.fileSize = jsonInt64(item, "fileSize");
                            page.items.append(entry);
                        }
                    }
                    if (cb) cb(ok, page, message);
                });
}

void SteamServiceManager::fetchSubscriptionStates(
    const QStringList& workshopIds,
    std::function<void(bool, const QHash<QString, bool>&, const QString&)> cb) {
    QStringList uniqueIds = workshopIds;
    uniqueIds.removeDuplicates();
    uniqueIds.removeAll(QString());
    if (uniqueIds.isEmpty()) {
        if (cb) cb(true, {}, QString());
        return;
    }
    QJsonObject fields;
    fields.insert(QStringLiteral("workshopIds"), QJsonArray::fromStringList(uniqueIds));
    sendCommand(QStringLiteral("checkSubscriptionStates"), fields,
                [cb](bool ok, const QString& message, const QString&, const QJsonObject& data) {
                    QHash<QString, bool> states;
                    if (ok) {
                        const QJsonArray items = data.value(QLatin1String("items")).toArray();
                        for (const QJsonValue& value : items) {
                            const QJsonObject item = value.toObject();
                            states.insert(jsonString(item, "workshopId"),
                                          item.value(QLatin1String("subscribed")).toBool(false));
                        }
                    }
                    if (cb) cb(ok, states, message);
                });
}

void SteamServiceManager::subscribe(const QString& workshopId, RequestCallback cb) {
    QJsonObject fields;
    fields.insert(QStringLiteral("workshopId"), workshopId);
    sendCommand(QStringLiteral("subscribe"), fields, cb);
}

void SteamServiceManager::unsubscribe(const QString& workshopId, RequestCallback cb) {
    QJsonObject fields;
    fields.insert(QStringLiteral("workshopId"), workshopId);
    sendCommand(QStringLiteral("unsubscribe"), fields, cb);
}

// ---- 下载命令 ----

void SteamServiceManager::downloadItem(const QString& workshopId, const QString& taskId,
                                       const QString& outputRoot, DownloadCallback cb) {
    m_downloadHandlers.insert(taskId, cb);
    QJsonObject fields;
    fields.insert(QStringLiteral("taskId"), taskId);
    fields.insert(QStringLiteral("workshopId"), workshopId);
    fields.insert(QStringLiteral("outputRoot"), outputRoot);
    sendCommand(QStringLiteral("download"), fields,
                [this, taskId](bool ok, const QString& message, const QString&,
                               const QJsonObject&) {
                    if (! ok) {
                        auto it = m_downloadHandlers.find(taskId);
                        if (it != m_downloadHandlers.end()) m_downloadHandlers.erase(it);
                        DownloadState result;
                        result.kind = DownloadStateKind::Failed;
                        result.message = message;
                        emit downloadStateChanged(taskId, result.kind, 0, 0, 0.0,
                                                  QString(), result.message);
                    }
                });
}

void SteamServiceManager::cancelDownload(const QString& taskId) {
    QJsonObject fields;
    fields.insert(QStringLiteral("taskId"), taskId);
    sendCommand(QStringLiteral("cancelDownload"), fields);
}

// 定位服务二进制，优先级与上游 serviceLaunchConfiguration 一致：
//   1. 环境变量 MIRAGE_STEAM_SERVICE_PATH（直接可执行文件）
//   2. 应用目录 SteamService/linux-x64/runtime/dotnet（打包布局）
//   3. 仓库内 SteamService/build/linux-x64/runtime/dotnet（开发布局）
QString SteamServiceManager::locateServiceExecutable() const {
    const QByteArray env = qgetenv("MIRAGE_STEAM_SERVICE_PATH");
    if (! env.isEmpty() && QFileInfo::exists(QString::fromLocal8Bit(env))) {
        return QString::fromLocal8Bit(env);
    }
    const QStringList candidateRoots = {
        // 打包/安装布局：<app>/SteamService/linux-x64/runtime/dotnet
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("SteamService/linux-x64/runtime/dotnet")),
        // 开发布局：<repo>/SteamService/build/linux-x64/runtime/dotnet
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("../../../SteamService/build/linux-x64/runtime/dotnet")),
    };
    for (const QString& root : candidateRoots) {
        if (QFileInfo(root).isExecutable()) return root;
    }
    return QString();
}

} // namespace Mirage
