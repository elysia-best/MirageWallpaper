module;

#include <rstd/macro.hpp>

export module sr.vulkan_render:resource;
import sr.core;
import rstd.log;
import rstd.cppstd;
import sr.types;
import sr.vulkan;
import sr.scene;

export namespace sr::vulkan
{

class ShaderReflectionCache;

// ---------------------------------------------------------------------------
// Placeholder types for the upstream RenderSceneSnapshot model.
//
// The divergent port does not yet carry the full RenderSceneSnapshot /
// SceneResourceIndex infrastructure (added across 4b41483 + b99a6d6). These
// stubs exist so the 4b41483 render-resource request types compile; they are
// not expected to carry live data yet. Calls that actually need snapshot
// resolution are stubbed with TODO(4b41483) comments.
// ---------------------------------------------------------------------------
// TODO(4b41483): replace these placeholders with the real RenderSceneSnapshot
// types once the snapshot/resource-index infrastructure is ported.
struct RenderItemId {
    uint64_t value { 0 };
    bool     operator==(const RenderItemId&) const = default;
};

struct RenderTextureDescId {
    uint32_t index { 0 };
    uint64_t generation { 0 };
    bool     operator==(const RenderTextureDescId&) const = default;
};

struct RenderTextureDescRecord {
    struct {
        std::string url;
    } desc;
    std::string key;
};

struct RenderSceneSnapshot {
    const RenderTextureDescRecord* textureDesc(const RenderTextureDescId&) const { return nullptr; }
    std::optional<RenderTextureDescId> textureDescId(std::string_view) const { return std::nullopt; }
};

enum class TextureRequestKind
{
    Imported,
    RenderTarget,
    RenderTargetMsaa,
    DepthAttachment,
};

struct TextureRequest {
    TextureRequestKind                 kind { TextureRequestKind::Imported };
    std::string                        name;
    std::optional<RenderTextureDescId> imported_texture;
    std::optional<TextureKey>          cache_key;
    bool                               persist { false };
};

struct TextureBindingRequest {
    std::string                   name;
    std::optional<TextureRequest> request;

