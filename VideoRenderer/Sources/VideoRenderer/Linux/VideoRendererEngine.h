#pragma once

#include "VideoRendererTypes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class VRVideoManifest;

// Linux playback engine: FFmpeg decodes the video, PulseAudio plays the audio.
// Decoded RGB frames are handed to the viewer (GLFW/OpenGL, mirroring
// SceneViewer) via currentFrame(). Deliberately free of Qt Widgets and
// Qt Multimedia.
class VRVideoRendererEngine final {
public:
    struct Callbacks {
        std::function<void(const std::string&)> playbackError;
        std::function<void()> videoDidEnd;
    };

    struct Frame {
        const std::uint8_t* rgb { nullptr };
        int width { 0 };
        int height { 0 };
        int stride { 0 };
        std::uint64_t serial { 0 };
    };

    explicit VRVideoRendererEngine(
        VRVideoEngineConfig config = VRDefaultVideoEngineConfig(),
        Callbacks callbacks = {});
    ~VRVideoRendererEngine();

    VRVideoRendererEngine(const VRVideoRendererEngine&) = delete;
    VRVideoRendererEngine& operator=(const VRVideoRendererEngine&) = delete;

    [[nodiscard]] static VRVideoEngineConfig defaultConfig() noexcept;

    [[nodiscard]] bool openWallpaper(const VRVideoManifest& manifest,
                                     std::string* error = nullptr);

    void play();
    void pause();
    void setVolume(float volume);
    void setMuted(bool muted);
    void setFillMode(VRVideoFillMode fillMode);

    [[nodiscard]] bool loaded() const noexcept;
    [[nodiscard]] float volume() const noexcept;
    [[nodiscard]] bool muted() const noexcept;
    [[nodiscard]] VRVideoFillMode fillMode() const noexcept;

    // Latest decoded frame (RGB24). The buffer stays valid until the next
    // decode step swaps it; callers should upload it before calling again.
    [[nodiscard]] bool currentFrame(Frame& out) const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
