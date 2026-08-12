#include "Services/PlaybackController.h"

#include "Services/GlobalSettingsService.h"
#include "Services/MirageController.h"
#include "Services/PlaylistManager.h"

#include <QGuiApplication>
#include <QScreen>

namespace Mirage {

namespace {

// 把 QML 提交的设置 map 应用到 GlobalSettings（缺失键保留原值）。
void updateSettingsFromMap(GlobalSettings& settings, const QVariantMap& values) {
    const auto stringValue = [&values](const QString& key, const QString& fallback) {
        return values.contains(key) ? values.value(key).toString() : fallback;
    };
    const auto boolValue = [&values](const QString& key, bool fallback) {
        return values.contains(key) ? values.value(key).toBool() : fallback;
    };
    const auto intValue = [&values](const QString& key, int fallback) {
        return values.contains(key) ? values.value(key).toInt() : fallback;
    };
    const auto doubleValue = [&values](const QString& key, double fallback) {
        return values.contains(key) ? values.value(key).toDouble() : fallback;
    };
    settings.otherApplicationFocused = stringValue(QStringLiteral("otherApplicationFocused"), settings.otherApplicationFocused);
    settings.otherApplicationFullscreen = stringValue(QStringLiteral("otherApplicationFullscreen"), settings.otherApplicationFullscreen);
    settings.otherApplicationPlayingAudio = stringValue(QStringLiteral("otherApplicationPlayingAudio"), settings.otherApplicationPlayingAudio);
    settings.displayAsleep = stringValue(QStringLiteral("displayAsleep"), settings.displayAsleep);
    settings.laptopOnBattery = stringValue(QStringLiteral("laptopOnBattery"), settings.laptopOnBattery);
    settings.antiAliasing = stringValue(QStringLiteral("antiAliasing"), settings.antiAliasing);
    settings.postProcessing = stringValue(QStringLiteral("postProcessing"), settings.postProcessing);
    settings.textureResolution = stringValue(QStringLiteral("textureResolution"), settings.textureResolution);
    settings.reflections = boolValue(QStringLiteral("reflections"), settings.reflections);
    settings.fps = intValue(QStringLiteral("fps"), settings.fps);
    settings.wallpaperLoadSource = stringValue(QStringLiteral("wallpaperLoadSource"), settings.wallpaperLoadSource);
    settings.autoStart = boolValue(QStringLiteral("autoStart"), settings.autoStart);
    settings.safeMode = boolValue(QStringLiteral("safeMode"), settings.safeMode);
    settings.language = stringValue(QStringLiteral("language"), settings.language);
    settings.appearance = stringValue(QStringLiteral("appearance"), settings.appearance);
    settings.audioOutput = boolValue(QStringLiteral("audioOutput"), settings.audioOutput);
    settings.reloadWhenChangingOutputDevice = boolValue(QStringLiteral("reloadWhenChangingOutputDevice"), settings.reloadWhenChangingOutputDevice);
    settings.masterVolume = doubleValue(QStringLiteral("masterVolume"), settings.masterVolume);
    settings.globalMuted = boolValue(QStringLiteral("globalMuted"), settings.globalMuted);
    settings.enableSpectrum = boolValue(QStringLiteral("enableSpectrum"), settings.enableSpectrum);
    settings.videoFramework = stringValue(QStringLiteral("videoFramework"), settings.videoFramework);
    settings.processPriority = stringValue(QStringLiteral("processPriority"), settings.processPriority);
    settings.pauseOnVRAMExhausted = boolValue(QStringLiteral("pauseOnVRAMExhausted"), settings.pauseOnVRAMExhausted);
    settings.restartAfterCrashing = boolValue(QStringLiteral("restartAfterCrashing"), settings.restartAfterCrashing);
    settings.logLevel = stringValue(QStringLiteral("logLevel"), settings.logLevel);
    settings.verboseLog = boolValue(QStringLiteral("verboseLog"), settings.verboseLog);
    settings.autoRefresh = boolValue(QStringLiteral("autoRefresh"), settings.autoRefresh);
    settings.steamAPIEndpoint = stringValue(QStringLiteral("steamAPIEndpoint"), settings.steamAPIEndpoint);
    settings.steamAPIKey = stringValue(QStringLiteral("steamAPIKey"), settings.steamAPIKey);
    settings.customWorkshopDirectory = stringValue(QStringLiteral("customWorkshopDirectory"), settings.customWorkshopDirectory);
    settings.customImportedDirectory = stringValue(QStringLiteral("customImportedDirectory"), settings.customImportedDirectory);
}

} // namespace

PlaybackController::PlaybackController(GlobalSettingsService* settings,
                                       RendererController* renderer,
                                       WallpaperRuntimeStore* runtimeStore,
                                       PlaylistManager* playlist,
                                       MirageController* owner,
                                       QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_renderer(renderer)
    , m_runtimeStore(runtimeStore)
    , m_playlist(playlist)
    , m_owner(owner) {}

RenderOptions PlaybackController::renderOptionsFor(const Wallpaper& item) const {
    const GlobalSettings& settings = m_settings->settings();
    const WallpaperRuntimeState runtime = m_runtimeStore->loadRuntime(item);
    RenderOptions options;
    options.fps = settings.fps;
    if (settings.textureResolution == QStringLiteral("highQuality"))
        options.renderScale = 1.0;
    else if (settings.textureResolution == QStringLiteral("highPerformance"))
        options.renderScale = 0.5;
    else
        options.renderScale = 0.75;
    if (settings.antiAliasing == QStringLiteral("none"))
        options.msaaSamples = 1;
    else if (settings.antiAliasing == QStringLiteral("msaa_x4"))
        options.msaaSamples = 4;
    else if (settings.antiAliasing == QStringLiteral("msaa_x8"))
        options.msaaSamples = 8;
    else
        options.msaaSamples = 2;
    options.volume = runtime.volume * settings.masterVolume;
    options.muted = runtime.muted || settings.globalMuted || runtime.volume <= 0.0;
    options.speed = runtime.speed;
    options.fillMode = runtime.fillMode;
    options.enableSpectrum = settings.enableSpectrum;
    options.loadFromMemory = settings.wallpaperLoadSource == QStringLiteral("memory");
    options.userProperties = m_runtimeStore->effectiveProperties(item, runtime);
    return options;
}

QVariantList PlaybackController::displays() const {
    QVariantList result;
    const QList<QScreen*> screens = QGuiApplication::screens();
    result.reserve(qMax(1, screens.size()));

    if (screens.isEmpty()) {
        result.append(QVariantMap{
            {QStringLiteral("index"), 0},
            {QStringLiteral("name"), QStringLiteral("显示器 1")},
            {QStringLiteral("width"), 0},
            {QStringLiteral("height"), 0},
            {QStringLiteral("primary"), true},
            {QStringLiteral("running"), m_renderer->isRunningOnScreen(0)},
            {QStringLiteral("wallpaperTitle"), QString()},
        });
        return result;
    }

    for (int index = 0; index < screens.size(); ++index) {
        const QScreen* screen = screens.at(index);
        const QString wallpaperId = m_renderer->wallpaperIdOnScreen(index);
        const Wallpaper runningWallpaper = m_owner->wallpaper(wallpaperId);
        const QRect geometry = screen->geometry();
        result.append(QVariantMap{
            {QStringLiteral("index"), index},
            {QStringLiteral("name"), screen->name().isEmpty()
                ? QStringLiteral("显示器 %1").arg(index + 1) : screen->name()},
            {QStringLiteral("width"), geometry.width()},
            {QStringLiteral("height"), geometry.height()},
            {QStringLiteral("primary"), screen == QGuiApplication::primaryScreen()},
            {QStringLiteral("running"), m_renderer->isRunningOnScreen(index)},
            {QStringLiteral("wallpaperTitle"), runningWallpaper.isValid()
                ? runningWallpaper.project.title : QString()},
        });
    }
    return result;
}

double PlaybackController::selectedVolume() const {
    const Wallpaper item = m_owner->wallpaper(m_owner->m_selectedWallpaperId);
    return item.isValid() ? m_runtimeStore->loadRuntime(item).volume : 1.0;
}

double PlaybackController::selectedSpeed() const {
    const Wallpaper item = m_owner->wallpaper(m_owner->m_selectedWallpaperId);
    return item.isValid() ? m_runtimeStore->loadRuntime(item).speed : 1.0;
}

QString PlaybackController::selectedFillMode() const {
    const Wallpaper item = m_owner->wallpaper(m_owner->m_selectedWallpaperId);
    if (!item.isValid()) return QStringLiteral("cover");
    return RendererController::fillModeKey(m_runtimeStore->loadRuntime(item).fillMode);
}

void PlaybackController::apply(const Wallpaper& item, bool allScreens) {
    if (!item.isValid()) return;
    m_owner->selectWallpaper(item.id());

    bool applied = false;
    QString error;
    const RenderOptions options = renderOptionsFor(item);
    const int count = allScreens ? qMax(1, QGuiApplication::screens().size()) : 1;
    for (int screen = 0; screen < count; ++screen) {
        QString screenError;
        if (m_renderer->render(item, screen, options, &screenError)) {
            applied = true;
            m_playlist->setCurrentWallpaper(screen, item);
            m_playlist->kickRotator(screen);
        }
        if (!screenError.isEmpty()) error = screenError;
    }
    if (applied) {
        if (m_paused) {
            m_paused = false;
            emit pausedChanged(false);
        }
        emit m_owner->playlistChanged();
        m_owner->setStatusMessage(allScreens
            ? QStringLiteral("已应用到所有显示器") : QStringLiteral("已应用壁纸"));
    } else if (!error.isEmpty()) {
        m_owner->setStatusMessage(error);
    }
}

void PlaybackController::applySelectedToScreen(int screen) {
    const Wallpaper item = m_owner->wallpaper(m_owner->m_selectedWallpaperId);
    if (!item.isValid()) return;

    const int target = qBound(0, screen, m_owner->screenCount() - 1);
    QString error;
    if (m_renderer->render(item, target, renderOptionsFor(item), &error)) {
        if (m_paused) {
            m_paused = false;
            emit pausedChanged(false);
        }
        m_playlist->setCurrentWallpaper(target, item);
        m_playlist->kickRotator(target);
        emit m_owner->playlistChanged();
        m_owner->setStatusMessage(QStringLiteral("已应用到显示器 %1").arg(target + 1));
    } else if (!error.isEmpty()) {
        m_owner->setStatusMessage(error);
    }
}

void PlaybackController::playPlaylistItem(const Wallpaper& item) {
    if (!item.isValid()) return;
    QString error;
    if (m_renderer->render(item, m_owner->m_playlistScreen, renderOptionsFor(item), &error)) {
        if (m_paused) {
            m_paused = false;
            emit pausedChanged(false);
        }
        m_playlist->setCurrentWallpaper(m_owner->m_playlistScreen, item);
        m_playlist->kickRotator(m_owner->m_playlistScreen);
        m_owner->selectWallpaper(item.id());
        m_owner->setStatusMessage(QStringLiteral("已应用壁纸"));
    } else if (!error.isEmpty()) {
        m_owner->setStatusMessage(error);
    }
}

void PlaybackController::stopWallpapers() {
    if (m_paused) {
        m_paused = false;
        emit pausedChanged(false);
    }
    m_renderer->stopAll();
    m_owner->setStatusMessage(QStringLiteral("已停止动态壁纸"));
}

void PlaybackController::stopScreen(int screen) {
    if (screen < 0 || screen >= m_owner->screenCount()) return;
    m_renderer->stop(screen);
    m_owner->setStatusMessage(QStringLiteral("已停止显示器 %1 的动态壁纸").arg(screen + 1));
}

void PlaybackController::pauseWallpapers() {
    if (m_paused) return;
    m_paused = true;
    emit pausedChanged(true);
    m_renderer->pause();
}

void PlaybackController::resumeWallpapers() {
    if (!m_paused) return;
    m_paused = false;
    emit pausedChanged(false);
    m_renderer->resume();
}

void PlaybackController::muteWallpapers() {
    m_renderer->setMuted(true);
}

void PlaybackController::setSelectedVolume(double volume) {
    const Wallpaper item = m_owner->wallpaper(m_owner->m_selectedWallpaperId);
    if (!item.isValid()) return;
    m_runtimeStore->setVolume(item, volume);
    const GlobalSettings& settings = m_settings->settings();
    const WallpaperRuntimeState runtime = m_runtimeStore->loadRuntime(item);
    m_renderer->setVolume(runtime.volume * settings.masterVolume);
    m_renderer->setMuted(runtime.muted || settings.globalMuted || runtime.volume <= 0.0);
    emit m_owner->selectedRuntimeChanged();
}

void PlaybackController::setSelectedSpeed(double speed) {
    const Wallpaper item = m_owner->wallpaper(m_owner->m_selectedWallpaperId);
    if (!item.isValid()) return;
    m_runtimeStore->setSpeed(item, speed);
    m_renderer->setSpeed(speed);
    emit m_owner->selectedRuntimeChanged();
}

void PlaybackController::setSelectedFillMode(const QString& mode) {
    const Wallpaper item = m_owner->wallpaper(m_owner->m_selectedWallpaperId);
    if (!item.isValid()) return;
    const FillMode fillMode = mode == QStringLiteral("contain")
        ? FillMode::Contain
        : mode == QStringLiteral("stretch") ? FillMode::Stretch : FillMode::Cover;
    m_runtimeStore->setFillMode(item, fillMode);
    m_renderer->setFillMode(fillMode);
    emit m_owner->selectedRuntimeChanged();
}

void PlaybackController::setSelectedProperty(const QString& key, const QVariant& value) {
    const Wallpaper item = m_owner->wallpaper(m_owner->m_selectedWallpaperId);
    if (!item.isValid()) return;
    const ProjectProperty property = m_runtimeStore->setProperty(item, key, value);
    if (property.type.isEmpty()) return;
    if (item.kind() == WallpaperKind::Scene || item.kind() == WallpaperKind::Web) {
        m_renderer->setProperty(key, property);
    }
}

void PlaybackController::resetSelectedProperties() {
    const Wallpaper item = m_owner->wallpaper(m_owner->m_selectedWallpaperId);
    if (!item.isValid()) return;
    m_runtimeStore->resetRuntime(item);
    bool rendered = false;
    for (int screen : m_renderer->activeScreens()) {
        if (m_playlist->currentWallpaper(screen).id() != item.id()) continue;
        QString error;
        if (m_renderer->render(item, screen, renderOptionsFor(item), &error))
            rendered = true;
    }
    // 重新渲染启动的是全新播放进程，暂停状态随之解除。
    if (rendered && m_paused) {
        m_paused = false;
        emit pausedChanged(false);
    }
}

void PlaybackController::previewFps(int fps) {
    m_renderer->setFps(qBound(10, fps, 120));
}

bool PlaybackController::applySettings(const QVariantMap& values) {
    const GlobalSettings before = m_settings->settings();
    GlobalSettings updated = m_settings->settings();
    updateSettingsFromMap(updated, values);
    QString error;
    if (m_settings->setSettings(updated, &error)) {
        const GlobalSettings& applied = m_settings->settings();
        if (before.fps != applied.fps) m_renderer->setFps(applied.fps);
        return true;
    }
    m_owner->setStatusMessage(error.isEmpty() ? QStringLiteral("设置保存失败") : error);
    return false;
}

void PlaybackController::restoreStartupPlayback() {
    const QHash<int, QString> lastApplied = m_playlist->lastAppliedIDs();
    for (auto it = lastApplied.constBegin(); it != lastApplied.constEnd(); ++it) {
        const Wallpaper item = m_playlist->resolveWallpaper(it.value());
        if (!item.isValid()) continue;
        QString error;
        if (m_renderer->render(item, it.key(), renderOptionsFor(item), &error)) {
            m_playlist->setCurrentWallpaper(it.key(), item);
        }
    }
}

} // namespace Mirage