    bool empty() const { return name.empty(); }
};

inline bool SameTextureSample(const TextureSample& lhs, const TextureSample& rhs) {
    return lhs.wrapS == rhs.wrapS && lhs.wrapT == rhs.wrapT && lhs.magFilter == rhs.magFilter &&
           lhs.minFilter == rhs.minFilter;
}

inline bool SameTextureKey(const TextureKey& lhs, const TextureKey& rhs) {
    return lhs.width == rhs.width && lhs.height == rhs.height && lhs.usage == rhs.usage &&
           lhs.format == rhs.format && SameTextureSample(lhs.sample, rhs.sample) &&
           lhs.mipmap_level == rhs.mipmap_level && lhs.samples == rhs.samples;
}

inline bool SameRenderTextureDescId(const RenderTextureDescId& lhs,
                                    const RenderTextureDescId& rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline bool SameTextureRequest(const TextureRequest& lhs, const TextureRequest& rhs) {
    if (lhs.kind != rhs.kind || lhs.name != rhs.name || lhs.persist != rhs.persist) return false;
    if (lhs.imported_texture.has_value() != rhs.imported_texture.has_value()) return false;
    if (lhs.imported_texture.has_value() &&
        ! SameRenderTextureDescId(*lhs.imported_texture, *rhs.imported_texture))
        return false;
    if (lhs.cache_key.has_value() != rhs.cache_key.has_value()) return false;
    if (lhs.cache_key.has_value() && ! SameTextureKey(*lhs.cache_key, *rhs.cache_key)) return false;
    return true;
}

inline bool SameTextureRequest(const std::optional<TextureRequest>& lhs,
                               const std::optional<TextureRequest>& rhs) {
    if (lhs.has_value() != rhs.has_value()) return false;
    return ! lhs.has_value() || SameTextureRequest(*lhs, *rhs);
}

inline bool SameTextureBindingRequest(const TextureBindingRequest& lhs,
                                      const TextureBindingRequest& rhs) {
    return lhs.name == rhs.name && SameTextureRequest(lhs.request, rhs.request);
}

inline bool SetTextureRequestIfChanged(std::optional<TextureRequest>& target,
                                       std::optional<TextureRequest>  request) {
    if (SameTextureRequest(target, request)) return false;
    target = std::move(request);
    return true;
}

inline VkSampleCountFlagBits TextureSampleCount(unsigned sample_count) {
    switch (sample_count) {
    case 2: return VK_SAMPLE_COUNT_2_BIT;
    case 4: return VK_SAMPLE_COUNT_4_BIT;
    case 8: return VK_SAMPLE_COUNT_8_BIT;
    case 16: return VK_SAMPLE_COUNT_16_BIT;
    case 32: return VK_SAMPLE_COUNT_32_BIT;
    case 64: return VK_SAMPLE_COUNT_64_BIT;
    default: return VK_SAMPLE_COUNT_1_BIT;
    }
}

inline TextureKey RenderTargetTextureKey(sr::SceneRenderTarget rt) {
    return TextureKey {
        .width        = rt.width,
        .height       = rt.height,
        .usage        = {},
        .format       = sr::TextureFormat::RGBA8,
        .sample       = rt.sample,
        .mipmap_level = rt.mipmap_level,
    };
}

inline TextureKey RenderTargetTextureKeyNoMip(sr::SceneRenderTarget rt) {
    return TextureKey {
        .width  = rt.width,
        .height = rt.height,
        .usage  = {},
        .format = sr::TextureFormat::RGBA8,
        .sample = rt.sample,
    };
}

inline TextureKey MsaaTextureKey(sr::SceneRenderTarget rt, VkSampleCountFlagBits samples) {
    auto key    = RenderTargetTextureKey(rt);
    key.samples = samples;
    return key;
}

inline TextureKey DepthTextureKey(sr::SceneRenderTarget rt) {
    return TextureKey {
        .width        = rt.width,
        .height       = rt.height,
        .usage        = TexUsage::DEPTH,
        .format       = sr::TextureFormat::D32F,
        .sample       = rt.sample,
        .mipmap_level = 1,
        .samples      = TextureSampleCount(rt.sample_count),
    };
}

inline TextureRequest
MakeImportedTextureRequest(std::string_view                   name,
                           std::optional<RenderTextureDescId> texture = std::nullopt) {
    return TextureRequest { .kind             = TextureRequestKind::Imported,
                            .name             = std::string(name),
                            .imported_texture = texture };
}

inline TextureRequest MakeRenderTargetTextureRequest(std::string_view         name,
                                                     const SceneRenderTarget& rt) {
    return TextureRequest { .kind      = TextureRequestKind::RenderTarget,
                            .name      = std::string(name),
                            .cache_key = RenderTargetTextureKey(rt),
                            .persist   = ! rt.allowReuse };
}

inline TextureRequest MakeRenderTargetNoMipTextureRequest(std::string_view         name,
                                                          const SceneRenderTarget& rt) {
    return TextureRequest { .kind      = TextureRequestKind::RenderTarget,
                            .name      = std::string(name),
                            .cache_key = RenderTargetTextureKeyNoMip(rt),
                            .persist   = ! rt.allowReuse };
}

inline TextureRequest MakeMsaaTextureRequest(std::string_view name, const SceneRenderTarget& rt,
                                             VkSampleCountFlagBits samples) {
    return TextureRequest { .kind      = TextureRequestKind::RenderTargetMsaa,
                            .name      = std::string(name),
                            .cache_key = MsaaTextureKey(rt, samples),
                            .persist   = true };
}

inline TextureRequest MakeDepthTextureRequest(std::string_view name, const SceneRenderTarget& rt) {
    return TextureRequest { .kind      = TextureRequestKind::DepthAttachment,
                            .name      = std::string(name),
                            .cache_key = DepthTextureKey(rt),
                            .persist   = ! rt.allowReuse };
}

inline std::optional<ImageParameters> QueryTextureRequest(const Device&         device,
                                                          const TextureRequest& request) {
    if (! request.cache_key.has_value()) return std::nullopt;
    return device.tex_cache().Query(request.name, *request.cache_key, request.persist);
}

inline std::optional<std::string>
ResolveImportedTextureName(const RenderSceneSnapshot& render_scene, const TextureRequest& request) {
    if (request.kind != TextureRequestKind::Imported) return std::nullopt;

    // TODO(4b41483): wire real RenderSceneSnapshot texture-desc resolution.
    // The placeholder snapshot always returns nullptr for textureDesc, so the
    // imported-texture path falls back to the request name until the snapshot
    // infrastructure is ported.
    const RenderTextureDescRecord* record { nullptr };
    if (request.imported_texture.has_value()) {
        record = render_scene.textureDesc(*request.imported_texture);
    }
    if (record == nullptr) {
        if (auto id = render_scene.textureDescId(request.name)) {
            record = render_scene.textureDesc(*id);
        }
    }
    if (record == nullptr) return std::nullopt;
    if (! record->desc.url.empty()) return record->desc.url;
    return record->key;
}

class ImportedTextureProvider {
public:
    ImportedTextureProvider()          = default;
    virtual ~ImportedTextureProvider() = default;

    virtual std::shared_ptr<Image> ParseImportedTexture(const TextureRequest&) const = 0;
};

class SnapshotImportedTextureProvider : public ImportedTextureProvider {
public:
    SnapshotImportedTextureProvider(const RenderSceneSnapshot& render_scene,
                                    IImageParser*              image_parser)
        : m_render_scene(&render_scene), m_image_parser(image_parser) {}

    std::shared_ptr<Image> ParseImportedTexture(const TextureRequest& request) const override {
        if (m_image_parser == nullptr) return nullptr;
        auto name = ResolveImportedTextureName(*m_render_scene, request).value_or(request.name);
        return m_image_parser->Parse(name);
    }

private:
    const RenderSceneSnapshot* m_render_scene { nullptr };
    IImageParser*              m_image_parser { nullptr };
};

class RenderResourceSystem {
public:
    explicit RenderResourceSystem(const Device& device): m_device(&device) {}
    RenderResourceSystem(ImportedTextureProvider* imported_textures, const Device& device)
        : m_imported_textures(imported_textures), m_device(&device) {}

    std::optional<ImageParameters> EnsureTexture(const TextureRequest& request) const {
        if (request.kind == TextureRequestKind::Imported) return std::nullopt;
        return QueryTextureRequest(*m_device, request);
    }

    std::optional<ImageSlotsRef> EnsureSampledTexture(const TextureRequest& request) const {
        if (request.kind != TextureRequestKind::Imported) {
            auto image = EnsureTexture(request);
            if (! image.has_value()) return std::nullopt;
            ImageSlotsRef slots;
            slots.slots = { *image };
            return slots;
        }

        if (m_imported_textures == nullptr) return std::nullopt;
        auto image = m_imported_textures->ParseImportedTexture(request);
        if (! image) {
            rstd_error("parse tex \"{}\" failed", request.name);
            return std::nullopt;
        }
        return m_device->tex_cache().CreateTex(*image);
    }

    void MarkShareReady(std::string_view key) const { m_device->tex_cache().MarkShareReady(key); }

private:
    ImportedTextureProvider* m_imported_textures { nullptr };
    const Device*            m_device { nullptr };
};

struct PipelineResourceRequest {
    std::vector<DescriptorSetInfo>                 descriptor_sets;
    std::vector<VkVertexInputBindingDescription>   vertex_bindings;
    std::vector<VkVertexInputAttributeDescription> vertex_attrs;
    std::vector<Uni_ShaderSpv>                     shader_stages;
    VkPipelineColorBlendAttachmentState            color_blend {};
    VkPipelineDepthStencilStateCreateInfo          depth {};
    VkPipelineRasterizationStateCreateInfo         raster {};
    VkPipelineMultisampleStateCreateInfo           multisample {};
    VkPrimitiveTopology topology { VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP };
    VkFormat            color_format { VK_FORMAT_R8G8B8A8_UNORM };
    VkImageLayout       color_final_layout { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkAttachmentLoadOp  color_load_op { VK_ATTACHMENT_LOAD_OP_DONT_CARE };
    VkAttachmentLoadOp  depth_load_op { VK_ATTACHMENT_LOAD_OP_DONT_CARE };
    bool                has_depth_attachment { false };
};

struct PipelineCacheKey {
    std::size_t value { 0 };
};

struct RenderPassCacheKey {
    std::size_t value { 0 };
};

struct FramebufferCacheKey {
    std::size_t value { 0 };
};

struct FramebufferResourceRequest {
    VkRenderPass             render_pass { VK_NULL_HANDLE };
    RenderPassCacheKey       render_pass_key;
    std::vector<VkImageView> attachments;
    VkExtent2D               extent { 0, 0 };
};

struct PipelineCacheProbe {
    PipelineCacheKey key;
    bool             hit { false };
    uint64_t         observed_count { 0 };
};

struct PipelineResourceEntry {
    PipelineParameters pipeline;
};

struct PipelineResourceResult {
    std::shared_ptr<PipelineResourceEntry> pipeline;
    PipelineCacheKey                       cache_key;
    RenderPassCacheKey                     render_pass_key;
    bool                                   cache_hit { false };
    uint64_t                               cache_observed_count { 0 };
    bool                                   render_pass_cache_hit { false };
    uint64_t                               render_pass_cache_observed_count { 0 };
};

struct FramebufferResourceResult {
    std::shared_ptr<vvk::Framebuffer> framebuffer;
    FramebufferCacheKey               cache_key;
    bool                              cache_hit { false };
    uint64_t                          cache_observed_count { 0 };
};

inline bool SamePipelineCacheKey(PipelineCacheKey lhs, PipelineCacheKey rhs) {
    return lhs.value == rhs.value;
}

inline bool SameRenderPassCacheKey(RenderPassCacheKey lhs, RenderPassCacheKey rhs) {
    return lhs.value == rhs.value;
}

inline bool SameFramebufferCacheKey(FramebufferCacheKey lhs, FramebufferCacheKey rhs) {
    return lhs.value == rhs.value;
}

template<typename T>
inline void HashPipelineScalar(std::size_t& seed, T value) {
    utils::hash_combine(seed, static_cast<uint64_t>(value));
}

// TODO(4b41483): upstream factors SceneShaderStageCodeHash / SceneShaderCodeHash
// out of the scene module. The port lacks those helpers, so a local SPIR-V
// word hash is used here for pipeline-cache keying. Behaviour is equivalent
// for cache-hit purposes (stable hash of the same code vector).
inline std::size_t SceneShaderStageCodeHash(std::span<const unsigned int> spirv) {
    std::size_t seed { 0 };
    utils::hash_combine(seed, spirv.size());
    for (auto word : spirv) utils::hash_combine(seed, static_cast<uint64_t>(word));
    return seed;
}

inline std::size_t HashPipelineShaderStages(std::span<const Uni_ShaderSpv> stages) {
    struct StageRecord {
        sr::ShaderType stage;
        std::string    entry_point;
        std::size_t    code_hash { 0 };
    };

    std::vector<StageRecord> records;
    records.reserve(stages.size());
    for (const auto& stage : stages) {
        if (! stage) continue;
        records.push_back(StageRecord {
            .stage       = stage->stage,
            .entry_point = stage->entry_point,
            .code_hash   = SceneShaderStageCodeHash(stage->spirv),
        });
    }
    std::sort(records.begin(), records.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.stage != rhs.stage) return lhs.stage < rhs.stage;
        return lhs.entry_point < rhs.entry_point;
    });

    std::size_t seed { 0 };
    utils::hash_combine(seed, records.size());
    for (const auto& record : records) {
        HashPipelineScalar(seed, record.stage);
        utils::hash_combine(seed, record.entry_point);
        utils::hash_combine(seed, record.code_hash);
    }
    return seed;
}

inline std::size_t HashPipelineDescriptorSets(std::span<const DescriptorSetInfo> sets) {
    std::size_t seed { 0 };
    utils::hash_combine(seed, sets.size());
    for (const auto& set : sets) {
        HashPipelineScalar(seed, set.push_descriptor);
        auto bindings = set.bindings;
        std::sort(bindings.begin(), bindings.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.binding < rhs.binding;
        });
        utils::hash_combine(seed, bindings.size());
        for (const auto& binding : bindings) {
            HashPipelineScalar(seed, binding.binding);
            HashPipelineScalar(seed, binding.descriptorType);
            HashPipelineScalar(seed, binding.descriptorCount);
            HashPipelineScalar(seed, binding.stageFlags);
        }
    }
    return seed;
}

