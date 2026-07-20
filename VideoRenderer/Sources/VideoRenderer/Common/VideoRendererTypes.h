#pragma once

#include <string_view>

enum VRVideoFillMode : int {
    VRVideoFillModeCover = 0,
    VRVideoFillModeContain = 1,
    VRVideoFillModeStretch = 2,
};

struct VRVideoEngineConfig {
    VRVideoFillMode fillMode = VRVideoFillModeCover;
    float initialVolume = 1.0f;
    bool muted = false;
    bool autoplay = true;
    bool loadFromMemory = false;
};

[[nodiscard]] VRVideoEngineConfig VRDefaultVideoEngineConfig() noexcept;
[[nodiscard]] float VRClampVideoVolume(float value) noexcept;
[[nodiscard]] bool VRParseVideoFillMode(std::string_view value, VRVideoFillMode& out) noexcept;
