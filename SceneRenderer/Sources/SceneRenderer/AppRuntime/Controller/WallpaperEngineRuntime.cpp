module;

#include <chrono>
#include <cstdlib>
#include <rstd/macro.hpp>

module sr.scene_wallpaper;
import sr.types;
import sr.utils;
import sr.scene;

import eigen;
import nlohmann.json;
import rstd.log;
import rstd.cppstd;
import wavsen.audio;
import sr.fs;
import sr.message_loop;
import sr.timer;
import sr.pkg.parse;
import sr.pkg_fs;
import sr.rgraph;
import sr.script;
import sr.vulkan_render;
import sr.spec_texs;

using namespace sr;

namespace sr
{

// TODO(4b41483): upstream SceneWallpaper reroutes the per-frame render path
// through a RenderSceneSnapshot produced by Scene::RebuildResourceIndex, and
// drives RenderProgram / render_items + ShaderReflectionCache through the
// render thread. The port has no snapshot infrastructure, so the
// pre-4b41483 sceneGraph-driven RenderDraw path is retained. Port the
// snapshot plumbing together with SceneRenderPlanner + VulkanFrameEngine.

// ---- Render-thread messages -------------------------------------------------

struct RenderInit {
    std::shared_ptr<RenderInitInfo> info;
};
struct RenderSetScene {
    std::shared_ptr<Scene> scene;
};
struct RenderSetFillMode {
    FillMode mode;
};
struct RenderSetSpeed {
    float speed;
};
struct RenderSetUserProperty {
    std::string    key;
    nlohmann::json property;
};
struct RenderSetMediaStatus {
    MediaStatus status;
};
struct RenderStop {
    bool stop;
};
struct RenderDraw {};
struct RenderSwapchainReady {
    bool     ready;
    uint32_t width;
    uint32_t height;
};

// Wrapped in a non-std struct so the rstd channel's internal `addressof`
// calls don't fall into ADL ambiguity with std::addressof when the element
// type sits in namespace std.
struct RenderMsg {
    std::variant<RenderInit, RenderSetScene, RenderSetFillMode, RenderSetSpeed,
                 RenderSetUserProperty, RenderSetMediaStatus, RenderStop, RenderDraw,
                 RenderSwapchainReady>
        v;
};

// ---- Main-thread messages ---------------------------------------------------

struct MainLoadScene {};
struct MainStop {
    bool     stop;
    uint32_t fade_ms { 0 };
    bool     scale_audio { false };
};
struct MainPauseAudio {
    uint64_t generation { 0 };
};
struct MainFirstFrame {};
struct MainConfigure {
    SceneWallpaperConfig config;
};
struct MainSetFps {
    uint32_t fps { 0 };
};
struct MainSetVolume {
    float volume { 1.0f };
};
struct MainSetVolumeScale {
    float    scale { 1.0f };
    uint32_t fade_ms { 0 };
};
struct MainSetMuted {
    bool muted { false };
};
struct MainSetFillMode {
    FillMode mode { FillMode::ASPECTCROP };
};
struct MainSetSpeed {
    float speed { 1.0f };
};
struct MainSetUserProperty {
    std::string    key;
    nlohmann::json value;
};
struct MainSetFirstFrameCallback {
    FirstFrameCallback cb;
};

struct MainMsg {
    std::variant<MainLoadScene, MainConfigure, MainSetFps, MainSetVolume, MainSetVolumeScale,
                 MainSetMuted, MainSetFillMode, MainSetSpeed, MainSetUserProperty,
                 MainSetFirstFrameCallback, MainStop, MainPauseAudio, MainFirstFrame>
        v;
};

namespace
{

bool PerfDiagEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("SCENERENDERER_PERF_DIAG");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

using PerfClock = std::chrono::steady_clock;

double MsSince(PerfClock::time_point begin, PerfClock::time_point end = PerfClock::now()) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

nlohmann::json MakeUserPropertyDescriptor(nlohmann::json value) {
    if (value.is_object() && value.contains("value")) return value;
    nlohmann::json out = nlohmann::json::object();
    out["value"]       = std::move(value);
    return out;
}

nlohmann::json ParseSettingJsonValue(std::string_view raw) {
    auto parsed = nlohmann::json::parse(raw,
                                        /*callback=*/nullptr,
                                        /*allow_exceptions=*/false,
                                        /*ignore_comments=*/true);
    if (! parsed.is_discarded()) return parsed;
    return std::string(raw);
}

nlohmann::json RawUserProperty(std::string_view value) {
    return MakeUserPropertyDescriptor(ParseSettingJsonValue(value));
}

nlohmann::json JsonUserProperty(nlohmann::json value) {
    return MakeUserPropertyDescriptor(std::move(value));
}

nlohmann::json InitialUserProperty(nlohmann::json value) {
    if (value.is_string()) return RawUserProperty(value.get<std::string>());
    return MakeUserPropertyDescriptor(std::move(value));
}

constexpr std::string_view kSchemeColorKey          = "schemecolor";
constexpr std::string_view kSrSchemeColorKey = "scenerenderer.scheme_color";

std::string CanonicalUserPropertyKey(std::string_view key) {
    if (key == kSrSchemeColorKey) return std::string(kSchemeColorKey);
    return std::string(key);
}

// Parse a "r g b" / "r g b a" / "x y z w ..." space-separated float string into
// a small float vector. Trailing / leading whitespace is tolerated. Returns
// false when no numbers parse — caller treats as coercion failure.
bool ParseFloatList(std::string_view s, std::vector<float>& out) {
    out.clear();
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        if (i >= s.size()) break;
        std::size_t start = i;
        while (i < s.size() && s[i] != ' ' && s[i] != '\t') ++i;
        std::string tok(s.substr(start, i - start));
        try {
            out.push_back(std::stof(tok));
        } catch (...) {
            return false;
        }
    }
    return ! out.empty();
}

// Coerce a project.json property entry into a ShaderValue. Returns ok=false
// (with skip_reason) for combo / texture / unsupported types — the handler
// logs and skips those.
struct UserPropertyCoerceResult {
    bool        ok { false };
    ShaderValue value;
    const char* skip_reason { nullptr };
};

UserPropertyCoerceResult CoerceUserPropertyValue(const nlohmann::json& prop) {
    UserPropertyCoerceResult r;

    // Pull the explicit type if present; project.json properties always have
    // one, but inline {"value": ...} descriptors don't.
    std::string type;
    if (prop.is_object() && prop.contains("type") && prop.at("type").is_string()) {
        type = prop.at("type").get<std::string>();
    }

    // Combo / texture / file paths can't write a uniform.
    if (type == "combo") {
        r.skip_reason = "combo type — live #define recompile not implemented";
        return r;
    }
    if (type == "texture" || type == "replacetexture" || type == "file" || type == "textinput") {
        r.skip_reason = "non-uniform property type";
        return r;
    }

    // Find the raw value.
    const nlohmann::json* val_ptr = &prop;
    if (prop.is_object() && prop.contains("value")) val_ptr = &prop.at("value");
    const nlohmann::json& v = *val_ptr;

    if (type == "color") {
        std::vector<float> nums;
        if (v.is_string() && ParseFloatList(v.get<std::string>(), nums) && nums.size() >= 3) {
            r.ok    = true;
            r.value = ShaderValue(std::span<const float>(nums));
            return r;
        }
        r.skip_reason = "color value not a 'r g b[ a]' float string";
        return r;
    }

    // Fallback inference when type is missing.
    if (v.is_boolean()) {
        r.ok    = true;
        float f = v.get<bool>() ? 1.0f : 0.0f;
        r.value = ShaderValue(f);
        return r;
    }
    if (v.is_number()) {
        r.ok    = true;
        r.value = ShaderValue(v.get<float>());
        return r;
    }
    if (v.is_string()) {
        std::vector<float> nums;
        if (ParseFloatList(v.get<std::string>(), nums)) {
            if (nums.size() == 1) {
                r.ok    = true;
                r.value = ShaderValue(nums[0]);
                return r;
            }
            r.ok    = true;
            r.value = ShaderValue(std::span<const float>(nums));
            return r;
        }
        r.skip_reason = "string value isn't parseable as float list";
        return r;
    }
    r.skip_reason = "unsupported JSON value shape";
    return r;
}

void ApplyUserPropertyToClear(Scene& scene, const std::string& key, const nlohmann::json& prop) {
    if (scene.clearColorUserKey.empty()) return;
    if (CanonicalUserPropertyKey(scene.clearColorUserKey) != key) return;
    auto coerced = CoerceUserPropertyValue(prop);
    if (! coerced.ok || coerced.value.size() < 3) return;
    auto clamp01 = [](float n) {
        return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
    };
    scene.clearColor = {
        clamp01(coerced.value[0]),
        clamp01(coerced.value[1]),
        clamp01(coerced.value[2]),
    };
}

// Push a user-property value to every material whose shader declared a
// `u_*` uniform with this material-key. Sets the per-material dirty flag so
// CustomShaderPass picks the new value up next frame. Routes through
// SceneMaterial::SetShaderValue so an active value-animation on the same
// uniform re-bases against the new user value (see TickMaterialShaderAnimations).
void ApplyUserPropertyToShaders(Scene& scene, const std::string& key, const nlohmann::json& prop) {
    auto it = scene.shader_user_var_index.find(key);
    if (it == scene.shader_user_var_index.end()) return;

    auto coerced = CoerceUserPropertyValue(prop);
    if (! coerced.ok) {
        rstd_warn("user property '{}' skipped: {}",
                  key,
                  coerced.skip_reason ? coerced.skip_reason : "unknown");
        return;
    }
    for (auto& [material, uniform_name] : it->second) {
        if (! material) continue;
        material->SetShaderValue(uniform_name, coerced.value);
    }
}

float CurrentImagePropertyAlpha(SceneNode* node) {
    if (! node) return 1.0f;
    return node->IsAlphaOverridden() ? node->EffectiveAlpha() : node->BaseAlpha();
}

Eigen::Vector3f CurrentImagePropertyColor(SceneNode* node) {
    if (! node) return { 1.0f, 1.0f, 1.0f };
    return node->IsColorOverridden() ? node->Color() : node->BaseColor();
}

bool MaterialHasShaderUniform(const SceneMaterial& material, std::string_view uniform_name) {
    const std::string name(uniform_name);
    if (material.customShader.constValues.contains(name)) return true;
    if (material.customShader.shader &&
        material.customShader.shader->default_uniforms.contains(name))
        return true;
    return false;
}

void ApplyUserPropertyToImageColor(Scene& scene, const std::string& key,
                                   const nlohmann::json& prop) {
    auto it = scene.image_color_user_index.find(key);
    if (it == scene.image_color_user_index.end()) return;

    auto coerced = CoerceUserPropertyValue(prop);
    if (! coerced.ok || coerced.value.size() < 3) return;

    Eigen::Vector3f color { coerced.value[0], coerced.value[1], coerced.value[2] };
    for (const auto& binding : it->second) {
        if (binding.node) binding.node->SetColor(color);

        std::array<float, 3> color3 { color.x(), color.y(), color.z() };
        for (auto* material : binding.materials) {
            if (! material) continue;
            const bool  has_user_alpha = MaterialHasShaderUniform(*material, G_USERALPHA);
            const float alpha          = has_user_alpha && binding.node
                                             ? binding.node->BaseAlpha()
                                             : CurrentImagePropertyAlpha(binding.node);
            std::array<float, 4> color4 { color.x(), color.y(), color.z(), alpha };
            if (MaterialHasShaderUniform(*material, G_COLOR4)) {
                material->customShader.constValues[std::string(G_COLOR4)] = color4;
                material->customShader.dirty                   = true;
            }
            if (MaterialHasShaderUniform(*material, G_COLOR)) {
                material->customShader.constValues[std::string(G_COLOR)] = color3;
                material->customShader.dirty                  = true;
            }
        }
    }
}

// Push a user-property alpha into every image whose `alpha` field carried a
// `user` binding. Routes the value into the material's `g_UserAlpha` (if
// declared), falling back to `g_Alpha` and finally `g_Color4.a` for materials
// that only expose the combined tint uniform. Also pins the node's user-alpha
// so EffectiveAlpha() reflects the new value for downstream consumers
// (visibility multiplication, puppet compositing).
void ApplyUserPropertyToImageAlpha(Scene& scene, const std::string& key,
                                   const nlohmann::json& prop) {
    auto it = scene.image_alpha_user_index.find(key);
    if (it == scene.image_alpha_user_index.end()) return;

    auto coerced = CoerceUserPropertyValue(prop);
    if (! coerced.ok || coerced.value.size() < 1) return;

    const float alpha = std::clamp(coerced.value[0], 0.0f, 1.0f);
    for (const auto& binding : it->second) {
        if (binding.node) binding.node->SetUserAlpha(alpha);

        Eigen::Vector3f      color = CurrentImagePropertyColor(binding.node);
        std::array<float, 4> color4 { color.x(), color.y(), color.z(), alpha };
        for (auto* material : binding.materials) {
            if (! material) continue;
            const bool has_user_alpha = MaterialHasShaderUniform(*material, G_USERALPHA);
            if (has_user_alpha) {
                material->customShader.constValues[std::string(G_USERALPHA)] = alpha;
                material->customShader.dirty                      = true;
            }
            if (MaterialHasShaderUniform(*material, G_ALPHA)) {
                material->customShader.constValues[std::string(G_ALPHA)] = alpha;
                material->customShader.dirty                  = true;
            }
            if (! has_user_alpha && MaterialHasShaderUniform(*material, G_COLOR4)) {
                material->customShader.constValues[std::string(G_COLOR4)] = color4;
                material->customShader.dirty                   = true;
            }
        }
    }
}

// Push a user-property value into every particle subsystem whose
// instanceoverride was authored as `{user:"<key>", value:...}` for one of
// its fields. The override sits behind a shared_ptr; mutating it through the
// scene-wide binding index is observed by every initializer / operator
// closure on next emission.
void ApplyUserPropertyToParticles(Scene& scene, const std::string& key,
                                  const nlohmann::json& prop) {
    auto it = scene.particle_user_var_index.find(key);
    if (it == scene.particle_user_var_index.end()) return;

    auto coerced = CoerceUserPropertyValue(prop);
    if (! coerced.ok) return;

    auto write_scalar = [&](float& dst) {
        if (coerced.value.size() >= 1) dst = coerced.value[0];
    };
    auto write_vec3 = [&](std::array<float, 3>& dst, float scale) {
        if (coerced.value.size() < 3) return;
        dst = { coerced.value[0] * scale, coerced.value[1] * scale, coerced.value[2] * scale };
    };

    for (auto& b : it->second) {
        if (! b.state) continue;
        auto*              st = static_cast<sr::wpscene::ParticleInstanceoverride*>(b.state.get());
        const std::string& f  = b.field;
        if (f == "alpha")
            write_scalar(st->alpha);
        else if (f == "size")
            write_scalar(st->size);
        else if (f == "lifetime")
            write_scalar(st->lifetime);
        else if (f == "rate")
            write_scalar(st->rate);
        else if (f == "speed")
            write_scalar(st->speed);
        else if (f == "count")
            write_scalar(st->count);
        else if (f == "brightness")
            write_scalar(st->brightness);
        else if (f == "color") {
            // `color` is 0..255 in the JSON; the init op divides by 255.
            write_vec3(st->color, 255.0f);
            st->overColor = true;
        } else if (f == "colorn") {
            write_vec3(st->colorn, 1.0f);
            st->overColorn = true;
        } else if (f.starts_with("controlpoint") && ! f.starts_with("controlpointangle")) {
            int idx = -1;
            try {
                idx = std::stoi(f.substr(std::string_view("controlpoint").size()));
            } catch (...) {
            }
            if (idx >= 0 && idx < 8) write_vec3(st->controlpoint[idx], 1.0f);
        } else if (f.starts_with("controlpointangle")) {
            int idx = -1;
            try {
                idx = std::stoi(f.substr(std::string_view("controlpointangle").size()));
            } catch (...) {
            }
            if (idx >= 0 && idx < 8) write_vec3(st->controlpointangle[idx], 1.0f);
        }
    }
}

void ApplyUserPropertyToSoundVolume(Scene& scene, const std::string& key,
                                    const nlohmann::json& prop) {
    auto it = scene.sound_volume_user_index.find(key);
    if (it == scene.sound_volume_user_index.end()) return;

    auto coerced = CoerceUserPropertyValue(prop);
    if (! coerced.ok || coerced.value.size() < 1) return;
    const float volume = std::clamp(coerced.value[0], 0.0f, 1.0f);
    for (auto& control : it->second) {
        if (control) control->SetVolume(volume);
    }
}

void ApplyUserPropertyToCameraParallax(Scene& scene, const std::string& key,
                                       const nlohmann::json& prop) {
    auto it = scene.camera_parallax_user_var_index.find(key);
    if (it == scene.camera_parallax_user_var_index.end() || ! scene.shaderValueUpdater) return;

    auto coerced = CoerceUserPropertyValue(prop);
    if (! coerced.ok || coerced.value.size() < 1) return;

    float value = coerced.value[0];
    for (const auto& field : it->second) {
        if (field == "cameraparallaxmouseinfluence")
            scene.shaderValueUpdater->SetCameraParallaxMouseInfluence(value);
    }
}

void ApplyUserPropertyToCameraShake(Scene& scene, const std::string& key,
                                    const nlohmann::json& prop) {
    auto it = scene.camera_shake_user_var_index.find(key);
    if (it == scene.camera_shake_user_var_index.end() || ! scene.shaderValueUpdater) return;

    auto coerced = CoerceUserPropertyValue(prop);
    if (! coerced.ok || coerced.value.size() < 1) return;

    float value = coerced.value[0];
    for (const auto& field : it->second) {
        if (field == "camerashake")
            scene.shaderValueUpdater->SetCameraShakeEnabled(value >= 0.5f);
        else if (field == "camerashakeamplitude")
            scene.shaderValueUpdater->SetCameraShakeAmplitude(value);
        else if (field == "camerashakespeed")
            scene.shaderValueUpdater->SetCameraShakeSpeed(value);
        else if (field == "camerashakeroughness")
            scene.shaderValueUpdater->SetCameraShakeRoughness(value);
    }
}

void ApplyUserPropertyToCameraPath(Scene& scene, const std::string& key,
                                   const nlohmann::json& prop) {
    scene.ApplyUserCameraPathVisibilityBindings(key, prop);
}

bool ApplyUserPropertyToNodeVisibility(Scene& scene, const std::string& key,
                                       const nlohmann::json& prop) {
    return scene.ApplyUserNodeVisibilityBindings(key, prop);
}

// Bridge the host-facing MediaStatus (sr::MediaStatus) to the script-facing
// one (sr::script::MediaStatus). The two are field-for-field identical today;
// the copy keeps the host layer from depending on the script module's type.
sr::script::MediaStatus ToScriptMediaStatus(const MediaStatus& status) {
    return sr::script::MediaStatus { .state            = status.state,
                                     .title            = status.title,
                                     .artist           = status.artist,
                                     .album            = status.album,
                                     .album_artist     = status.album_artist,
                                     .art_url          = status.art_url,
                                     .previous_art_url = status.previous_art_url };
}

void MergeProjectUserProperties(const std::filesystem::path&                     project_dir,
                                std::unordered_map<std::string, nlohmann::json>& out) {
    const auto    project_path = project_dir / "project.json";
    std::ifstream is(project_path);
    if (! is) return;

    auto j = nlohmann::json::parse(is,
                                   /*callback=*/nullptr,
                                   /*allow_exceptions=*/false,
                                   /*ignore_comments=*/true);
    if (j.is_discarded()) return;
    auto gen = j.find("general");
    if (gen == j.end() || ! gen->is_object()) return;
    auto props = gen->find("properties");
    if (props == gen->end() || ! props->is_object()) return;

    for (auto it = props->begin(); it != props->end(); ++it) {
        std::string key = CanonicalUserPropertyKey(it.key());
        if (out.contains(key)) continue;
        out.emplace(std::move(key), MakeUserPropertyDescriptor(it.value()));
    }
}

std::unordered_map<std::string, nlohmann::json>
NormalizeUserProperties(const std::unordered_map<std::string, nlohmann::json>& input) {
    std::unordered_map<std::string, nlohmann::json> out;
    out.reserve(input.size());
    for (const auto& [key, value] : input) {
        std::string canonical = CanonicalUserPropertyKey(key);
        if (key == canonical || ! out.contains(canonical)) {
            out[std::move(canonical)] = InitialUserProperty(value);
        }
    }
    return out;
}

} // namespace

