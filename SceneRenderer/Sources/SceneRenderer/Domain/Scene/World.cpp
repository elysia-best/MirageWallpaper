module;

#include <cmath>

module sr.scene;
import eigen;
import nlohmann.json;
import rstd;
import rstd.cppstd;

import sr.fs;

namespace sr
{

// TODO(4b41483): upstream Scene gains a SceneResourceIndex + RebuildResourceIndex
// (+ RenderSceneSnapshot / RenderItemId / render_items) that the render planner
// and VulkanRender consume. The port's Scene still exposes sceneGraph / cameras /
// renderTargets directly and walks the graph per-frame. The full
// resource-index/snapshot model (~700 lines) is deferred until its consumers
// (SceneRenderPlanner, VulkanFrameEngine RenderProgram traversal) are ported.

namespace
{
void delete_vfs(void* p) noexcept { delete static_cast<fs::VFS*>(p); }

float cubic(float p0, float p1, float p2, float p3, float t) {
    float omt = 1.0f - t;
    return omt * omt * omt * p0 + 3.0f * omt * omt * t * p1 + 3.0f * omt * t * t * p2 +
           t * t * t * p3;
}

float animation_frame(const SceneAnimationCurve& curve, double runtime) {
    float fps         = curve.fps > 0.0f ? curve.fps : 30.0f;
    float frame       = static_cast<float>(runtime) * fps;
    int   end         = curve.length;
    auto  absorb_last = [&end](const std::vector<SceneAnimationKey>& keys) {
        if (! keys.empty()) end = std::max(end, keys.back().frame);
    };
    absorb_last(curve.c0);
    absorb_last(curve.c1);
    absorb_last(curve.c2);
    if (end <= 0) return frame;

    bool loop = curve.wraploop || curve.mode == "loop" || curve.mode == "repeat";
    if (loop) {
        frame = std::fmod(frame, static_cast<float>(end));
        if (frame < 0.0f) frame += static_cast<float>(end);
        return frame;
    }
    return std::clamp(frame, 0.0f, static_cast<float>(end));
}

float eval_segment(const SceneAnimationKey& a, const SceneAnimationKey& b, float frame) {
    float dt = static_cast<float>(b.frame - a.frame);
    if (dt <= 0.0f) return b.value;
    float linear_t = std::clamp((frame - static_cast<float>(a.frame)) / dt, 0.0f, 1.0f);
    bool  has_tan  = a.front_enabled || b.back_enabled;
    if (! has_tan) return std::lerp(a.value, b.value, linear_t);

    float p0x = static_cast<float>(a.frame);
    float p3x = static_cast<float>(b.frame);
    float p1x = a.front_enabled ? p0x + a.front_x : p0x + dt / 3.0f;
    float p2x = b.back_enabled ? p3x + b.back_x : p3x - dt / 3.0f;
    float p0y = a.value;
    float p3y = b.value;
    float p1y = a.front_enabled ? p0y + a.front_y : std::lerp(p0y, p3y, 1.0f / 3.0f);
    float p2y = b.back_enabled ? p3y + b.back_y : std::lerp(p0y, p3y, 2.0f / 3.0f);

    if (! (p0x <= p1x && p1x <= p2x && p2x <= p3x)) return std::lerp(a.value, b.value, linear_t);

    float lo = 0.0f;
    float hi = 1.0f;
    for (int i = 0; i < 16; ++i) {
        float mid = (lo + hi) * 0.5f;
        if (cubic(p0x, p1x, p2x, p3x, mid) < frame)
            lo = mid;
        else
            hi = mid;
    }
    return cubic(p0y, p1y, p2y, p3y, (lo + hi) * 0.5f);
}

float eval_axis(const std::vector<SceneAnimationKey>& keys, float frame) {
    if (keys.empty()) return 0.0f;
    if (frame <= static_cast<float>(keys.front().frame)) return keys.front().value;
    for (std::size_t i = 1; i < keys.size(); ++i) {
        if (frame <= static_cast<float>(keys[i].frame))
            return eval_segment(keys[i - 1], keys[i], frame);
    }
    return keys.back().value;
}

Eigen::Vector3f lerp_vec3(const Eigen::Vector3f& a, const Eigen::Vector3f& b, float t) {
    return a + (b - a) * t;
}

SceneCameraLookAtKey eval_lookat_track(const SceneCameraLookAtTrack& track, float frame) {
    if (track.keys.empty()) return {};
    if (frame <= track.keys.front().frame) return track.keys.front();
    for (std::size_t i = 1; i < track.keys.size(); ++i) {
        const auto& a = track.keys[i - 1];
        const auto& b = track.keys[i];
        if (frame > b.frame) continue;
        float dt = b.frame - a.frame;
        float t  = dt > 0.0f ? std::clamp((frame - a.frame) / dt, 0.0f, 1.0f) : 1.0f;
        return {
            .frame  = frame,
            .eye    = lerp_vec3(a.eye, b.eye, t),
            .center = lerp_vec3(a.center, b.center, t),
            .up     = lerp_vec3(a.up, b.up, t).normalized(),
        };
    }
    return track.keys.back();
}

std::optional<SceneCameraLookAtKey>
eval_lookat_tracks(std::span<const SceneCameraLookAtTrack> tracks, double runtime, float fps) {
    float total = 0.0f;
    for (const auto& track : tracks) total += std::max(track.duration, 0.0f);
    if (total <= 0.0f) return std::nullopt;

    float frame = static_cast<float>(runtime) * (fps > 0.0f ? fps : 1.0f);
    frame       = std::fmod(frame, total);
    if (frame < 0.0f) frame += total;

    float offset = 0.0f;
    for (const auto& track : tracks) {
        float duration = std::max(track.duration, 0.0f);
        if (duration <= 0.0f) continue;
        if (frame <= offset + duration) return eval_lookat_track(track, frame - offset);
        offset += duration;
    }
    return eval_lookat_track(tracks.back(), tracks.back().duration);
}

bool shader_values_equal(const ShaderValue& a, const ShaderValue& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

ShaderValue eval_shader_value_animation(const SceneShaderValueAnimation& animation,
                                        double                           runtime) {
    if (! animation.curve || animation.curve->Empty() || animation.base.size() == 0)
        return animation.base;

    std::vector<float> value(animation.base.size());
    for (std::size_t i = 0; i < animation.base.size(); ++i) value[i] = animation.base[i];

    if (value.size() == 1) {
        value[0] = animation.curve->EvaluateScalar(value[0], runtime);
        return ShaderValue(std::span<const float>(value));
    }

    Eigen::Vector3f base { value[0],
                           value.size() > 1 ? value[1] : 0.0f,
                           value.size() > 2 ? value[2] : 0.0f };
    auto            animated = animation.curve->EvaluateVec3(base, runtime);
    value[0]                 = animated.x();
    if (value.size() > 1) value[1] = animated.y();
    if (value.size() > 2) value[2] = animated.z();
    return ShaderValue(std::span<const float>(value));
}
} // namespace

bool SceneAnimationCurve::Empty() const { return c0.empty() && c1.empty() && c2.empty(); }

float SceneAnimationCurve::EvaluateScalar(float base, double runtime) const {
    if (c0.empty()) return base;
    float value = eval_axis(c0, animation_frame(*this, runtime));
    return relative ? base + value : value;
}

Eigen::Vector3f SceneAnimationCurve::EvaluateVec3(const Eigen::Vector3f& base,
                                                  double                 runtime) const {
    if (Empty()) return base;
    float           frame = animation_frame(*this, runtime);
    Eigen::Vector3f value = base;
    if (! c0.empty()) value.x() = relative ? base.x() + eval_axis(c0, frame) : eval_axis(c0, frame);
    if (! c1.empty()) value.y() = relative ? base.y() + eval_axis(c1, frame) : eval_axis(c1, frame);
    if (! c2.empty()) value.z() = relative ? base.z() + eval_axis(c2, frame) : eval_axis(c2, frame);
    return value;
}

void SceneNode::TickFieldAnimations(double runtime) {
    if (m_alpha_curve) SetUserAlpha(m_alpha_curve->EvaluateScalar(m_base_alpha, runtime));
}

bool SceneMaterial::SetShaderValueAnimation(std::string                          uniform_name,
                                            std::shared_ptr<SceneAnimationCurve> curve) {
    if (uniform_name.empty() || ! curve || curve->Empty()) return false;

    ShaderValue base;
    if (auto it = customShader.constValues.find(uniform_name);
        it != customShader.constValues.end()) {
        base = it->second;
    } else if (customShader.shader) {
        if (auto it = customShader.shader->default_uniforms.find(uniform_name);
            it != customShader.shader->default_uniforms.end()) {
            base = ShapeShaderValue(uniform_name, it->second);
        }
    }
    if (base.size() == 0) return false;

    customShader.valueAnimations[std::move(uniform_name)] =
        SceneShaderValueAnimation { .base = base, .curve = std::move(curve) };
    return true;
}

bool SceneMaterial::TickShaderValueAnimations(double runtime) {
    bool changed = false;
    for (auto& [uniform_name, animation] : customShader.valueAnimations) {
        ShaderValue value = eval_shader_value_animation(animation, runtime);
        if (auto it = customShader.constValues.find(uniform_name);
            it != customShader.constValues.end() && shader_values_equal(it->second, value)) {
            continue;
        }
        customShader.constValues[uniform_name] = std::move(value);
        changed                                = true;
    }
    if (changed) customShader.dirty = true;
    return changed;
}

void SceneCameraPath::CaptureViewport() {
    if (! camera) return;
    default_width  = camera->Width();
    default_height = camera->Height();
    default_fov    = camera->Fov();
}

bool SceneCameraPath::ApplyDefault() {
    if (! camera) return false;
    if (default_lookat) {
        camera->SetLookAt(
            default_eye.cast<double>(), default_center.cast<double>(), default_up.cast<double>());
    }
    if (node) {
        node->SetTranslate(default_translate);
        node->SetRotation(default_rotation);
    }
    camera->SetWidth(default_width);
    camera->SetHeight(default_height);
    if (default_fov > 0.0) camera->SetFov(default_fov);
    camera->Update();
    return true;
}

bool SceneCameraPath::Tick(double runtime) {
    if (! camera) return false;
    if (! enabled) return ApplyDefault();

    if (! lookat_tracks.empty()) {
        auto key = eval_lookat_tracks(lookat_tracks, runtime, lookat_fps);
        if (! key) return false;
        camera->SetLookAt(
            key->eye.cast<double>(), key->center.cast<double>(), key->up.cast<double>());
        if (fov_base > 0.0f) camera->SetFov(fov_base);
        camera->Update();
        return true;
    }

    if (! node) return false;
    camera->AttatchNode(node);

    node->SetTranslate(path_translate_bias + origin_curve.EvaluateVec3(origin_base, runtime));
    node->SetRotation(path_rotation_bias + rotation_curve.EvaluateVec3(rotation_base, runtime));

    if (perspective) {
        float fov = fov_curve.EvaluateScalar(fov_base, runtime);
        if (fov > 0.0f) camera->SetFov(fov);
    } else {
        float zoom = zoom_curve.EvaluateScalar(zoom_base, runtime);
        zoom       = std::max(zoom, 0.001f);
        camera->SetWidth(default_width / static_cast<double>(zoom));
        camera->SetHeight(default_height / static_cast<double>(zoom));
    }
    camera->Update();
    return true;
}

Scene::Scene()
    : sceneGraph(rstd::sync::Arc<SceneNode>::make()),
      vfs(nullptr, &delete_vfs),
      paritileSys(std::make_unique<ParticleSystem>(*this)) {}
Scene::~Scene() = default;

void Scene::TickCameraPaths() {
    if (camera_paths.empty()) return;

    std::unordered_map<std::string, bool> has_enabled;
    for (const auto& path : camera_paths) {
        if (path && path->enabled) has_enabled[path->camera_name] = true;
    }

    std::unordered_set<std::string> touched;
    std::unordered_set<std::string> reset;
    for (const auto& path : camera_paths) {
        if (! path || ! path->enabled) continue;
        if (path->Tick(elapsingTime)) touched.insert(path->camera_name);
    }
    for (const auto& path : camera_paths) {
        if (! path || has_enabled[path->camera_name] || reset.contains(path->camera_name)) continue;
        if (path->ApplyDefault()) touched.insert(path->camera_name);
        reset.insert(path->camera_name);
    }

    for (const auto& name : touched) UpdateLinkedCamera(name);
}

void Scene::TickMaterialShaderAnimations() {
    std::unordered_set<SceneMaterial*> visited;
    auto                               tick_material = [&](SceneMaterial* mat) {
        if (mat && visited.insert(mat).second) mat->TickShaderValueAnimations(elapsingTime);
    };
    std::function<void(SceneNode*)> walk = [&](SceneNode* n) {
        if (! n) return;
        if (auto* mesh = n->Mesh()) {
            for (auto& slot : mesh->MaterialSlots()) tick_material(slot.get());
        }
        for (auto& c : n->GetChildren()) walk(c.as_ptr());
    };
    walk(sceneGraph.as_ptr());
    for (auto& pp : post_processes) {
        if (! pp) continue;
        for (auto& step : pp->steps) {
            if (auto* pass = std::get_if<ScenePostProcessPass>(&step)) {
                if (pass->node) walk(pass->node.as_ptr());
            }
        }
    }
}

void Scene::TickNodeFieldAnimations() {
    auto tick_node = [runtime = elapsingTime](auto&                             self,
                                              const rstd::sync::Arc<SceneNode>& node) -> void {
        if (! node) return;
        node->TickFieldAnimations(runtime);
        for (const auto& child : node->GetChildren()) self(self, child);
    };
    tick_node(tick_node, sceneGraph);
}

void Scene::TickTransformUpdaters() {
    for (auto& update : transform_updaters) update(elapsingTime);
}

void Scene::CaptureCameraPathViewports() {
    for (auto& path : camera_paths) {
        if (path) path->CaptureViewport();
    }
}

bool Scene::ApplyUserNodeVisibilityBindings(std::string_view key, const nlohmann::json& property) {
    bool                            requires_graph_rebuild = false;
    std::function<void(SceneNode*)> walk                   = [&](SceneNode* n) {
        if (! n) return;
        if (auto visible = ResolveSceneUserVisibilityBinding(n->VisibleUserBinding(), key, property)) {
            const bool was = n->Visible();
            n->SetVisible(*visible);
            requires_graph_rebuild |= was != n->Visible();
        }
        for (auto& c : n->GetChildren()) walk(c.as_ptr());
    };
    walk(sceneGraph.as_ptr());
    return requires_graph_rebuild;
}

bool Scene::ApplyUserImageEffectVisibilityBindings(std::string_view      key,
                                                   const nlohmann::json& property) {
    bool                                  requires_graph_rebuild = false;
    std::unordered_set<SceneImageEffect*> visited;
    std::function<void(SceneNode*)>       walk = [&](SceneNode* n) {
        if (! n) return;
        if (! n->Camera().empty()) {
            auto camera_it = cameras.find(n->Camera());
            if (camera_it != cameras.end() && camera_it->second->HasImgEffect()) {
                auto& effect_layer = camera_it->second->GetImgEffect();
                for (usize i = 0; i < effect_layer->EffectCount(); ++i) {
                    auto& effect = effect_layer->GetEffect(i);
                    if (! effect || ! visited.insert(effect.get()).second) continue;
                    auto visible =
                        ResolveSceneUserVisibilityBinding(effect->visible_user_binding, key, property);
                    if (! visible) continue;
                    if (effect_layer->SetEffectRuntimeVisible(*effect, *visible)) {
                        requires_graph_rebuild = true;
                    }
                }
            }
        }
        for (auto& c : n->GetChildren()) walk(c.as_ptr());
    };
    walk(sceneGraph.as_ptr());
    return requires_graph_rebuild;
}

bool Scene::ApplyUserLightVisibilityBindings(std::string_view key, const nlohmann::json& property) {
    bool changed = false;
    for (auto& light : lights) {
        if (! light) continue;
        auto visible = ResolveSceneUserVisibilityBinding(light->visibleUserBinding(), key, property);
        if (! visible) continue;
        changed |= light->runtimeVisible() != *visible;
        light->setRuntimeVisible(*visible);
    }
    return changed;
}

bool Scene::ApplyUserCameraPathVisibilityBindings(std::string_view      key,
                                                  const nlohmann::json& property) {
    auto it = camera_path_user_index.find(std::string(key));
    if (it == camera_path_user_index.end()) return false;

    bool changed = false;
    for (auto& path : it->second) {
        if (! path) continue;
        auto enabled = ResolveSceneUserVisibilityBinding(path->visible_user_binding, key, property);
        if (! enabled) continue;
        changed |= path->enabled != *enabled;
        path->SetEnabled(*enabled);
    }
    return changed;
}

} // namespace sr