inline std::size_t
HashPipelineVertexInput(std::span<const VkVertexInputBindingDescription>   bindings,
                        std::span<const VkVertexInputAttributeDescription> attrs) {
    auto sorted_bindings =
        std::vector<VkVertexInputBindingDescription>(bindings.begin(), bindings.end());
    std::sort(sorted_bindings.begin(), sorted_bindings.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.binding < rhs.binding;
    });

    auto sorted_attrs = std::vector<VkVertexInputAttributeDescription>(attrs.begin(), attrs.end());
    std::sort(sorted_attrs.begin(), sorted_attrs.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.location != rhs.location) return lhs.location < rhs.location;
        return lhs.binding < rhs.binding;
    });

    std::size_t seed { 0 };
    utils::hash_combine(seed, sorted_bindings.size());
    for (const auto& binding : sorted_bindings) {
        HashPipelineScalar(seed, binding.binding);
        HashPipelineScalar(seed, binding.stride);
        HashPipelineScalar(seed, binding.inputRate);
    }
    utils::hash_combine(seed, sorted_attrs.size());
    for (const auto& attr : sorted_attrs) {
        HashPipelineScalar(seed, attr.location);
        HashPipelineScalar(seed, attr.binding);
        HashPipelineScalar(seed, attr.format);
        HashPipelineScalar(seed, attr.offset);
    }
    return seed;
}

