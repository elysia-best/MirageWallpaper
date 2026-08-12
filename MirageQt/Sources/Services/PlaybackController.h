#pragma once

#include "Services/RendererController.h"
#include "Services/WallpaperRuntimeStore.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

namespace Mirage {

class GlobalSettingsService;
class MirageController;
class PlaylistManager;

// 渲染与播放控制：对应 macOS 版 RendererController.swift 的播放编排部分。
// 负责把壁纸渲染到显示器（单屏/全屏）、播放列表当前项联动、运行时属性
// （音量/速度/填充）应用、启动恢复与显示器状态序列化。
// 依赖 owner（MirageController）提供壁纸查找与选中状态。
class PlaybackController : public QObject {
    Q_OBJECT
public:
    Q_SIGNAL void pausedChanged(bool paused);

    PlaybackController(GlobalSettingsService* settings,
                       RendererController* renderer,
                       WallpaperRuntimeStore* runtimeStore,
                       PlaylistManager* playlist,
                       MirageController* owner,
                       QObject* parent = nullptr);

    QVariantList displays() const;
    double selectedVolume() const;
    double selectedSpeed() const;
    QString selectedFillMode() const;

    void apply(const Wallpaper& item, bool allScreens);
    void applySelectedToScreen(int screen);
    void playPlaylistItem(const Wallpaper& item);
    void stopWallpapers();
    void stopScreen(int screen);
    void pauseWallpapers();
    void resumeWallpapers();
    void muteWallpapers();
    void setSelectedVolume(double volume);
    void setSelectedSpeed(double speed);
    void setSelectedFillMode(const QString& mode);
    void setSelectedProperty(const QString& key, const QVariant& value);
    void resetSelectedProperties();
    void previewFps(int fps);
    bool applySettings(const QVariantMap& values);
    void restoreStartupPlayback();

private:
    RenderOptions renderOptionsFor(const Wallpaper& item) const;

    GlobalSettingsService* m_settings;
    RendererController* m_renderer;
    WallpaperRuntimeStore* m_runtimeStore;
    PlaylistManager* m_playlist;
    MirageController* m_owner;
    bool m_paused = false;
};

} // namespace Mirage
