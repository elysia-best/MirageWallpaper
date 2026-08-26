#include "Services/RendererController.h"

#include "Services/LinuxSystemIntegration.h"
#include "Services/Paths.h"
#include "Services/DisplayBrokerService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QGuiApplication>
#include <QScreen>
#include <QTextStream>
#include <QTimer>
#include <QUuid>

#include <functional>

namespace Mirage {
namespace {

QString number(double value) {
    return QString::number(value, 'f', 3);
}

void writeRendererDiagnostic(int screenIndex, const QString& text) {
    QTextStream stream(stdout);
    stream << "[Renderer " << screenIndex + 1 << "] " << text << Qt::endl;
}

QString siblingBinary(const QString& name) {
    const QString candidate = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/" + name);
    return QFileInfo(candidate).isExecutable() ? candidate : QString();
}

QString firstExecutable(const QStringList& candidates) {
    for (const QString& candidate : candidates) {
        if (QFileInfo(candidate).isExecutable()) return QDir::cleanPath(candidate);
    }
    return {};
}

QJsonValue propertyWireValue(const ProjectProperty& property) {
    switch (property.propertyKind()) {
    case PropertyKind::Bool:
        return property.boolValue();
    case PropertyKind::Slider:
        return property.doubleValue();
    case PropertyKind::Combo:
        return variantToJsonValue(property.value);
    case PropertyKind::Color:
    case PropertyKind::SceneTexture:
    case PropertyKind::File:
    case PropertyKind::TextInput:
    case PropertyKind::Text:
    case PropertyKind::Group:
    case PropertyKind::Directory:
    case PropertyKind::UserShortcut:
    case PropertyKind::Unknown:
        return property.stringValue();
    }
    return property.stringValue();
}

} // namespace

RendererController::RendererController(GlobalSettingsService* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings) {
    qRegisterMetaType<Mirage::FillMode>();
}

RendererController::~RendererController() {
    stopAll();
}

void RendererController::setWallpaperTrustChecker(const std::function<bool(const Wallpaper&)>& checker) {
    m_wallpaperTrustChecker = checker;
}

bool RendererController::render(const Wallpaper& wallpaper, int screenIndex, const RenderOptions& options, QString* error) {
    if (!wallpaper.isValid()) {
        qWarning() << "[Render] Wallpaper is invalid";
        if (error) *error = QStringLiteral("壁纸无效或缺少预设依赖");
        return false;
    }

    qWarning() << "[Render] Called with wallpaper kind:" << static_cast<int>(wallpaper.kind())
               << "screenIndex:" << screenIndex;

    if (wallpaper.kind() == WallpaperKind::Web
        && m_wallpaperTrustChecker
        && !m_wallpaperTrustChecker(wallpaper)) {
        if (error) *error = QStringLiteral("网页壁纸需要用户确认后才可运行");
        emit rendererMessage(error ? *error : QString());
        return false;
    }

    const QString unsupported = LinuxSystemIntegration::wallpaperUnsupportedReason();
    if (!unsupported.isEmpty()) {
        qWarning() << "[Render] Wallpaper unsupported:" << unsupported;
        if (error) *error = unsupported;
        return false;
    }

    const QString binary = binaryForKind(wallpaper.kind());
    if (binary.isEmpty()) {
        qWarning() << "[Render] Binary not found for kind:" << static_cast<int>(wallpaper.kind());
        if (error) *error = QStringLiteral("找不到渲染器二进制");
        return false;
    }

    const QList<QScreen*> screens = QGuiApplication::screens();
    QScreen* targetScreen = screens.isEmpty()
                                ? nullptr
                                : screens.at(qBound(0, screenIndex, screens.size() - 1));
    const QString outputStableId = stableOutputId(targetScreen);
    if (outputStableId.isEmpty()) {
        qWarning() << "[Render] Cannot determine output stable ID for screen" << screenIndex;
        if (error) *error = QStringLiteral("无法确定目标显示器标识，无法应用壁纸");
        return false;
    }
    qWarning() << "[Render] Output stable ID:" << outputStableId;

    qWarning() << "[Render] Checking m_running for screenIndex:" << screenIndex
               << "contains:" << m_running.contains(screenIndex);
    if (m_running.contains(screenIndex)) {
        qWarning() << "[Render] Found existing process, will terminate";
    }

    if (RunningProcess* const running = m_running.value(screenIndex)) {
        // Broker 在旧 producer 断连前拒绝同一输出的新 producer。因此切换时
        // 覆盖保存最新请求，并立即强制终止旧进程以加速 broker 清理。
        qWarning() << "[Switch] Detected existing renderer on screen" << screenIndex << "PID" << running->process->processId();
        m_pendingRenders.insert(screenIndex, PendingRender{wallpaper, options});
        if (!running->stopping) {
            running->stopping = true;
            qWarning() << "[Switch] Sending SIGTERM to PID" << running->process->processId();
            // 立即发送 SIGTERM，不再尝试优雅退出
            running->process->terminate();
            // 如果 200ms 后仍未退出，发送 SIGKILL
            QTimer::singleShot(200, running->process, [process = running->process] {
                if (process->state() != QProcess::NotRunning) {
                    qWarning() << "[Switch] Process still running after 200ms, sending SIGKILL to" << process->processId();
                    process->kill();
                }
            });
            emit rendererStateChanged();
        }
        return true;
    }

    auto* process = new QProcess(this);
    auto* running = new RunningProcess;
    running->process = process;
    running->wallpaper = wallpaper;
    running->screenIndex = screenIndex;
    running->outputStableId = outputStableId;

    QStringList args;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Wallpaper hosts display exclusively through the mirage-display protocol.
    args << "--display-output-id" << running->outputStableId
         << "--display-socket" << DisplayBrokerService::defaultSocketPath();

    switch (wallpaper.kind()) {
    case WallpaperKind::Scene: {
        args << Paths::assetsDir()
             << wallpaper.resolvedEntryPath()
             << "--fps" << QString::number(options.fps)
             << "--render-scale" << number(options.renderScale)
             << "--msaa" << QString::number(options.msaaSamples)
             << "--control-stdin";
        if (options.muted) args << "--muted";
        if (options.loadFromMemory) args << "--load-from-memory";
        if (!options.enableSpectrum) args << "--no-spectrum";
        const QString propsFile = writeUserPropertiesFile(options.userProperties, wallpaper);
        if (!propsFile.isEmpty()) {
            args << "--user-properties" << propsFile;
            running->tempFiles << propsFile;
        }
        break;
    }
    case WallpaperKind::Video:
        args << wallpaper.renderDirectory
             << "--volume" << number(options.volume)
             << "--fill" << fillModeKey(options.fillMode);
        if (options.muted) args << "--muted";
        if (options.loadFromMemory) args << "--load-from-memory";
        args << "--control-stdin";
            env.insert("LC_NUMERIC", "C");  // From mpv: Non-C locale detected. This is not supported.
                                                             // Call 'setlocale(LC_NUMERIC, "C");' in your code.
        break;
    case WallpaperKind::Web:
        // WebWallpaper resolves preset files in this declared order, so the
        // preset directory can replace base-project assets without changing
        // project property paths. The base render directory remains the
        // positional wallpaper root required by its manifest loader.
        args << wallpaper.renderDirectory;
        for (const QString& overlay : wallpaper.assetOverlayDirectories) {
            args << "--asset-overlay" << overlay;
        }
        args << "--fps" << QString::number(options.fps)
             << "--volume" << number(options.volume)
             << "--control-stdin";
        if (options.muted) args << "--muted";
        if (options.loadFromMemory) args << "--load-from-memory";
        if (!options.enableSpectrum) args << "--no-spectrum";
        break;
    case WallpaperKind::Unsupported:
        delete running;
        process->deleteLater();
        return false;
    }

    connect(process, &QProcess::readyReadStandardError, this, [running, process] {
        const QString text = QString::fromUtf8(process->readAllStandardError()).trimmed();
        if (!text.isEmpty()) writeRendererDiagnostic(running->screenIndex, text);
    });
    connect(process, &QProcess::readyReadStandardOutput, this, [this, running, process] {
        consumeStdout(running, process->readAllStandardOutput());
    });

    connect(process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this, running](int exitCode, QProcess::ExitStatus exitStatus) {
                const int screen = running->screenIndex;
                const bool abnormal = !running->stopping &&
                                      (exitStatus != QProcess::NormalExit || exitCode != 0);
                const bool wasCurrent = m_running.value(screen) == running;
                if (wasCurrent) m_running.remove(screen);
                const bool launchPending = wasCurrent && running->stopping &&
                                           m_pendingRenders.contains(screen);
                PendingRender pending;
                if (launchPending) pending = m_pendingRenders.take(screen);
                for (const QString& temp : running->tempFiles) QFile::remove(temp);
                running->process->deleteLater();
                delete running;
                if (wasCurrent) emit rendererStateChanged();
                if (launchPending) {
                    // 给 broker 100ms 时间完成旧 producer 清理，避免新 producer
                    // 连接时遇到 MD_ERR_STATE 或重复注册错误。
                    qWarning() << "[Switch] Old process finished, waiting 100ms before launching new renderer";
                    QTimer::singleShot(100, this, [this, pending, screen]() {
                        qWarning() << "[Switch] Launching new renderer after cleanup delay";
                        QString launchError;
                        if (!render(pending.wallpaper, screen, pending.options, &launchError)
                            && !launchError.isEmpty()) {
                            emit rendererMessage(launchError);
                        }
                    });
                }
                emit rendererExited(screen, abnormal);
            });