using MainSender   = msgloop::MessageLoop<MainMsg>::Sender;
using RenderSender = msgloop::MessageLoop<RenderMsg>::Sender;

class SceneRenderController;

class SceneRuntimeController {
public:
    SceneRuntimeController();
    ~SceneRuntimeController();

    bool init();
    auto renderController() const { return m_render_controller.get(); }
    bool inited() const { return m_inited; }

    MainSender   mainSender() { return m_main_loop.sender(); }
    RenderSender renderSender() { return m_render_loop.sender(); }

    void on(MainLoadScene&&);
    void on(MainConfigure&&);
    void on(MainSetFps&&);
    void on(MainSetVolume&&);
    void on(MainSetVolumeScale&&);
    void on(MainSetMuted&&);
    void on(MainSetFillMode&&);
    void on(MainSetSpeed&&);
    void on(MainSetUserProperty&&);
    void on(MainSetFirstFrameCallback&&);
    void on(MainStop&&);
    void on(MainPauseAudio&&);
    void on(MainFirstFrame&&);

    bool isGenGraphviz() const { return m_config.graphviz; }

    void setOnClearColor(ClearColorCallback cb) { m_clear_color_cb = std::move(cb); }

private:
    void loadScene();

    bool m_inited { false };

    SceneWallpaperConfig                            m_config;
    std::unordered_map<std::string, nlohmann::json> m_user_properties;