inline std::size_t HashPipelineColorBlend(const VkPipelineColorBlendAttachmentState& state) {
    std::size_t seed { 0 };
    HashPipelineScalar(seed, state.blendEnable);
    HashPipelineScalar(seed, state.srcColorBlendFactor);
    HashPipelineScalar(seed, state.dstColorBlendFactor);
    HashPipelineScalar(seed, state.colorBlendOp);
    HashPipelineScalar(seed, state.srcAlphaBlendFactor);
    HashPipelineScalar(seed, state.dstAlphaBlendFactor);
    HashPipelineScalar(seed, state.alphaBlendOp);
    HashPipelineScalar(seed, state.colorWriteMask);
    return seed;
}

template<typename T>
inline std::size_t HashPipelineStencil(const T& state) {
    std::size_t seed { 0 };
    HashPipelineScalar(seed, state.failOp);
    HashPipelineScalar(seed, state.passOp);
    HashPipelineScalar(seed, state.depthFailOp);
    HashPipelineScalar(seed, state.compareOp);
    HashPipelineScalar(seed, state.compareMask);
    HashPipelineScalar(seed, state.writeMask);
    HashPipelineScalar(seed, state.reference);
    return seed;
}

inline std::size_t HashPipelineDepthStencil(const VkPipelineDepthStencilStateCreateInfo& state) {
    std::size_t seed { 0 };
    HashPipelineScalar(seed, state.depthTestEnable);
    HashPipelineScalar(seed, state.depthWriteEnable);
    HashPipelineScalar(seed, state.depthCompareOp);
    HashPipelineScalar(seed, state.depthBoundsTestEnable);
    HashPipelineScalar(seed, state.stencilTestEnable);
    utils::hash_combine(seed, HashPipelineStencil(state.front));
    utils::hash_combine(seed, HashPipelineStencil(state.back));
    utils::hash_combine(seed, state.minDepthBounds);
    utils::hash_combine(seed, state.maxDepthBounds);
    return seed;
}