    process->setProgram(binary);
    process->setArguments(args);
    process->setProcessEnvironment(env);
    process->setProcessChannelMode(QProcess::SeparateChannels);

    // 在 start() 之前插入 m_running，避免竞态：如果进程快速退出，
    // finished 信号处理时 wasCurrent 判断才能正确工作。
    m_running.insert(screenIndex, running);
    qWarning() << "[Render] Inserted into m_running at screenIndex:" << screenIndex << "before start";

    process->start();

    if (!process->waitForStarted(5000)) {
        const QString message = process->errorString();
        for (const QString& temp : running->tempFiles) QFile::remove(temp);
        m_running.remove(screenIndex);
        delete running;
        process->deleteLater();
        if (error) *error = message;
        return false;
    }

    qWarning() << "[Render] Process started successfully, PID:" << process->processId();

    if (wallpaper.kind() == WallpaperKind::Web) {
        // QtWebEngine navigation is asynchronous. WebWallpaper retains this
        // full snapshot until the page installs its Wallpaper Engine listener,
        // preventing startup properties from being lost before live edits.
        QJsonObject values;
        for (auto it = options.userProperties.constBegin();
             it != options.userProperties.constEnd(); ++it) {
            values.insert(it.key(), QJsonObject{{"value", propertyWireValue(it.value())}});
        }
        sendCommand(running, QJsonObject{
            {"cmd", "setProperties"},
            {"generation", QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"values", values},
        });
    }