    WallpaperSceneCompiler                                m_scene_parser;
    std::unique_ptr<wavsen::audio::SoundManager> m_sound_manager;
    FirstFrameCallback                           m_first_frame_callback;
    ClearColorCallback                           m_clear_color_cb;
    uint64_t                                     m_audio_pause_generation { 0 };

    msgloop::MessageLoop<MainMsg>          m_main_loop;
    msgloop::MessageLoop<RenderMsg>        m_render_loop;
    std::unique_ptr<SceneRenderController> m_render_controller;
};

class SceneRenderController {
public:
    explicit SceneRenderController(SceneRuntimeController& main): m_main(main) {
        // Best-effort: a failing init just leaves snapshots returning false
        // and audio_average at zeros — wallpapers still render fine.
        (void)m_audio_capture.init();
    }
    ~SceneRenderController() {
        m_render->destroy();
        rstd_info("render handler deleted");
    }

    void on(RenderInit&&);
    void on(RenderSetScene&&);
    void on(RenderSetFillMode&&);
    void on(RenderSetSpeed&&);
    void on(RenderSetUserProperty&&);
    void on(RenderSetMediaStatus&&);
    void on(RenderStop&&);
    void on(RenderDraw&&);
    void on(RenderSwapchainReady&&);

    ExSwapchain* exSwapchain() const { return m_render->exSwapchain(); }
    vulkan::VulkanRender* render() const { return m_render.get(); }

