module;

#include <rstd/macro.hpp>

module sr.scene;
import eigen;
import sr.spec_texs;
import sr.core;
import rstd.log;
import rstd.cppstd;

using namespace sr;
using namespace Eigen;

struct WPGOption {
    bool thick_format { false };
};

namespace
{
inline void AssignVertexTimes(std::span<float> dst, std::span<const float> src,
                              unsigned num) noexcept {
    const unsigned dst_one_size = dst.size() / num;
    for (unsigned i = 0; i < num; i++) {
        std::copy(src.begin(), src.end(), dst.begin() + i * dst_one_size);
    }
}

inline void AssignVertex(std::span<float> dst, std::span<const float> src, unsigned num) noexcept {
    const unsigned dst_one_size = dst.size() / num;
    const unsigned src_one_size = src.size() / num;
    for (unsigned i = 0; i < num; i++) {
        std::copy_n(src.begin() + i * src_one_size, src_one_size, dst.begin() + i * dst_one_size);
    }
}

inline usize GenParticleData(std::span<const std::unique_ptr<ParticleInstance>> instances,
                             const ParticleRawGenSpecOp& specOp, WPGOption opt,
                             SceneVertexArray& sv) noexcept {
    std::array<float, 32 * 4> storage;

    float* data = storage.data();

    const auto one_size   = sv.OneSize();
    const auto totle_size = 4 * one_size;
    auto       dst        = sv.DynamicWriteData();
    usize      i { 0 };
    for (const auto& inst : instances) {
        if (inst->IsNoLiveParticle()) continue;

        for (const auto& p : inst->Particles()) {
            if (! ParticleModify::LifetimeOk(p)) {
                continue;
            }

            float lifetime = p.lifetime;
            specOp(p, { &lifetime });

            auto  pos  = inst->GetBoundedData().pos + p.position;
            float size = p.size / 2.0f;

            usize offset = 0;

            // pos
            AssignVertexTimes(
                { data + offset, totle_size }, std::array { pos[0], pos[1], pos[2] }, 4);
            offset += 4;
            // TexCoordVec4
            float      rz = p.rotation[2];
            std::array t { 0.0f, 1.0f, rz, size, 1.0f, 1.0f, rz, size,
                           1.0f, 0.0f, rz, size, 0.0f, 0.0f, rz, size };
            AssignVertex({ data + offset, totle_size }, t, 4);
            offset += 4;

            // color
            AssignVertexTimes({ data + offset, totle_size },
                              std::array { p.color[0], p.color[1], p.color[2], p.alpha },
                              4);
            offset += 4;

            if (opt.thick_format) {
                AssignVertexTimes(
                    { data + offset, totle_size },
                    std::array { p.velocity[0], p.velocity[1], p.velocity[2], lifetime },
                    4);
                offset += 4;
            }
            // TexCoordC2
            AssignVertexTimes(
                { data + offset, totle_size }, std::array { p.rotation[0], p.rotation[1] }, 4);

            const usize out_offset = i * totle_size;
            rstd_assert(out_offset + totle_size <= dst.size());
            if (out_offset + totle_size > dst.size()) {
                sv.CommitDynamicVertexCount(i * 4);
                return i;
            }
            std::copy_n(data, totle_size, dst.data() + out_offset);
            ++i;
        }
    }
    sv.CommitDynamicVertexCount(i * 4);
    return i;
}

struct AttrSlot {
    usize offset { 0 };
    bool  enabled { false };
};

inline AttrSlot
FindAttrSlot(const sr::Map<std::string, SceneVertexArray::SceneVertexAttributeOffset>& attrs,
             std::string_view name) noexcept {
    auto it = attrs.find(std::string(name));
    if (it == attrs.end()) return {};
    return { it->second.offset / sizeof(float), true };
}

inline usize GenParticlePointData(std::span<const std::unique_ptr<ParticleInstance>> instances,
                                  const ParticleRawGenSpecOp& specOp, WPGOption opt,
                                  SceneVertexArray& sv) noexcept {
    const auto            one_size = sv.OneSize();
    const auto            attrs    = sv.GetAttrOffsetMap();
    const AttrSlot        position = FindAttrSlot(attrs, WE_IN_POSITION);
    const AttrSlot        texcoord = FindAttrSlot(attrs, WE_IN_TEXCOORDVEC4);
    const AttrSlot        color    = FindAttrSlot(attrs, WE_IN_COLOR);
    const AttrSlot        velocity = FindAttrSlot(attrs, WE_IN_TEXCOORDVEC4C1);
    std::array<float, 16> v {};
    auto                  write3 = [&](AttrSlot slot, float x, float y, float z) noexcept {
        if (! slot.enabled) return;
        v[slot.offset + 0] = x;
        v[slot.offset + 1] = y;
        v[slot.offset + 2] = z;
    };
    auto write4 = [&](AttrSlot slot, float x, float y, float z, float w) noexcept {
        if (! slot.enabled) return;
        v[slot.offset + 0] = x;
        v[slot.offset + 1] = y;
        v[slot.offset + 2] = z;
        v[slot.offset + 3] = w;
    };

    auto  dst = sv.DynamicWriteData();
    usize i { 0 };
    for (const auto& inst : instances) {
        if (inst->IsNoLiveParticle()) continue;

        for (const auto& p : inst->Particles()) {
            if (! ParticleModify::LifetimeOk(p)) continue;

            float lifetime = p.lifetime;
            specOp(p, { &lifetime });

            auto  pos  = inst->GetBoundedData().pos + p.position;
            float size = p.size / 2.0f;

            std::fill(v.begin(), v.begin() + (isize)one_size, 0.0f);
            write3(position, pos[0], pos[1], pos[2]);
            write4(texcoord, p.rotation[0], p.rotation[1], p.rotation[2], size);
            write4(color, p.color[0], p.color[1], p.color[2], p.alpha);

            if (opt.thick_format) {
                write4(velocity, p.velocity[0], p.velocity[1], p.velocity[2], lifetime);
            }

            const usize out_offset = i * one_size;
            rstd_assert(out_offset + one_size <= dst.size());
            if (out_offset + one_size > dst.size()) {
                sv.CommitDynamicVertexCount(i);
                return i;
            }
            std::copy_n(v.data(), one_size, dst.data() + out_offset);
            ++i;
        }
    }
    sv.CommitDynamicVertexCount(i);
    return i;
}

// Writes one record per particle for the genericparticle instanced shader.
// The static corner stream is immutable; all values that evolve with the
// simulation live in this compact second stream.
inline usize GenParticleInstanceData(std::span<const std::unique_ptr<ParticleInstance>> instances,
                                     const ParticleRawGenSpecOp& specOp, WPGOption opt,
                                     SceneVertexArray& sv) noexcept {
    const auto one_size = sv.OneSize();
    // SetParticleMesh owns this stream's schema: FLOAT4 position/size,
    // padded FLOAT3 rotation, FLOAT4 color, and optional FLOAT4 velocity.
    // Avoid rebuilding a string-keyed attribute map for every rendered frame.
    rstd_assert(one_size == (opt.thick_format ? 16u : 12u));

    auto  dst = sv.DynamicWriteData();
    usize i = 0;
    for (const auto& inst : instances) {
        if (inst->IsNoLiveParticle()) continue;
        const Eigen::Vector3f base = inst->GetBoundedData().pos;
        for (const auto& p : inst->Particles()) {
            if (! ParticleModify::LifetimeOk(p)) continue;

            float lifetime = p.lifetime;
            specOp(p, { &lifetime });
            const auto pos = base + p.position;
            std::array<float, 16> v {
                pos[0], pos[1], pos[2], p.size * 0.5f,
                p.rotation[0], p.rotation[1], p.rotation[2], 0.0f,
                p.color[0], p.color[1], p.color[2], p.alpha,
                p.velocity[0], p.velocity[1], p.velocity[2], lifetime,
            };
            const usize out_offset = i * one_size;
            rstd_assert(out_offset + one_size <= dst.size());
            if (out_offset + one_size > dst.size()) {
                sv.CommitDynamicVertexCount(i);
                return i;
            }
            std::copy_n(v.data(), one_size, dst.data() + out_offset);
            ++i;
        }
    }
    sv.CommitDynamicVertexCount(i);
    return i;
}

// Emit one VS-input vertex per consecutive pair of trail-history samples for a
// single live rope-head particle. Each emitted vertex carries the segment's
// endpoints + Catmull-Rom neighbour positions as splineCP0/CP1 (the vert shader
// derives the GS-side tangents from them). Returns the number of segment
// vertices emitted; vertices land at [base_index, base_index+ret).
inline size_t GenRopeParticleSegments(const Particle& p, const ParticleTrail& trail,
                                      const ParticleRawGenSpecOp& specOp, WPGOption opt,
                                      SceneVertexArray& sv, std::span<float> dst,
                                      size_t base_index) {
    const auto            one_size = sv.OneSize();
    std::array<float, 32> v {};
    size_t                emitted = 0;

    if (trail.len < 2) return 0;

    float size     = p.size / 2.0f;
    float lifetime = p.lifetime;
    specOp(p, { &lifetime });

    const float in_ParticleTrailLength = (float)trail.len;

    // trail.At(0) = oldest, At(len-1) = newest. Segments connect (j-1) -> j.
    for (uint16_t j = 1; j < trail.len; j++) {
        Vector3f pre_pos = trail.At((uint16_t)(j - 1));
        Vector3f cur_pos = trail.At(j);
        // Catmull-Rom neighbour samples for the cubic-Bezier subdivision; the
        // GS hardcoded 0.15 factor matches Catmull-Rom tension 0.5 (theoretical
        // 1/6 ≈ 0.167). At the trail endpoints fall back to the segment ends
        // so those boundary spans render flat.
        Vector3f scp = (j >= 2) ? trail.At((uint16_t)(j - 2)) : pre_pos;
        Vector3f ecp = (j + 1 < trail.len) ? trail.At((uint16_t)(j + 1)) : cur_pos;

        const float in_ParticleTrailPosition = (float)(j - 1);

        size_t off = 0;
        v[off++]   = pre_pos[0];
        v[off++]   = pre_pos[1];
        v[off++]   = pre_pos[2];
        v[off++]   = size;
        v[off++]   = cur_pos[0];
        v[off++]   = cur_pos[1];
        v[off++]   = cur_pos[2];
        v[off++]   = in_ParticleTrailLength;
        v[off++]   = scp[0];
        v[off++]   = scp[1];
        v[off++]   = scp[2];
        v[off++]   = in_ParticleTrailPosition;
        if (opt.thick_format) {
            v[off++] = ecp[0];
            v[off++] = ecp[1];
            v[off++] = ecp[2];
            v[off++] = size;
            v[off++] = p.color[0];
            v[off++] = p.color[1];
            v[off++] = p.color[2];
            v[off++] = p.alpha;
        } else {
            v[off++] = ecp[0];
            v[off++] = ecp[1];
            v[off++] = ecp[2];
            v[off++] = 0.0f;
        }
        v[off++] = p.color[0];
        v[off++] = p.color[1];
        v[off++] = p.color[2];
        v[off++] = p.alpha;

        rstd_assert(off == one_size);
        const size_t out_offset = (base_index + emitted) * one_size;
        rstd_assert(out_offset + one_size <= dst.size());
        if (out_offset + one_size > dst.size()) return emitted;
        std::copy_n(v.data(), one_size, dst.data() + out_offset);
        emitted++;
    }
    return emitted;
}

inline size_t GenRopeParticleData(std::span<const std::unique_ptr<ParticleInstance>> instances,
                                  const ParticleRawGenSpecOp& specOp, WPGOption opt,
                                  SceneVertexArray& sv) {
    auto   dst   = sv.DynamicWriteData();
    size_t total = 0;
    for (const auto& inst : instances) {
        if (inst->IsNoLiveParticle()) continue;
        auto         particles = inst->Particles();
        auto         trails    = inst->Trails();
        const size_t n         = std::min(particles.size(), trails.size());
        for (size_t si = 0; si < n; si++) {
            if (! ParticleModify::LifetimeOk(particles[si])) continue;
            total +=
                GenRopeParticleSegments(particles[si], trails[si], specOp, opt, sv, dst, total);
        }
    }
    sv.CommitDynamicVertexCount(total);
    return total;
}

struct RopeQuadAttrSlots {
    AttrSlot position;
    AttrSlot endpoint;
    AttrSlot cp_start;
    AttrSlot cp_end4;
    AttrSlot cp_end3;
    AttrSlot color2;
    AttrSlot uv4;
    AttrSlot uv3;
    AttrSlot color;
};

// CPU rope-quad expansion. Metal/MoltenVK has no geometry-shader stage, so on
// macOS the rope segment fan-out the .geom would do is done here instead: each
// trail segment becomes four vertices (a quad) with per-corner UVs, matching
// the non-GS vertex-shader path selected by SetRopeParticleMesh.
inline size_t GenRopeParticleQuadSegments(const Particle& p, const ParticleTrail& trail,
                                          const ParticleRawGenSpecOp& specOp, WPGOption opt,
                                          const RopeQuadAttrSlots& slots, SceneVertexArray& sv,
                                          std::span<float> dst, size_t base_index) {
    const auto            one_size = sv.OneSize();
    std::array<float, 64> v {};

    auto write2 = [&](AttrSlot slot, float x, float y) noexcept {
        if (! slot.enabled) return;
        v[slot.offset + 0] = x;
        v[slot.offset + 1] = y;
    };
    auto write4 = [&](AttrSlot slot, float x, float y, float z, float w) noexcept {
        if (! slot.enabled) return;
        v[slot.offset + 0] = x;
        v[slot.offset + 1] = y;
        v[slot.offset + 2] = z;
        v[slot.offset + 3] = w;
    };

    rstd_assert(one_size <= v.size());
    if (trail.len < 2) return 0;

    float size     = p.size / 2.0f;
    float lifetime = p.lifetime;
    specOp(p, { &lifetime });

    const float in_ParticleTrailLength = (float)trail.len;
    const std::array<std::array<float, 2>, 4> uvs {
        std::array { 0.0f, 0.0f },
        std::array { 0.0f, 1.0f },
        std::array { 1.0f, 1.0f },
        std::array { 1.0f, 0.0f },
    };

    size_t emitted = 0;
    for (uint16_t j = 1; j < trail.len; j++) {
        Vector3f pre_pos = trail.At((uint16_t)(j - 1));
        Vector3f cur_pos = trail.At(j);
        Vector3f scp     = (j >= 2) ? trail.At((uint16_t)(j - 2)) : pre_pos;
        Vector3f ecp     = (j + 1 < trail.len) ? trail.At((uint16_t)(j + 1)) : cur_pos;

        const float in_ParticleTrailPosition = (float)(j - 1);
        for (usize q = 0; q < uvs.size(); ++q) {
            std::fill(v.begin(), v.begin() + (isize)one_size, 0.0f);
            write4(slots.position, pre_pos[0], pre_pos[1], pre_pos[2], size);
            write4(slots.endpoint, cur_pos[0], cur_pos[1], cur_pos[2], in_ParticleTrailLength);
            write4(slots.cp_start, scp[0], scp[1], scp[2], in_ParticleTrailPosition);
            if (opt.thick_format) {
                write4(slots.cp_end4, ecp[0], ecp[1], ecp[2], size);
                write4(slots.color2, p.color[0], p.color[1], p.color[2], p.alpha);
                write2(slots.uv4, uvs[q][0], uvs[q][1]);
            } else {
                write4(slots.cp_end3, ecp[0], ecp[1], ecp[2], 0.0f);
                write2(slots.uv3, uvs[q][0], uvs[q][1]);
            }
            write4(slots.color, p.color[0], p.color[1], p.color[2], p.alpha);
            const size_t out_offset = ((base_index + emitted) * 4 + q) * one_size;
            rstd_assert(out_offset + one_size <= dst.size());
            if (out_offset + one_size > dst.size()) return emitted;
            std::copy_n(v.data(), one_size, dst.data() + out_offset);
        }
        emitted++;
    }
    return emitted;
}

inline size_t GenRopeParticleQuadData(std::span<const std::unique_ptr<ParticleInstance>> instances,
                                      const ParticleRawGenSpecOp& specOp, WPGOption opt,
                                      SceneVertexArray& sv) {
    const auto attrs = sv.GetAttrOffsetMap();
    const RopeQuadAttrSlots slots {
        .position = FindAttrSlot(attrs, WE_IN_POSITIONVEC4),
        .endpoint = FindAttrSlot(attrs, WE_IN_TEXCOORDVEC4),
        .cp_start = FindAttrSlot(attrs, WE_IN_TEXCOORDVEC4C1),
        .cp_end4  = FindAttrSlot(attrs, WE_IN_TEXCOORDVEC4C2),
        .cp_end3  = FindAttrSlot(attrs, WE_IN_TEXCOORDVEC3C2),
        .color2   = FindAttrSlot(attrs, WE_IN_TEXCOORDVEC4C3),
        .uv4      = FindAttrSlot(attrs, WE_IN_TEXCOORDC4),
        .uv3      = FindAttrSlot(attrs, WE_IN_TEXCOORDC3),
        .color    = FindAttrSlot(attrs, WE_IN_COLOR),
    };
    auto   dst   = sv.DynamicWriteData();
    size_t total = 0;
    for (const auto& inst : instances) {
        if (inst->IsNoLiveParticle()) continue;
        auto         particles = inst->Particles();
        auto         trails    = inst->Trails();
        const size_t n         = std::min(particles.size(), trails.size());
        for (size_t si = 0; si < n; si++) {
            if (! ParticleModify::LifetimeOk(particles[si])) continue;
            total +=
                GenRopeParticleQuadSegments(particles[si], trails[si], specOp, opt, slots, sv, dst,
                                            total);
        }
    }
    sv.CommitDynamicVertexCount(total * 4);
    return total;
}

inline void updateIndexArray(uint32_t index, size_t count, SceneIndexArray& iarray) noexcept {
    constexpr size_t single_size = 6;
    uint32_t         cv          = index * 4;

    std::array<uint32_t, single_size> single;
    // 0 1 3
    // 1 2 3
    single[0] = cv;
    single[1] = cv + 1;
    single[2] = cv + 3;
    single[3] = cv + 1;
    single[4] = cv + 2;
    single[5] = cv + 3;
    // every particle
    for (uint32_t i = index; i < count; i++) {
        iarray.Assign(i * single_size, single);
        for (auto& x : single) x += 4;
    }
}
} // namespace