inline std::size_t HashPipelineRaster(const VkPipelineRasterizationStateCreateInfo& state) {
    std::size_t seed { 0 };
    HashPipelineScalar(seed, state.depthClampEnable);
    HashPipelineScalar(seed, state.rasterizerDiscardEnable);
    HashPipelineScalar(seed, state.polygonMode);
    HashPipelineScalar(seed, state.cullMode);
    HashPipelineScalar(seed, state.frontFace);
    HashPipelineScalar(seed, state.depthBiasEnable);
    utils::hash_combine(seed, state.depthBiasConstantFactor);
    utils::hash_combine(seed, state.depthBiasClamp);
    utils::hash_combine(seed, state.depthBiasSlopeFactor);
    utils::hash_combine(seed, state.lineWidth);
    return seed;
}

inline std::size_t HashPipelineMultisample(const VkPipelineMultisampleStateCreateInfo& state) {
    std::size_t seed { 0 };
    HashPipelineScalar(seed, state.rasterizationSamples);
    HashPipelineScalar(seed, state.sampleShadingEnable);
    utils::hash_combine(seed, state.minSampleShading);
    HashPipelineScalar(seed, state.alphaToCoverageEnable);
    HashPipelineScalar(seed, state.alphaToOneEnable);
    return seed;
}