    bool renderInited() const { return m_render->inited(); }

    void setMousePos(double x, double y) { m_mouse_pos.store(std::array { (float)x, (float)y }); }

    // Edge-events for the cursor button stream. Each call from the input
    // thread sets/clears the held bit and records the edge so the next
    // TickSceneScripts can fire cursorDown/Up. fetch_or guards against
    // press-release-press coalescing between ticks (rare).
    void setMouseButton(int button, bool down) {
        if (button < 0 || button > 31) return;
        const uint32_t mask = 1u << button;
        if (down) {
            m_buttons_down.fetch_or(mask);
            m_buttons_pressed.fetch_or(mask);
        } else {
            m_buttons_down.fetch_and(~mask);
            m_buttons_released.fetch_or(mask);
        }
    }
    void     setMouseInWindow(bool in) { m_cursor_in_window.store(in); }
    uint32_t buttonsDown() const { return m_buttons_down.load(); }
    uint32_t consumePressed() { return m_buttons_pressed.exchange(0); }
    uint32_t consumeReleased() { return m_buttons_released.exchange(0); }
    bool     cursorInWindow() const { return m_cursor_in_window.load(); }

    void setSenders(RenderSender render_tx, MainSender main_tx) {
        m_render_tx.emplace(std::move(render_tx));
        m_main_tx.emplace(std::move(main_tx));
    }

    // Drop every Sender clone owned by this handler so the render channel
    // can disconnect at shutdown. The swapchain callback's sender is held
    // through `m_swapchain_tx` (strong) + a weak_ptr in the lambda — clearing
    // the strong ref turns the lambda into a no-op.
    void clearSenders() {
        m_swapchain_tx.reset();
        m_render_tx.reset();
        m_main_tx.reset();
    }

    FrameTimer frame_timer { [] {
    } };
    FpsCounter fps_counter;

private:
    SceneRuntimeController& m_main;

    std::unique_ptr<vulkan::VulkanRender> m_render { std::make_unique<vulkan::VulkanRender>() };
    std::shared_ptr<Scene>                m_scene { nullptr };
    std::unique_ptr<rg::RenderGraph>      m_rg { nullptr };
    float                                 m_speed { 1.0f };
    FillMode                              m_fillmode { FillMode::ASPECTCROP };
    bool                                  m_stopped { false };

    std::atomic<std::array<float, 2>> m_mouse_pos { std::array { 0.5f, 0.5f } };
    std::atomic<uint32_t>             m_buttons_down { 0 };
    std::atomic<uint32_t>             m_buttons_pressed { 0 };
    std::atomic<uint32_t>             m_buttons_released { 0 };
    std::atomic<bool>                 m_cursor_in_window { false };

    std::optional<RenderSender> m_render_tx;
    std::optional<MainSender>   m_main_tx;

    // Strong ref kept here, weak copy captured by the swapchain callback;
    // nulling this out at shutdown lets the callback short-circuit so the
    // render channel can actually reach Err on recv().
    std::shared_ptr<RenderSender> m_swapchain_tx;

    wavsen::audio::AudioCapture m_audio_capture;
};

// ---- SceneRenderController message handlers ---------------------------------

void SceneRenderController::on(RenderStop&& m) {
    m_stopped = m.stop;
    if (m.stop)
        frame_timer.Stop();
    else
        frame_timer.Run();
}

