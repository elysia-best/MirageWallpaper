#pragma once

#include "VideoManifest.h"
#include "VideoRendererTypes.h"

#include <QWidget>

class QAudioOutput;
class QMediaPlayer;
class QVideoWidget;

class VRVideoRendererEngine final : public QWidget {
    Q_OBJECT

public:
    explicit VRVideoRendererEngine(
        VRVideoEngineConfig config = VRDefaultVideoEngineConfig(), QWidget* parent = nullptr);
    ~VRVideoRendererEngine() override;

    [[nodiscard]] static VRVideoEngineConfig defaultConfig() noexcept;
    [[nodiscard]] bool openWallpaper(const VRVideoManifest& manifest, QString* error = nullptr);

    void play();
    void pause();
    void setVolume(float volume);
    void setMuted(bool muted);
    void setFillMode(VRVideoFillMode fillMode);

    [[nodiscard]] QMediaPlayer* player() const noexcept { return m_player; }
    [[nodiscard]] bool loaded() const noexcept { return m_loaded; }
    [[nodiscard]] float volume() const noexcept { return m_volume; }
    [[nodiscard]] bool muted() const noexcept { return m_muted; }
    [[nodiscard]] VRVideoFillMode fillMode() const noexcept { return m_fillMode; }

signals:
    void playbackError(const QString& message);

private:
    QMediaPlayer* m_player = nullptr;
    QAudioOutput* m_audioOutput = nullptr;
    QVideoWidget* m_videoWidget = nullptr;
    bool m_loaded = false;
    float m_volume = 1.0f;
    bool m_muted = false;
    VRVideoFillMode m_fillMode = VRVideoFillModeCover;
    bool m_autoplay = true;
};