inline PipelineCacheKey MakePipelineCacheKey(const PipelineResourceRequest& request) {
    std::size_t seed { 0 };
    utils::hash_combine(seed, HashPipelineShaderStages(request.shader_stages));
    utils::hash_combine(seed, HashPipelineDescriptorSets(request.descriptor_sets));
    utils::hash_combine(seed,
                        HashPipelineVertexInput(request.vertex_bindings, request.vertex_attrs));
    utils::hash_combine(seed, HashPipelineColorBlend(request.color_blend));
    utils::hash_combine(seed, HashPipelineDepthStencil(request.depth));
    utils::hash_combine(seed, HashPipelineRaster(request.raster));
    utils::hash_combine(seed, HashPipelineMultisample(request.multisample));
    HashPipelineScalar(seed, request.topology);
    HashPipelineScalar(seed, request.color_format);
    HashPipelineScalar(seed, request.color_final_layout);
    HashPipelineScalar(seed, request.color_load_op);
    HashPipelineScalar(seed, request.depth_load_op);
    HashPipelineScalar(seed, request.has_depth_attachment);
    HashPipelineScalar(seed, VK_FORMAT_D32_SFLOAT);
    HashPipelineScalar(seed, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    HashPipelineScalar(seed, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    return PipelineCacheKey { .value = seed };
}

inline RenderPassCacheKey MakeRenderPassCacheKey(const PipelineResourceRequest& request) {
    std::size_t seed { 0 };
    HashPipelineScalar(seed, request.color_format);
    HashPipelineScalar(seed, VK_FORMAT_D32_SFLOAT);
    HashPipelineScalar(seed, request.multisample.rasterizationSamples);
    HashPipelineScalar(seed, request.color_final_layout);
    HashPipelineScalar(seed, request.color_load_op);
    HashPipelineScalar(seed, request.depth_load_op);
    HashPipelineScalar(seed, request.has_depth_attachment);
    HashPipelineScalar(seed, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    HashPipelineScalar(seed, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    return RenderPassCacheKey { .value = seed };
}

inline FramebufferCacheKey MakeFramebufferCacheKey(const FramebufferResourceRequest& request) {
    std::size_t seed { 0 };
    utils::hash_combine(seed, request.render_pass_key.value);
    utils::hash_combine(seed, request.attachments.size());
    for (auto view : request.attachments) utils::hash_combine(seed, view);
    HashPipelineScalar(seed, request.extent.width);
    HashPipelineScalar(seed, request.extent.height);
    return FramebufferCacheKey { .value = seed };
}

class PipelineCacheDiagnostics {
public:
    PipelineCacheProbe Record(PipelineCacheKey key) {
        auto& count = m_seen[key.value];
        bool  hit   = count > 0;
        ++count;
        return PipelineCacheProbe {
            .key            = key,
            .hit            = hit,
            .observed_count = count,
        };
    }

private:
    std::unordered_map<std::size_t, uint64_t> m_seen;
};

class FramebufferCacheDiagnostics {
public:
    struct Probe {
        FramebufferCacheKey key;
        bool                hit { false };
        uint64_t            observed_count { 0 };
    };

    Probe Record(FramebufferCacheKey key) {
        auto& count = m_seen[key.value];
        bool  hit   = count > 0;
        ++count;
        return Probe {
            .key            = key,
            .hit            = hit,
            .observed_count = count,
        };
    }

private:
    std::unordered_map<std::size_t, uint64_t> m_seen;
};

class FramebufferResourceCache {
public:
    std::optional<FramebufferResourceResult> Ensure(const Device&                     device,
                                                    const FramebufferResourceRequest& request) {
        if (request.render_pass == VK_NULL_HANDLE || request.attachments.empty()) {
            return std::nullopt;
        }

        auto  key  = MakeFramebufferCacheKey(request);
        auto& slot = m_entries[key.value];
        if (auto existing = slot.framebuffer.lock()) {
            ++slot.observed_count;
            return FramebufferResourceResult {
                .framebuffer          = std::move(existing),
                .cache_key            = key,
                .cache_hit            = true,
                .cache_observed_count = slot.observed_count,
            };
        }

        VkFramebufferCreateInfo info {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext           = nullptr,
            .renderPass      = request.render_pass,
            .attachmentCount = static_cast<uint32_t>(request.attachments.size()),
            .pAttachments    = request.attachments.data(),
            .width           = request.extent.width,
            .height          = request.extent.height,
            .layers          = 1,
        };
        vvk::Framebuffer framebuffer;
        if (device.handle().CreateFramebuffer(info, framebuffer) != VK_SUCCESS) {
            return std::nullopt;
        }
        auto shared      = std::make_shared<vvk::Framebuffer>(std::move(framebuffer));
        slot.framebuffer = shared;
        ++slot.observed_count;
        return FramebufferResourceResult {
            .framebuffer          = std::move(shared),
            .cache_key            = key,
            .cache_hit            = false,
            .cache_observed_count = slot.observed_count,
        };
    }

    void PruneExpired() {
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            if (it->second.framebuffer.expired()) {
                it = m_entries.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::size_t entryCount() const { return m_entries.size(); }

private:
    struct Entry {
        std::weak_ptr<vvk::Framebuffer> framebuffer;
        uint64_t                        observed_count { 0 };
    };

    std::unordered_map<std::size_t, Entry> m_entries;
};

inline bool HasPipelineResources(const PipelineParameters& pipeline) {
    return static_cast<bool>(pipeline.handle) || static_cast<bool>(pipeline.layout) ||
           static_cast<bool>(pipeline.pass) || ! pipeline.descriptor_layouts.empty();
}

inline bool HasPipelineResources(const PipelineResourceEntry& entry) {
    return HasPipelineResources(entry.pipeline);
}

struct RenderPassResourceResult {
    std::shared_ptr<vvk::RenderPass> render_pass;
    RenderPassCacheKey               cache_key;
    bool                             cache_hit { false };
    uint64_t                         cache_observed_count { 0 };
};

class RenderPassResourceCache {
public:
    std::optional<RenderPassResourceResult> Ensure(const Device&                  device,
                                                   const PipelineResourceRequest& request) {
        auto  key  = MakeRenderPassCacheKey(request);
        auto& slot = m_entries[key.value];
        if (auto existing = slot.render_pass.lock()) {
            ++slot.observed_count;
            return RenderPassResourceResult {
                .render_pass          = std::move(existing),
                .cache_key            = key,
                .cache_hit            = true,
                .cache_observed_count = slot.observed_count,
            };
        }

        auto created = CreateRenderPass(device, request);
        if (! created.has_value()) return std::nullopt;
        auto shared      = std::make_shared<vvk::RenderPass>(std::move(*created));
        slot.render_pass = shared;
        ++slot.observed_count;
        return RenderPassResourceResult {
            .render_pass          = std::move(shared),
            .cache_key            = key,
            .cache_hit            = false,
            .cache_observed_count = slot.observed_count,
        };
    }

    void PruneExpired() {
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            if (it->second.render_pass.expired()) {
                it = m_entries.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::size_t entryCount() const { return m_entries.size(); }

private:
    struct Entry {
        std::weak_ptr<vvk::RenderPass> render_pass;
        uint64_t                       observed_count { 0 };
    };

    static std::optional<vvk::RenderPass> CreateRenderPass(const Device&                  device,
                                                           const PipelineResourceRequest& request) {
        const bool has_resolve = request.multisample.rasterizationSamples != VK_SAMPLE_COUNT_1_BIT;
        VkAttachmentDescription color {
            .format         = request.color_format,
            .samples        = request.multisample.rasterizationSamples,
            .loadOp         = request.color_load_op,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout =
                has_resolve ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : request.color_final_layout,
        };
        if (request.color_load_op == VK_ATTACHMENT_LOAD_OP_LOAD) {
            color.initialLayout = has_resolve ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                              : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        VkAttachmentDescription resolve {
            .format         = request.color_format,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout    = request.color_final_layout,
        };

        VkAttachmentDescription depth {
            .format         = VK_FORMAT_D32_SFLOAT,
            .samples        = request.multisample.rasterizationSamples,
            .loadOp         = request.depth_load_op,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = request.depth_load_op == VK_ATTACHMENT_LOAD_OP_LOAD
                                  ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                  : VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };

        VkAttachmentReference color_ref {
            .attachment = 0,
            .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        VkAttachmentReference resolve_ref {
            .attachment = 1,
            .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        VkAttachmentReference depth_ref {
            .attachment = has_resolve ? 2u : 1u,
            .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };

        std::vector<VkAttachmentDescription> attachments;
        attachments.reserve(3);
        attachments.push_back(color);
        if (has_resolve) attachments.push_back(resolve);
        if (request.has_depth_attachment) attachments.push_back(depth);

        VkSubpassDescription subpass {
            .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount    = 1,
            .pColorAttachments       = &color_ref,
            .pResolveAttachments     = has_resolve ? &resolve_ref : nullptr,
            .pDepthStencilAttachment = request.has_depth_attachment ? &depth_ref : nullptr,
        };

        VkSubpassDependency dependency {
            .srcSubpass   = VK_SUBPASS_EXTERNAL,
            .dstSubpass   = 0,
            .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .srcAccessMask =
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        };

        VkRenderPassCreateInfo create {
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = static_cast<uint32_t>(attachments.size()),
            .pAttachments    = attachments.data(),
            .subpassCount    = 1,
            .pSubpasses      = &subpass,
            .dependencyCount = 1,
            .pDependencies   = &dependency,
        };
        vvk::RenderPass pass;
        if (device.handle().CreateRenderPass(create, pass) != VK_SUCCESS) return std::nullopt;
        return pass;
    }

    std::unordered_map<std::size_t, Entry> m_entries;
};

class PipelineResourceCache {
public:
    std::optional<PipelineResourceResult> Ensure(const Device&            device,
                                                 PipelineResourceRequest  request,
                                                 RenderPassResourceCache& render_pass_cache) {
        auto  key  = MakePipelineCacheKey(request);
        auto& slot = m_entries[key.value];
        if (auto existing = slot.pipeline.lock()) {
            ++slot.observed_count;
            return PipelineResourceResult {
                .pipeline                         = std::move(existing),
                .cache_key                        = key,
                .render_pass_key                  = slot.render_pass_key,
                .cache_hit                        = true,
                .cache_observed_count             = slot.observed_count,
                .render_pass_cache_hit            = true,
                .render_pass_cache_observed_count = slot.observed_count,
            };
        }

        // TODO(4b41483): wire graphics-pipeline creation through the cache.
        // Upstream hoists GraphicsPipeline::create() to take a non-owning
        // VkRenderPass and stores the render pass back into PipelineParameters
        // via std::shared_ptr<vvk::RenderPass>. The divergent port still uses
        // the owning create(Device&, vvk::RenderPass&, PipelineParameters&)
        // overload, which is incompatible with sharing a cached render pass
        // across pipeline entries. Until the create() signature is ported
        // (tracked separately), this cache returns a miss for new keys so the
        // existing per-pass pipeline construction path remains authoritative.
        (void)device;
        (void)render_pass_cache;
        return std::nullopt;
    }

    void PruneExpired() {
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            if (it->second.pipeline.expired()) {
                it = m_entries.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::size_t entryCount() const { return m_entries.size(); }

private:
    struct Entry {
        std::weak_ptr<PipelineResourceEntry> pipeline;
        RenderPassCacheKey                   render_pass_key;
        uint64_t                             observed_count { 0 };
    };

    std::unordered_map<std::size_t, Entry> m_entries;
};

class PipelineRetireQueue {
public:
    void Retire(PipelineParameters&& pipeline) {
        if (! HasPipelineResources(pipeline)) return;
        m_retired.push_back(std::move(pipeline));
    }

    void Retire(std::shared_ptr<PipelineResourceEntry> pipeline) {
        if (! pipeline || ! HasPipelineResources(*pipeline)) return;
        m_pipeline_entries.push_back(std::move(pipeline));
    }

    void Retire(vvk::Framebuffer&& framebuffer) {
        if (! framebuffer) return;
        m_framebuffers.push_back(std::make_shared<vvk::Framebuffer>(std::move(framebuffer)));
    }

    void Retire(std::shared_ptr<vvk::Framebuffer> framebuffer) {
        if (! framebuffer || ! *framebuffer) return;
        m_framebuffers.push_back(std::move(framebuffer));
    }

    void ReleaseReady() {
        m_pipeline_entries.clear();
        m_retired.clear();
    }

    void ReleaseFramebuffersReady() { m_framebuffers.clear(); }

    void ReleaseAllReady() {
        ReleaseFramebuffersReady();
        ReleaseReady();
    }

    std::size_t pending() const {
        return m_pipeline_entries.size() + m_retired.size() + m_framebuffers.size();
    }

private:
    std::vector<std::shared_ptr<PipelineResourceEntry>> m_pipeline_entries;
    std::vector<PipelineParameters>                     m_retired;
    std::vector<std::shared_ptr<vvk::Framebuffer>>      m_framebuffers;
};

class FramebufferResourceSystem {
public:
    explicit FramebufferResourceSystem(const Device&                device,
                                       FramebufferResourceCache*    cache       = nullptr,
                                       FramebufferCacheDiagnostics* diagnostics = nullptr)
        : m_device(&device), m_cache(cache), m_diagnostics(diagnostics) {}

    std::optional<FramebufferResourceResult>
    CreateFramebuffer(const FramebufferResourceRequest& request) const {
        if (request.render_pass == VK_NULL_HANDLE || request.attachments.empty()) {
            return std::nullopt;
        }
        if (m_diagnostics != nullptr) m_diagnostics->Record(MakeFramebufferCacheKey(request));
        FramebufferResourceCache local_cache;
        auto&                    cache = m_cache != nullptr ? *m_cache : local_cache;
        return cache.Ensure(*m_device, request);
    }

private:
    const Device*                m_device { nullptr };
    FramebufferResourceCache*    m_cache { nullptr };
    FramebufferCacheDiagnostics* m_diagnostics { nullptr };
};

class PipelineResourceSystem {
public:
    explicit PipelineResourceSystem(const Device&            device,
                                    PipelineResourceCache*   pipeline_cache    = nullptr,
                                    RenderPassResourceCache* render_pass_cache = nullptr)
        : m_device(&device),
          m_pipeline_cache(pipeline_cache),
          m_render_pass_cache(render_pass_cache) {}

    std::optional<PipelineResourceResult>
    CreateGraphicsPipeline(PipelineResourceRequest request) const {
        PipelineResourceCache   local_pipeline_cache;
        RenderPassResourceCache local_render_pass_cache;
        auto&                   pipeline_cache =
            m_pipeline_cache != nullptr ? *m_pipeline_cache : local_pipeline_cache;
        auto& render_pass_cache =
            m_render_pass_cache != nullptr ? *m_render_pass_cache : local_render_pass_cache;
        return pipeline_cache.Ensure(*m_device, std::move(request), render_pass_cache);
    }

private:
    const Device*            m_device { nullptr };
    PipelineResourceCache*   m_pipeline_cache { nullptr };
    RenderPassResourceCache* m_render_pass_cache { nullptr };
};

struct RenderingResources {
    vvk::CommandBuffer command;

    vvk::Semaphore sem_swap_wait_image;
    vvk::Semaphore sem_export;
    // TODO(4b41483): upstream adds a timeline `sem_upload` semaphore plus
    // upload_timeline_value / pending_upload_value to overlap dyn_buf uploads
    // with rendering. The port's render loop still submits uploads inline
    // before the render cmd; wiring the timeline semaphore requires the full
    // render-loop refactor deferred below.
    vvk::Fence     fence_frame;

    // Static vertex/index buffers are owned by Device::mesh_cache() now;
    // only the per-rebuild dyn_buf lives here.
    StagingBuffer*              dyn_buf { nullptr };
    ShaderReflectionCache*      shader_reflection_cache { nullptr };
    ImportedTextureProvider*    imported_texture_provider { nullptr };
    // TODO(4b41483): these caches are declared so the resource-request types
    // compile and are ready to wire; the port's render loop does not route
    // pipeline/framebuffer creation through them yet.
    PipelineResourceCache       pipeline_cache;
    RenderPassResourceCache     render_pass_cache;
    FramebufferResourceCache    framebuffer_cache;
    FramebufferCacheDiagnostics framebuffer_cache_diagnostics;
    PipelineRetireQueue         pipeline_retire_queue;
};

} // namespace sr::vulkan