void SceneRenderController::on(RenderDraw&&) {
    const bool perf_diag = PerfDiagEnabled();
    auto       perf_frame_begin = PerfClock::now();
    double     perf_scene_ms {};
    double     perf_particle_ms {};
    double     perf_video_ms {};
    double     perf_font_ms {};
    double     perf_draw_ms {};
    frame_timer.FrameBegin();
    if (m_rg) {
        m_scene->shaderValueUpdater->FrameBegin();
        {
            auto pos                 = m_mouse_pos.load();
            m_scene->pointerPosition = pos;
            m_scene->shaderValueUpdater->MouseInput(pos[0], pos[1]);
        }
        // Drive any per-Scene scenescripts before particle emission.
        // Scripts mutate SceneNode transforms (scale/origin/angles) so
        // they need to run before the matrix-derivation in the
        // shaderValueUpdater's per-frame uniform pass — that's already
        // what FrameBegin set up; UpdateUniforms runs inside drawFrame.
        // The runtime is a no-op when no ScriptScene is installed.
        {
            auto perf_begin = PerfClock::now();
            sr::script::FrameInputs fi;
            fi.frametime = static_cast<float>(m_scene->frameTime * m_speed);
            fi.runtime   = static_cast<float>(m_scene->elapsingTime);
            fi.canvas_w  = static_cast<float>(m_scene->ortho[0]);
            fi.canvas_h  = static_cast<float>(m_scene->ortho[1]);
            fi.screen_w  = fi.canvas_w;
            fi.screen_h  = fi.canvas_h;
            {
                auto pos    = m_mouse_pos.load();
                fi.cursor_x = pos[0];
                fi.cursor_y = pos[1];
            }
            fi.cursor_in_window       = cursorInWindow();
            fi.mouse_buttons_down     = buttonsDown();
            fi.mouse_buttons_pressed  = consumePressed();
            fi.mouse_buttons_released = consumeReleased();
            wavsen::audio::AudioSpectrum spec;
            const bool                   primed = m_audio_capture.snapshot(spec);
            // Treat as silence if wavsen hasn't published recently. Without
            // this, a disconnected sink / suspended pipeline leaves the
            // last snapshot frozen and bars stick at the last live value.
            // 250 ms grace covers backend batching plus the FFT trigger
            // half-window by a wide margin.
            constexpr std::int64_t kStaleMs = 250;
            const auto             now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::steady_clock::now().time_since_epoch())
                                                .count();
            const bool             stale =
                ! primed || spec.publish_ms == 0 || (now_ms - spec.publish_ms) > kStaleMs;
            if (stale) spec.clear();
            fi.audio_left    = spec.left;
            fi.audio_right   = spec.right;
            fi.audio_average = spec.average;
            for (std::size_t bin = 0; bin < m_scene->audioAverage.size(); ++bin) {
                const auto begin = bin * fi.audio_average.size() / m_scene->audioAverage.size();
                const auto end = (bin + 1) * fi.audio_average.size() / m_scene->audioAverage.size();
                float sum = 0.0f;
                for (std::size_t i = begin; i < end; ++i) sum += fi.audio_average[i];
                const float level = end > begin ? sum / static_cast<float>(end - begin) : 0.0f;
                auto& slot = m_scene->audioAverage[bin];
                const float old = slot.load(std::memory_order_relaxed);
                slot.store(std::max(old * 0.75f, level), std::memory_order_relaxed);
            }
            m_scene->shaderValueUpdater->SetAudioSpectrum(
                std::span<const float, 64>(fi.audio_left),
                std::span<const float, 64>(fi.audio_right));
            m_scene->TickNodeFieldAnimations();
            sr::script::TickSceneScripts(*m_scene, fi);
            m_scene->TickCameraPaths();
            m_scene->TickMaterialShaderAnimations();
            m_scene->TickTransformUpdaters();
            if (perf_diag) perf_scene_ms = MsSince(perf_begin);
        }
        {
            auto perf_begin = PerfClock::now();
            m_scene->paritileSys->Emitt();
            if (perf_diag) perf_particle_ms = MsSince(perf_begin);
        }

        /* Advance video textures (no-op if none) before drawFrame so
         * the new RGBA frame is sampled by the same render pass. */
        {
            auto perf_begin = PerfClock::now();
            m_render->pumpVideoTextures(frame_timer.IdeaTime() * m_speed);
            if (perf_diag) perf_video_ms = MsSince(perf_begin);
        }

        /* Upload any glyph rects the actuators added this tick. Runs after
         * TickSceneScripts (which calls FontFace::Populate) and before
         * drawFrame so newly-rasterised glyphs are visible the same frame. */
        {
            auto perf_begin = PerfClock::now();
            m_render->pumpFontAtlases(*m_scene);
            if (perf_diag) perf_font_ms = MsSince(perf_begin);
        }

        {
            auto perf_begin = PerfClock::now();
            m_render->drawFrame(*m_scene);
            if (perf_diag) perf_draw_ms = MsSince(perf_begin);
        }

        m_scene->PassFrameTime(frame_timer.IdeaTime() * m_speed);

        m_scene->shaderValueUpdater->FrameEnd();

        if (! m_scene->first_frame_ok) {
            m_scene->first_frame_ok = true;
            if (m_main_tx) (void)m_main_tx->send(MainMsg { MainFirstFrame {} });
        }
    }
    frame_timer.FrameEnd();
    if (perf_diag && m_rg) {
        static uint64_t frame_count = 0;
        ++frame_count;
        if ((frame_count % 120u) == 0u) {
            rstd_info("PerfDiag runtime frame={} total_us={} scene_us={} particle_us={} video_us={} font_us={} draw_us={}",
                      frame_count,
                      static_cast<std::uint64_t>(MsSince(perf_frame_begin) * 1000.0),
                      static_cast<std::uint64_t>(perf_scene_ms * 1000.0),
                      static_cast<std::uint64_t>(perf_particle_ms * 1000.0),
                      static_cast<std::uint64_t>(perf_video_ms * 1000.0),
                      static_cast<std::uint64_t>(perf_font_ms * 1000.0),
                      static_cast<std::uint64_t>(perf_draw_ms * 1000.0));
        }
    }
}

void SceneRenderController::on(RenderSetFillMode&& m) {
    m_fillmode = m.mode;
    if (m_scene && renderInited()) {
        m_render->UpdateCameraFillMode(*m_scene, m_fillmode);
    }
}

void SceneRenderController::on(RenderSetScene&& m) {
    m_scene = std::move(m.scene);
    if (m_rg) m_render->clearLastRenderGraph();
    // Drop cached mesh buffers from the previous scene before building the
    // new graph. Swapchain-resize rebuilds (RenderSwapchainReady) reuse the
    // same SceneMesh set, so evict is intentionally not called there.
    m_render->evictUnusedMeshes();
    m_rg = sceneToRenderGraph(*m_scene);

    if (m_main.isGenGraphviz()) m_rg->ToGraphviz("graph.dot");
    m_render->compileRenderGraph(*m_scene, *m_rg);
    m_render->UpdateCameraFillMode(*m_scene, m_fillmode);
}

void SceneRenderController::on(RenderSetSpeed&& m) { m_speed = m.speed; }