    emit rendererStateChanged();
    return true;
}

void RendererController::stop(int screenIndex) {
    // 显式停止优先于异步切换：移除待请求，禁止 finished 回调重新启动壁纸。
    m_pendingRenders.remove(screenIndex);
    RunningProcess* running = m_running.value(screenIndex);
    if (!running || running->stopping) return;

    running->stopping = true;
    sendCommand(running, QJsonObject{{"cmd", "quit"}});
    running->process->closeWriteChannel();

    // Escalating shutdown: "quit" first, then terminate() at 1.5 s and
    // kill() at 3 s. Both timers no-op once the process has exited.
    QTimer::singleShot(1500, running->process, [process = running->process] {
        if (process->state() != QProcess::NotRunning) process->terminate();
    });
    QTimer::singleShot(3000, running->process, [process = running->process] {
        if (process->state() != QProcess::NotRunning) process->kill();
    });
    emit rendererStateChanged();
}

void RendererController::stopAll() {
    // 应用退出和“停止全部”均不得保留延迟启动意图。
    m_pendingRenders.clear();
    const QVector<int> screens = activeScreens();
    for (int screen : screens) stop(screen);
}

QVector<int> RendererController::activeScreens() const {
    QVector<int> screens;
    for (auto it = m_running.constBegin(); it != m_running.constEnd(); ++it) {
        if (!it.value()->stopping) screens.push_back(it.key());
    }
    std::sort(screens.begin(), screens.end());
    return screens;
}

bool RendererController::isRunningOnScreen(int screenIndex) const {
    const RunningProcess* const running = m_running.value(screenIndex);
    return running != nullptr && !running->stopping;
}

QString RendererController::wallpaperIdOnScreen(int screenIndex) const {
    const RunningProcess* running = m_running.value(screenIndex);
    return running != nullptr && !running->stopping ? running->wallpaper.id() : QString();
}