void WPParticleRawGener::GenGLData(std::span<const std::unique_ptr<ParticleInstance>> instances,
                                   SceneMesh& mesh, ParticleRawGenSpecOp& specOp) {
    if (mesh.ParticleInstanced()) {
        auto& instance_sv = mesh.GetVertexArray(1);
        WPGOption opt;
        opt.thick_format = instance_sv.GetOption(WE_CB_THICK_FORMAT);
        const usize particle_num = GenParticleInstanceData(instances, specOp, opt, instance_sv);
        mesh.SetParticleInstanceCount(static_cast<u32>(particle_num));
        auto& indices = mesh.GetIndexArray(0);
        indices.SetRenderDataCount(particle_num == 0 ? 0 : 6);
        return;
    }

    auto& sv = mesh.GetVertexArray(0);

    WPGOption opt;
    opt.thick_format = sv.GetOption(WE_CB_THICK_FORMAT);

    if (sv.GetOption(WE_PRENDER_ROPE)) {
        // Rope/spline. With a geometry shader (POINT primitive) it's one vertex
        // per segment expanded on the GPU. On macOS (no GS) the mesh is a
        // TRIANGLE list with an index buffer and the segments are expanded to
        // quads on the CPU. Reset the active size before regen so the segment
        // count for this frame isn't masked by a previous frame's high-water
        // mark.
        usize segment_num = 0;
        if (mesh.Primitive() == MeshPrimitive::POINT) {
            segment_num = GenRopeParticleData(instances, specOp, opt, sv);
        } else {
            segment_num     = GenRopeParticleQuadData(instances, specOp, opt, sv);
            auto& si        = mesh.GetIndexArray(0);
            u32   index_num = (u32)(si.DataCount() / 6);
            if (segment_num > index_num) {
                updateIndexArray(index_num, segment_num, si);
            }
            si.SetRenderDataCount(segment_num * 6);
        }
        return;
    }

    if (mesh.Primitive() == MeshPrimitive::POINT) {
        GenParticlePointData(instances, specOp, opt, sv);
        return;
    }

    // Unlike the original path, never retain a previous frame's high-water
    // vertex count. The indexed draw already tracks the exact active quad
    // count, and the dynamic uploader must only copy vertices needed now.
    usize particle_num = GenParticleData(instances, specOp, opt, sv);

    auto& si       = mesh.GetIndexArray(0);
    u32   indexNum = (u32)(si.DataCount() / 6);
    // Old/third-party particle meshes may not have had their fixed topology
    // populated at compile time. Keep that compatibility path, but do not
    // mutate topology for the normal compiler-produced meshes.
    if (! si.StaticTopology() && particle_num > indexNum)
        updateIndexArray(indexNum, particle_num, si);
    si.SetRenderDataCount(particle_num * 6);
}