void SceneRenderController::on(RenderSetUserProperty&& m) {
    if (! m_scene) return;
    std::string key = CanonicalUserPropertyKey(m.key);
    sr::script::SetSceneUserProperty(*m_scene, key, m.property);
    ApplyUserPropertyToClear(*m_scene, key, m.property);
    ApplyUserPropertyToShaders(*m_scene, key, m.property);
    ApplyUserPropertyToImageColor(*m_scene, key, m.property);
    ApplyUserPropertyToImageAlpha(*m_scene, key, m.property);
    ApplyUserPropertyToParticles(*m_scene, key, m.property);
    ApplyUserPropertyToSoundVolume(*m_scene, key, m.property);
    ApplyUserPropertyToCameraParallax(*m_scene, key, m.property);
    ApplyUserPropertyToCameraShake(*m_scene, key, m.property);
    ApplyUserPropertyToCameraPath(*m_scene, key, m.property);
    bool requires_graph_rebuild = ApplyUserPropertyToNodeVisibility(*m_scene, key, m.property);
    requires_graph_rebuild =
        m_scene->ApplyUserImageEffectVisibilityBindings(key, m.property) || requires_graph_rebuild;
    (void)requires_graph_rebuild;
}

void SceneRenderController::on(RenderSetMediaStatus&& m) {
    if (! m_scene) return;

    // Script-side fanout: dispatch mediaPlaybackChanged / mediaPropertiesChanged
    // / mediaThumbnailChanged to every live field script. This is the
    // primary effect of a media-status push on macOS.
    sr::script::SetSceneMediaStatus(*m_scene, ToScriptMediaStatus(m.status));

    // TODO(port): upstream additionally refreshes the `$mediaThumbnail` /
    // `$mediaPreviousThumbnail` material texture bindings at runtime here
    // (ApplyUserPropertyToMaterialTextures + refreshPreparedMaterialTextures,
    // falling back to rebuildRenderGraph). The port has no runtime material-
    // texture refresh path yet — `on(RenderSetUserProperty)` above likewise
    // skips it — so the art_url is not pushed into scene textures from here.
    // The TextureDecoder external-image + percent-decode support
    // (ParseExternalImage / ResolveExternalImagePath) is in place so that
    // once the refresh path lands, mpris `file://...` art URLs will resolve.
    // For now, scene scripts still receive the thumbnail event with the
    // `thumbnail`/`artUrl` strings and can react in JS.
}

void SceneRenderController::on(RenderInit&& m) {
    m_render->init(std::move(*m.info));

    // Subscribe to ExSwapchain ready/extent/format changes. The
    // callback runs on the render thread (sync for Local, from
    // drainPendingDirective for Bridge); we just relay it as a
    // RenderSwapchainReady message back to ourselves so the actual
    // handling happens through the normal loop path. Format reaches
    // VulkanRender via ExSwapchain::format() directly; no need to
    // round-trip it through this message.
    if (auto* sw = m_render->exSwapchain()) {
        if (m_render_tx) {
            m_swapchain_tx                   = std::make_shared<RenderSender>(*m_render_tx);
            std::weak_ptr<RenderSender> weak = m_swapchain_tx;
            sw->setOnReadyChanged([weak](const ExSwapchainReadyEvent& e) {
                if (auto tx = weak.lock()) {
                    (void)tx->send(
                        RenderMsg { RenderSwapchainReady { e.ready, e.width, e.height } });
                }
            });
        }
    }

    // inited, callback to load scene
    if (m_main_tx) (void)m_main_tx->send(MainMsg { MainLoadScene {} });
}

void SceneRenderController::on(RenderSwapchainReady&& m) {
    if (! m.ready) {
        frame_timer.Stop();
        return;
    }
    bool extent_changed = m_render->onSwapchainReady(m.width, m.height);
    if (extent_changed && m_scene && m_rg) {
        m_render->clearTransientRenderGraphResources();
        m_render->compileRenderGraph(*m_scene, *m_rg);
        m_render->UpdateCameraFillMode(*m_scene, m_fillmode);
    }
    if (m_stopped)
        frame_timer.Stop();
    else
        frame_timer.Run();
}

// ---- SceneRuntimeController message handlers --------------------------------

void SceneRuntimeController::on(MainLoadScene&&) {
    if (m_render_controller->renderInited()) {
        loadScene();
    }
}

void SceneRuntimeController::on(MainConfigure&& m) {
    m_config          = std::move(m.config);
    m_user_properties = NormalizeUserProperties(m_config.user_properties);
    on(MainSetFps { m_config.fps });
    on(MainSetVolume { m_config.volume });
    on(MainSetVolumeScale { 1.0f });
    on(MainSetMuted { m_config.muted });
    on(MainSetFillMode { m_config.fill_mode });
    on(MainSetSpeed { m_config.speed });
    on(MainLoadScene {});
}

void SceneRuntimeController::on(MainSetFps&& m) {
    m_config.fps = m.fps;
    if (m.fps >= 5) m_render_controller->frame_timer.SetRequiredFps(static_cast<uint8_t>(m.fps));
}

void SceneRuntimeController::on(MainSetVolume&& m) {
    m_config.volume = m.volume;
    m_sound_manager->set_volume(m.volume);
}

void SceneRuntimeController::on(MainSetVolumeScale&& m) {
    m_sound_manager->set_volume_scale(m.scale, m.fade_ms);
}

void SceneRuntimeController::on(MainSetMuted&& m) {
    m_config.muted = m.muted;
    m_sound_manager->set_muted(m.muted);
}

void SceneRuntimeController::on(MainSetFillMode&& m) {
    m_config.fill_mode = m.mode;
    (void)m_render_loop.sender().send(RenderMsg { RenderSetFillMode { m.mode } });
}

void SceneRuntimeController::on(MainSetSpeed&& m) {
    m_config.speed = m.speed;
    (void)m_render_loop.sender().send(RenderMsg { RenderSetSpeed { m.speed } });
}

void SceneRuntimeController::on(MainSetUserProperty&& m) {
    nlohmann::json    prop             = MakeUserPropertyDescriptor(std::move(m.value));
    const std::string property         = CanonicalUserPropertyKey(m.key);
    m_config.user_properties[property] = prop;
    m_user_properties[property]        = prop;
    const nlohmann::json prop_for_rt   = prop;
    (void)m_render_loop.sender().send(
        RenderMsg { RenderSetUserProperty { property, prop_for_rt } });
}

void SceneRuntimeController::on(MainSetFirstFrameCallback&& m) {
    m_first_frame_callback = std::move(m.cb);
}