QString RendererController::fillModeKey(FillMode mode) {
    switch (mode) {
    case FillMode::Cover: return QStringLiteral("cover");
    case FillMode::Contain: return QStringLiteral("contain");
    case FillMode::Stretch: return QStringLiteral("stretch");
    }
    return QStringLiteral("cover");
}

void RendererController::setPowerState(const QString& state, int screenIndex) {
    forEachTarget(screenIndex, [&](RunningProcess* running) {
        sendCommand(running, QJsonObject{{QStringLiteral("cmd"), QStringLiteral("power")},
                                         {QStringLiteral("state"), state}});
    });
}

QString RendererController::stableOutputId(const QScreen* screen) {
    if (screen == nullptr) return {};
    const QString manufacturer = screen->manufacturer().trimmed();
    const QString model = screen->model().trimmed();
    const QString serial = screen->serialNumber().trimmed();
    const QString connector = screen->name().trimmed();
    const QString identity = serial.isEmpty()
                                 ? QStringList {manufacturer, model, connector}.join('|')
                                 : QStringList {manufacturer, model, serial}.join('|');
    return QStringLiteral("kde:") + identity;
}

void RendererController::setVolume(double volume, int screenIndex) {
    forEachTarget(screenIndex, [&](RunningProcess* running) {
        sendCommand(running, QJsonObject{{"cmd", "volume"}, {"value", volume}});
    });
}

void RendererController::setMuted(bool muted, int screenIndex) {
    forEachTarget(screenIndex, [&](RunningProcess* running) {
        sendCommand(running, QJsonObject{{"cmd", "muted"}, {"value", muted}});
    });
}

void RendererController::pause(int screenIndex) {
    forEachTarget(screenIndex, [&](RunningProcess* running) {
        sendCommand(running, QJsonObject{{"cmd", "pause"}});
    });
}

void RendererController::resume(int screenIndex) {
    forEachTarget(screenIndex, [&](RunningProcess* running) {
        sendCommand(running, QJsonObject{{"cmd", "resume"}});
    });
}

void RendererController::setFps(int fps, int screenIndex) {
    forEachTarget(screenIndex, [&](RunningProcess* running) {
        sendCommand(running, QJsonObject{{"cmd", "fps"}, {"value", fps}});
    });
}

void RendererController::setSpeed(double speed, int screenIndex) {
    forEachTarget(screenIndex, [&](RunningProcess* running) {
        sendCommand(running, QJsonObject{{"cmd", "speed"}, {"value", speed}});
    });
}

void RendererController::setFillMode(FillMode mode, int screenIndex) {
    forEachTarget(screenIndex, [&](RunningProcess* running) {
        sendCommand(running, QJsonObject{{"cmd", "fillmode"}, {"value", fillModeKey(mode)}});
    });
}

void RendererController::setProperty(const QString& key, const ProjectProperty& property, int screenIndex) {
    forEachTarget(screenIndex, [&](RunningProcess* running) {
        sendCommand(running, propertyCommand(key, property));
    });
}

QString RendererController::binaryForKind(WallpaperKind kind) const {
    switch (kind) {
    case WallpaperKind::Scene: return sceneWallpaperBinary();
    case WallpaperKind::Web: return webWallpaperBinary();
    case WallpaperKind::Video: return videoWallpaperBinary();
    case WallpaperKind::Unsupported: return {};
    }
    return {};
}

QString RendererController::sceneWallpaperBinary() const {
    return firstExecutable({
        QDir::cleanPath(QStringLiteral(MIRAGEQT_RUNTIME_DIR) + "/SceneWallpaper"),
        siblingBinary("SceneWallpaper"),
        QDir::cleanPath(Paths::repoRoot() + "/SceneRenderer/build/linux-clang-release/Tools/SceneWallpaper/SceneWallpaper"),
        QDir::cleanPath(Paths::repoRoot() + "/SceneRenderer/build/release/Tools/SceneWallpaper/SceneWallpaper"),
        QDir::cleanPath(Paths::repoRoot() + "/SceneRenderer/cmake-build-debug-clang-21/Tools/SceneWallpaper/SceneWallpaper"),
    });
}