void SceneRuntimeController::on(MainStop&& m) {
    const uint64_t generation = ++m_audio_pause_generation;
    if (m.stop) {
        if (m.scale_audio) m_sound_manager->set_volume_scale(0.0f, m.fade_ms);
        if (m.fade_ms == 0 || ! m.scale_audio) {
            m_sound_manager->pause();
        } else {
            auto     tx    = m_main_loop.sender();
            uint32_t delay = m.fade_ms;
            std::thread([tx = std::move(tx), generation, delay]() mutable {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                (void)tx.send(MainMsg { MainPauseAudio { generation } });
            }).detach();
        }
    } else {
        m_sound_manager->play();
        if (m.scale_audio) m_sound_manager->set_volume_scale(1.0f, m.fade_ms);
    }
    (void)m_render_loop.sender().send(RenderMsg { RenderStop { m.stop } });
}

void SceneRuntimeController::on(MainPauseAudio&& m) {
    if (m.generation == m_audio_pause_generation) m_sound_manager->pause();
}

void SceneRuntimeController::on(MainFirstFrame&&) {
    if (m_first_frame_callback) m_first_frame_callback();
}

void SceneRuntimeController::loadScene() {
    if (m_config.source_pkg_path.empty() || m_config.assets_dir.empty()) return;

    rstd_info("loading scene: {}", m_config.source_pkg_path);

    if (! m_sound_manager->is_inited()) {
        m_sound_manager->init();
    } else {
        m_sound_manager->unmount_all();
    }

    std::shared_ptr<Scene> scene { nullptr };

    // mount assets dir
    std::unique_ptr<fs::VFS> pVfs = std::make_unique<fs::VFS>();
    auto&                    vfs  = *pVfs;
    if (! vfs.IsMounted("assets")) {
        bool sus = vfs.Mount("/assets", fs::CreatePhysicalFs(m_config.assets_dir), "assets");
        if (! sus) {
            rstd_error("Mount assets dir failed");
            return;
        }
    }
    std::filesystem::path pkgPath_fs { m_config.source_pkg_path };
    pkgPath_fs.replace_extension("pkg");
    std::string pkgPath  = pkgPath_fs.native();
    std::string pkgEntry = pkgPath_fs.filename().replace_extension("json").native();
    std::string pkgDir   = pkgPath_fs.parent_path().native();
    std::string scene_id = pkgPath_fs.parent_path().filename().native();
    MergeProjectUserProperties(pkgPath_fs.parent_path(), m_user_properties);

    // load pkgfile. Read pkg version stamp before move-mounting so we can
    // pass it to the scene parser; on fallback (loose dir) we have no
    // version info and use kSceneVersionUnknown.
    wpscene::SceneVersion pkg_v = wpscene::kSceneVersionUnknown;
    auto                  wfs   = fs::WPPkgFs::CreatePkgFs(pkgPath);
    if (wfs) pkg_v = wpscene::ParsePkgVersionStamp(wfs->pkg_version_stamp());
    if (! wfs || ! vfs.Mount("/assets", std::move(wfs))) {
        rstd_info("load pkg file {} failed, fallback to use dir", pkgPath);
        pkg_v = wpscene::kSceneVersionUnknown;
        // load pkg dir
        if (! vfs.Mount("/assets", fs::CreatePhysicalFs(pkgDir))) {
            rstd_error("can't load pkg directory: {}", pkgDir);
            return;
        }
    }
    if (! m_config.cache_dir.empty()) {
        if (! vfs.Mount("/cache", fs::CreatePhysicalFs(m_config.cache_dir, true), "cache")) {
            rstd_error("can't load cache folder: {}", m_config.cache_dir);
        } else {
            rstd_info("cache folder: {}", m_config.cache_dir);
        }
    }

    {
        const std::string base { "/assets/" };
        auto              scene_doc = m_config.scene_document;
        if (! scene_doc) {
            auto loaded = wpscene::LoadSceneDocumentFromVfs(vfs, base + pkgEntry, pkg_v);
            if (loaded) scene_doc = std::make_shared<wpscene::SceneDocument>(std::move(*loaded));
        }
        if (! scene_doc) {
            rstd_error("Not supported scene type");
            return;
        }
        // Hand the (already-merged project.json defaults + any host-supplied
        // overrides) user-property map to the parser so visible-binding
        // pruning sees the user's saved values, not the scene.json defaults.
        m_scene_parser.SetUserProperties(&m_user_properties);
        scene = m_scene_parser.Parse(scene_id, *scene_doc, vfs, *m_sound_manager);
        m_scene_parser.SetUserProperties(nullptr);
        for (const auto& [key, prop] : m_user_properties) {
            ApplyUserPropertyToClear(*scene, key, prop);
            sr::script::SetSceneUserProperty(*scene, key, prop);
        }
        if (! m_config.cache_dir.empty() && scene) {
            std::filesystem::path ls_dir =
                std::filesystem::path(m_config.cache_dir) / "script_localstorage";
            std::error_code ec;
            std::filesystem::create_directories(ls_dir, ec);
            std::string ls_file = (ls_dir / (scene_id + ".json")).native();
            sr::script::SetScenePersistence(*scene, std::move(ls_file));
        }
        scene->vfs.reset(pVfs.release());
        if (! m_config.muted) {
            if (! m_sound_manager->is_inited()) m_sound_manager->init();
            m_sound_manager->play();
        }

        // Surface the parsed clear color before the scene is shipped
        // off to the render thread; downstream callers (the daemon
        // host) need the value to feed `set_config.clear_*`.
        if (m_clear_color_cb) {
            const auto& c = scene->clearColor;
            m_clear_color_cb(c[0], c[1], c[2]);
        }
    }

    auto rtx = m_render_loop.sender();
    (void)rtx.send(RenderMsg { RenderSetScene { scene } });
    // First-frame default push: now that the render thread owns the scene,
    // replay every collected user property (project.json defaults + any
    // mutations the host already pushed during scene load) so the shader
    // cbuffer matches what the host UI displays.
    for (const auto& [key, prop] : m_user_properties) {
        (void)rtx.send(RenderMsg { RenderSetUserProperty { key, prop } });
    }
    // draw first frame
    (void)rtx.send(RenderMsg { RenderDraw {} });
}

bool SceneRuntimeController::init() {
    if (m_inited) return true;

    // Wire render handler senders before starting the loops; otherwise an
    // early RenderInit could fire before they're set.
    m_render_controller->setSenders(m_render_loop.sender(), m_main_loop.sender());

    m_main_loop.start([this](MainMsg&& m) {
        std::visit(
            [this](auto&& v) {
                on(std::move(v));
            },
            std::move(m.v));
    });
    m_render_loop.start([this](RenderMsg&& m) {
        std::visit(
            [this](auto&& v) {
                m_render_controller->on(std::move(v));
            },
            std::move(m.v));
    });

    {
        auto& frameTimer = m_render_controller->frame_timer;
        auto  rtx        = m_render_loop.sender();
        frameTimer.SetCallback([rtx]() mutable {
            (void)rtx.send(RenderMsg { RenderDraw {} });
        });
        frameTimer.SetRequiredFps(15);
        frameTimer.Run();
    }

    m_inited = true;
    return true;
}

SceneRuntimeController::SceneRuntimeController()
    : m_sound_manager(std::make_unique<wavsen::audio::SoundManager>()),
      m_main_loop("main"),
      m_render_loop("render"),
      m_render_controller(std::make_unique<SceneRenderController>(*this)) {}

SceneRuntimeController::~SceneRuntimeController() {
    // Orderly shutdown: drain both loops *before* SceneRenderController dies, so
    // m_render->destroy() doesn't race with an in-flight RenderDraw.
    //
    //   1. Stop the frame timer (joins its thread → no more Draw posts).
    //   2. Replace the timer callback with a no-op so the captured render
    //      Sender clone is released.
    //   3. Drop every Sender clone the render handler holds, including the
    //      strong ref the swapchain callback weak-captures.
    //   4. Stop the render loop — drops engine sender, recv() returns Err
    //      after the in-flight handler returns, thread joins.
    //   5. Same for the main loop.
    //   6. Default member destruction then runs SceneRenderController's dtor with
    //      the render thread already gone, so destroy() is single-threaded.
    if (m_render_controller) {
        m_render_controller->frame_timer.Stop();
        m_render_controller->frame_timer.SetCallback([] {
        });
        m_render_controller->clearSenders();
    }
    m_render_loop.stop();
    m_main_loop.stop();
}

} // namespace sr

SceneWallpaper::SceneWallpaper(): m_runtime(std::make_unique<SceneRuntimeController>()) {}

SceneWallpaper::~SceneWallpaper() = default;

bool SceneWallpaper::inited() const { return m_runtime->inited(); }

bool SceneWallpaper::init() { return m_runtime->init(); }

void SceneWallpaper::initVulkan(RenderInitInfo info) {
    m_offscreen = info.offscreen;
    auto sp     = std::make_shared<RenderInitInfo>(std::move(info));
    (void)m_runtime->renderSender().send(RenderMsg { RenderInit { std::move(sp) } });
}

void SceneWallpaper::play() { (void)m_runtime->mainSender().send(MainMsg { MainStop { false } }); }
void SceneWallpaper::play(uint32_t fade_ms) {
    (void)m_runtime->mainSender().send(MainMsg { MainStop { false, fade_ms, true } });
}
void SceneWallpaper::pause() { (void)m_runtime->mainSender().send(MainMsg { MainStop { true } }); }
void SceneWallpaper::pause(uint32_t fade_ms) {
    (void)m_runtime->mainSender().send(MainMsg { MainStop { true, fade_ms, true } });
}
void SceneWallpaper::requestFrame() {
    (void)m_runtime->renderSender().send(RenderMsg { RenderDraw {} });
}

void SceneWallpaper::mouseInput(double x, double y) {
    m_runtime->renderController()->setMousePos(x, y);
}

void SceneWallpaper::mouseButton(int button, bool down) {
    m_runtime->renderController()->setMouseButton(button, down);
}

void SceneWallpaper::mouseEnter(bool in_window) {
    m_runtime->renderController()->setMouseInWindow(in_window);
}

void SceneWallpaper::configure(SceneWallpaperConfig config) {
    (void)m_runtime->mainSender().send(MainMsg { MainConfigure { std::move(config) } });
}

void SceneWallpaper::setFps(uint32_t fps) {
    (void)m_runtime->mainSender().send(MainMsg { MainSetFps { fps } });
}

void SceneWallpaper::setVolume(float volume) {
    (void)m_runtime->mainSender().send(MainMsg { MainSetVolume { volume } });
}

void SceneWallpaper::setVolumeScale(float scale) { setVolumeScale(scale, 0); }

void SceneWallpaper::setVolumeScale(float scale, uint32_t fade_ms) {
    (void)m_runtime->mainSender().send(MainMsg { MainSetVolumeScale { scale, fade_ms } });
}

void SceneWallpaper::setMuted(bool muted) {
    (void)m_runtime->mainSender().send(MainMsg { MainSetMuted { muted } });
}

void SceneWallpaper::setFillMode(FillMode mode) {
    (void)m_runtime->mainSender().send(MainMsg { MainSetFillMode { mode } });
}

void SceneWallpaper::setSpeed(float speed) {
    (void)m_runtime->mainSender().send(MainMsg { MainSetSpeed { speed } });
}

void SceneWallpaper::setMediaStatus(MediaStatus status) {
    // Media status is transient (no config to mirror), so bypass the main
    // thread and post straight to the render thread that owns the Scene +
    // script runtime — matches upstream's setMediaStatus routing.
    (void)m_runtime->renderSender().send(RenderMsg { RenderSetMediaStatus { std::move(status) } });
}

void SceneWallpaper::setUserPropertyRaw(std::string_view name, std::string value) {
    (void)m_runtime->mainSender().send(
        MainMsg { MainSetUserProperty { std::string(name), RawUserProperty(value) } });
}

void SceneWallpaper::setUserPropertyJson(std::string_view name, nlohmann::json value) {
    (void)m_runtime->mainSender().send(
        MainMsg { MainSetUserProperty { std::string(name), std::move(value) } });
}

void SceneWallpaper::setOnClearColor(ClearColorCallback cb) {
    m_runtime->setOnClearColor(std::move(cb));
}

void SceneWallpaper::setOnFirstFrame(FirstFrameCallback cb) {
    (void)m_runtime->mainSender().send(MainMsg { MainSetFirstFrameCallback { std::move(cb) } });
}

ExSwapchain* SceneWallpaper::exSwapchain() const {
    return m_runtime->renderController()->exSwapchain();
}

bool SceneWallpaper::waitVulkanInited(uint32_t timeout_ms) {
    using clock   = std::chrono::steady_clock;
    auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms);
    auto rh       = m_runtime->renderController();
    while (clock::now() < deadline) {
        if (rh->renderInited()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return rh->renderInited();
}

VkInstance SceneWallpaper::vkInstance() const {
    return m_runtime->renderController()->render()->vkInstance();
}
VkPhysicalDevice SceneWallpaper::vkPhysicalDevice() const {
    return m_runtime->renderController()->render()->vkPhysicalDevice();
}
VkDevice SceneWallpaper::vkDevice() const {
    return m_runtime->renderController()->render()->vkDevice();
}
VkQueue SceneWallpaper::vkGraphicsQueue() const {
    return m_runtime->renderController()->render()->vkGraphicsQueue();
}
uint32_t SceneWallpaper::vkGraphicsQueueFamily() const {
    return m_runtime->renderController()->render()->vkGraphicsQueueFamily();
}
void SceneWallpaper::deviceUuid(uint8_t out[16]) const {
    m_runtime->renderController()->render()->deviceUuid(out);
}
void SceneWallpaper::driverUuid(uint8_t out[16]) const {
    m_runtime->renderController()->render()->driverUuid(out);
}