QString RendererController::webWallpaperBinary() const {
    return firstExecutable({
        QDir::cleanPath(QStringLiteral(MIRAGEQT_RUNTIME_DIR) + "/WebWallpaper"),
        siblingBinary("WebWallpaper"),
        QDir::cleanPath(Paths::repoRoot() + "/WebRenderer/build/linux-release/Tools/WebWallpaper/WebWallpaper"),
        QDir::cleanPath(Paths::repoRoot() + "/WebRenderer/build/linux-debug/Tools/WebWallpaper/WebWallpaper"),
        QDir::cleanPath(Paths::repoRoot() + "/WebRenderer/build/linux/Tools/WebWallpaper/WebWallpaper"),
        QDir::cleanPath(Paths::repoRoot() + "/WebRenderer/build/release/Tools/WebWallpaper/WebWallpaper"),
        QDir::cleanPath(Paths::repoRoot() + "/WebRenderer/build/debug/Tools/WebWallpaper/WebWallpaper"),
    });
}

QString RendererController::videoWallpaperBinary() const {
    return firstExecutable({
        QDir::cleanPath(QStringLiteral(MIRAGEQT_RUNTIME_DIR) + "/VideoWallpaper"),
        siblingBinary("VideoWallpaper"),
        QDir::cleanPath(Paths::repoRoot() + "/VideoRenderer/build/linux-clang-release/Tools/VideoWallpaper/VideoWallpaper"),
        QDir::cleanPath(Paths::repoRoot() + "/VideoRenderer/build/release/Tools/VideoWallpaper/VideoWallpaper"),
        QDir::cleanPath(Paths::repoRoot() + "/VideoRenderer/build/debug/Tools/VideoWallpaper/VideoWallpaper"),
        QDir::cleanPath(Paths::repoRoot() + "/VideoRenderer/cmake-build-debug-clang-21/Tools/VideoWallpaper/VideoWallpaper"),
    });
}

QString RendererController::writeUserPropertiesFile(const QHash<QString, ProjectProperty>& props, const Wallpaper& wallpaper) const {
    if (props.isEmpty()) return {};

    QJsonObject object;
    for (auto it = props.constBegin(); it != props.constEnd(); ++it) {
        const auto kind = it.value().propertyKind();
        if (kind == PropertyKind::Color) {
            object.insert(it.key(), QJsonObject{{"type", "color"}, {"value", it.value().stringValue()}});
        } else if (kind == PropertyKind::SceneTexture || kind == PropertyKind::File) {
            object.insert(it.key(), QJsonObject{{"type", "scenetexture"}, {"value", it.value().stringValue()}});
        } else {
            object.insert(it.key(), propertyWireValue(it.value()));
        }
    }

    const QString path = QDir::temp().filePath(QStringLiteral("mirageqt_props_%1.json")
                                                   .arg(qHash(wallpaper.id())));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    return path;
}

QJsonObject RendererController::propertyCommand(const QString& key, const ProjectProperty& property) const {
    QJsonObject object{{"cmd", "setProperty"}, {"key", key}};
    const auto kind = property.propertyKind();
    if (kind == PropertyKind::Color) {
        object.insert("type", "color");
        object.insert("value", property.stringValue());
    } else if (kind == PropertyKind::SceneTexture || kind == PropertyKind::File) {
        object.insert("type", "scenetexture");
        object.insert("value", property.stringValue());
    } else {
        object.insert("value", propertyWireValue(property));
    }
    return object;
}

void RendererController::sendCommand(RunningProcess* running, const QJsonObject& command) {
    if (!running || running->process->state() == QProcess::NotRunning) return;
    QByteArray line = QJsonDocument(command).toJson(QJsonDocument::Compact);
    line.push_back('\n');
    running->process->write(line);
}

void RendererController::forEachTarget(int screenIndex, const std::function<void(RunningProcess*)>& body) {
    if (screenIndex >= 0) {
        if (RunningProcess* running = m_running.value(screenIndex)) body(running);
        return;
    }
    for (RunningProcess* running : m_running) body(running);
}

void RendererController::consumeStdout(RunningProcess* running, const QByteArray& chunk) {
    if (!running || chunk.isEmpty()) return;
    running->stdoutBuffer.append(chunk);
    while (true) {
        const int newline = running->stdoutBuffer.indexOf('\n');
        if (newline < 0) break;
        const QByteArray line = running->stdoutBuffer.left(newline).trimmed();
        running->stdoutBuffer.remove(0, newline + 1);
        if (line.isEmpty()) continue;
        const auto doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) continue;
        const QJsonObject object = doc.object();
        if (object.value(QStringLiteral("event")).toString() == QStringLiteral("video-did-end")) {
            emit videoDidEnd(running->screenIndex);
        }
    }
}

} // namespace Mirage
