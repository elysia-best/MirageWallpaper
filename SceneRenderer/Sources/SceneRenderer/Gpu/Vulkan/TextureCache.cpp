module;

#define VK_USE_PLATFORM_METAL_EXT
#include <vulkan/vulkan.h>
#include <rstd/macro.hpp>
#include "vk_mem_alloc.h"

#include "Utils/AutoDeletor.hpp"
#include "vvk/macros.hpp"

#include <unistd.h>

module sr.vulkan;
import sr.core;
import rstd.log;
import rstd.cppstd;

import sr.types;
import sr.fs;
import wavsen.video;

using namespace sr;
using namespace sr::vulkan;

namespace sr
{
namespace vulkan
{
VkFormat ToVkType(TextureFormat tf) {
    switch (tf) {
    case TextureFormat::BC1: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case TextureFormat::BC2: return VK_FORMAT_BC2_UNORM_BLOCK;
    case TextureFormat::BC3: return VK_FORMAT_BC3_UNORM_BLOCK;
    case TextureFormat::R8: return VK_FORMAT_R8_UNORM;
    case TextureFormat::RG8: return VK_FORMAT_R8G8_UNORM;
    case TextureFormat::RGB8: return VK_FORMAT_R8G8B8_UNORM;
    case TextureFormat::RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::D32F: return VK_FORMAT_D32_SFLOAT;
    default: rstd_assert(false); return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

VkSamplerAddressMode ToVkType(sr::TextureWrap sam) {
    using namespace sr;
    switch (sam) {
    case TextureWrap::CLAMP_TO_EDGE: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case TextureWrap::REPEAT:
    default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}
VkFilter ToVkType(sr::TextureFilter sam) {
    using namespace sr;
    switch (sam) {
    case TextureFilter::LINEAR: return VK_FILTER_LINEAR;
    case TextureFilter::NEAREST:
    default: return VK_FILTER_NEAREST;
    }
}
} // namespace vulkan
} // namespace sr

namespace
{
VkSamplerCreateInfo GenSamplerInfo(TextureKey key) {
    auto& sam = key.sample;

    VkSamplerCreateInfo sampler_info { .sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                       .pNext            = nullptr,
                                       .magFilter        = ToVkType(sam.magFilter),
                                       .minFilter        = (ToVkType(sam.minFilter)),
                                       .mipmapMode       = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                       .addressModeU     = (ToVkType(sam.wrapS)),
                                       .addressModeV     = (ToVkType(sam.wrapS)),
                                       .addressModeW     = (ToVkType(sam.wrapT)),
                                       .anisotropyEnable = (false),
                                       .maxAnisotropy    = (1.0f),
                                       .compareEnable    = (false),
                                       .compareOp        = VK_COMPARE_OP_NEVER,
                                       .minLod           = (0.0f),
                                       .maxLod           = (1.0f),
                                       .borderColor      = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
                                       .unnormalizedCoordinates = (false) };
    return sampler_info;
}

VkResult TransImgLayout(const vvk::Queue& queue, vvk::CommandBuffer& cmd,
                        const ImageParameters& image, VkImageLayout layout) {
    VkResult result;
    do {
        result = cmd.Begin(VkCommandBufferBeginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        });
        if (result != VK_SUCCESS) break;

        VkImageSubresourceRange subresourceRange {
            .aspectMask     = layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                  ? VK_IMAGE_ASPECT_DEPTH_BIT
                                  : VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = VK_REMAINING_MIP_LEVELS,
            .baseArrayLayer = 0,
            .layerCount     = VK_REMAINING_ARRAY_LAYERS,
        };
        const bool    depth_layout = layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAccessFlags dst_access   = depth_layout ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                                                  : VK_ACCESS_MEMORY_READ_BIT;
        VkPipelineStageFlags dst_stage = depth_layout
                                             ? VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
                                             : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        {
            VkImageMemoryBarrier out_bar {
                .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext            = nullptr,
                .srcAccessMask    = {},
                .dstAccessMask    = dst_access,
                .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout        = layout,
                .image            = image.handle,
                .subresourceRange = subresourceRange,
            };
            cmd.PipelineBarrier(
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, dst_stage, VK_DEPENDENCY_BY_REGION_BIT, out_bar);
        }
        result = cmd.End();
        if (result != VK_SUCCESS) break;

        VkSubmitInfo sub_info {
            .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext              = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers    = cmd.address(),
        };
        result = queue.Submit(sub_info);
    } while (false);
    return result;
}

std::optional<vvk::DeviceMemory> AllocateMemory(const vvk::Device& device, vvk::PhysicalDevice gpu,
                                                VkMemoryRequirements  reqs,
                                                VkMemoryPropertyFlags property,
                                                void*                 pNext = NULL) {
    VkPhysicalDeviceMemoryProperties pros = gpu.GetMemoryProperties().memoryProperties;
    for (uint32_t i = 0; i < pros.memoryTypeCount; ++i) {
        if ((reqs.memoryTypeBits & (1 << i)) && (pros.memoryTypes[i].propertyFlags & property)) {
            VkMemoryAllocateInfo memory_allocate_info { .sType =
                                                            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                                        .pNext           = pNext,
                                                        .allocationSize  = reqs.size,
                                                        .memoryTypeIndex = i };
            vvk::DeviceMemory    mem;
            VkResult             res = device.AllocateMemory(memory_allocate_info, mem);
            if (res == VK_SUCCESS) {
                return mem;
            } else {
                VVK_CHECK(res);
                return std::nullopt;
            }
        }
    }
    rstd_error("vulkan allocate memory failed, no memory match requires");
    return std::nullopt;
}

inline std::optional<VmaImageParameters>
CreateImage(const Device& device, VkExtent3D extent, u32 miplevel, VkFormat format,
            VkSamplerCreateInfo sampler_info, VkImageUsageFlags usage,
            VmaMemoryUsage        mem_usage = VMA_MEMORY_USAGE_GPU_ONLY,
            VkSampleCountFlagBits samples   = VK_SAMPLE_COUNT_1_BIT) {
    VmaImageParameters image;
    do {
        // Multisample images can't have mipmaps; force levelCount=1 and
        // restrict usage to color attachment (no transfer/sampled needed
        // since the resolved sibling carries the readable copy).
        if (samples != VK_SAMPLE_COUNT_1_BIT) {
            miplevel = 1;
            if ((usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0)
                usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }
        const bool metal_export =
            device.supportExt("VK_EXT_metal_objects") &&
            (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0;
        VkExportMetalObjectCreateInfoEXT metal_image_export {
            .sType            = VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT,
            .pNext            = nullptr,
            .exportObjectType = VK_EXPORT_METAL_OBJECT_TYPE_METAL_TEXTURE_BIT_EXT,
        };
        VkImageCreateInfo info {
            .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext                = metal_export ? &metal_image_export : nullptr,
            .imageType             = VK_IMAGE_TYPE_2D,
            .format                = format,
            .extent                = extent,
            .mipLevels             = miplevel,
            .arrayLayers           = 1,
            .samples               = samples,
            .tiling                = VK_IMAGE_TILING_OPTIMAL,
            .usage                 = usage,
            .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        image.extent = info.extent;
        VmaAllocationCreateInfo vma_info {};
        vma_info.usage = mem_usage;
        VVK_CHECK_ACT(break,
                      vvk::CreateImage(device.vma_allocator(), info, vma_info, image.handle));

        image.mipmap_level = miplevel;
        {
            const bool depth_usage = (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
            VkExportMetalObjectCreateInfoEXT metal_view_export {
                .sType            = VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT,
                .pNext            = nullptr,
                .exportObjectType = VK_EXPORT_METAL_OBJECT_TYPE_METAL_TEXTURE_BIT_EXT,
            };
            VkImageViewCreateInfo createinfo {
                .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext    = metal_export ? &metal_view_export : nullptr,
                .image    = *image.handle,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format   = format,
                .subresourceRange =
                    VkImageSubresourceRange {
                        .aspectMask =
                            depth_usage ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel   = 0,
                        .levelCount     = miplevel,
                        .baseArrayLayer = 0,
                        .layerCount     = 1,
                    },
            };
            VVK_CHECK_ACT(break, device.handle().CreateImageView(createinfo, image.view));
        }
        VVK_CHECK_ACT(break, device.handle().CreateSampler(sampler_info, image.sampler));
        return image;
    } while (false);
    /*
    if (result != vk::Result::eSuccess) {
        device.DestroyImageParameters(image);
    }
    */
    return std::nullopt;
}

inline VkResult CopyImageData(std::span<const BufferParameters> in_bufs,
                              std::span<const VkExtent3D> in_exts, const vvk::Queue& queue,
                              vvk::CommandBuffer& cmd, const ImageParameters& image) {
    VkResult result;
    do {
        result = cmd.Begin(VkCommandBufferBeginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        });
        if (result != VK_SUCCESS) break;

        VkImageSubresourceRange subresourceRange {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = (uint32_t)in_bufs.size(),
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };
        {
            VkImageMemoryBarrier in_bar {
                .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext            = nullptr,
                .srcAccessMask    = VK_ACCESS_MEMORY_WRITE_BIT,
                .dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .image            = image.handle,
                .subresourceRange = subresourceRange,
            };
            cmd.PipelineBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_DEPENDENCY_BY_REGION_BIT,
                                in_bar);
        }
        VkBufferImageCopy copy {
            .imageSubresource =
                VkImageSubresourceLayers {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
                },
        };
        for (usize i = 0; i < in_bufs.size(); i++) {
            copy.imageSubresource.mipLevel = (u32)i;
            copy.imageExtent               = in_exts[i];
            cmd.CopyBufferToImage(
                in_bufs[i].handle, image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, copy);
        }
        {
            VkImageMemoryBarrier out_bar {
                .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext            = nullptr,
                .srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask    = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .image            = image.handle,
                .subresourceRange = subresourceRange,
            };
            cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                VK_DEPENDENCY_BY_REGION_BIT,
                                out_bar);
        }
        result = cmd.End();
        if (result != VK_SUCCESS) break;

        VkSubmitInfo sub_info {
            .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext              = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers    = cmd.address(),
        };
        result = queue.Submit(sub_info);
    } while (false);
    return result;
}
} // namespace

std::size_t TextureKey::HashValue(const TextureKey& k) {
    std::size_t seed { 0 };
    utils::hash_combine(seed, k.width);
    utils::hash_combine(seed, k.height);
    utils::hash_combine(seed, (int)k.usage);
    utils::hash_combine(seed, (int)k.format);
    utils::hash_combine(seed, (int)k.mipmap_level);

    utils::hash_combine(seed, (int)k.sample.wrapS);
    utils::hash_combine(seed, (int)k.sample.wrapT);
    utils::hash_combine(seed, (int)k.sample.magFilter);
    utils::hash_combine(seed, (int)k.samples);
    return seed;
}

std::optional<VmaImageParameters>
TextureCache::CreateRenderTargetTex(uint32_t width, uint32_t height, VkFormat format) {
    VkSamplerCreateInfo sampler_info {
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext                   = nullptr,
        .magFilter               = VK_FILTER_NEAREST,
        .minFilter               = VK_FILTER_NEAREST,
        .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .anisotropyEnable        = false,
        .maxAnisotropy           = 1.0f,
        .compareEnable           = false,
        .compareOp               = VK_COMPARE_OP_NEVER,
        .minLod                  = 0.0f,
        .maxLod                  = 1.0f,
        .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = false,
    };
    VkExtent3D ext { width, height, 1 };
    return CreateImage(m_device,
                       ext,
                       /*miplevel=*/1u,
                       format,
                       sampler_info,
                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                           VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
}

ImageSlotsRef TextureCache::CreateTex(Image& image) {
    if (exists(m_tex_map, image.key)) {
        return m_tex_map.at(image.key);
    }

    if (image.header.type == ImageType::VIDEO) {
        return CreateVideoTex(image);
    }

    ImageSlots img_slots;

    if (! m_tex_cmd) allocateCmd();

    img_slots.slots.resize(image.slots.size());

    auto& sam = image.header.sample;

    for (usize i = 0; i < image.slots.size(); i++) {
        auto& image_paras   = img_slots.slots[i];
        auto& image_slot    = image.slots[i];
        auto  mipmap_levels = image_slot.mipmaps.size();

        // check data
        if (! image_slot) return {};
        VkSamplerCreateInfo sampler_info {
            .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .pNext                   = nullptr,
            .magFilter               = ToVkType(sam.magFilter),
            .minFilter               = (ToVkType(sam.minFilter)),
            .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU            = (ToVkType(sam.wrapS)),
            .addressModeV            = (ToVkType(sam.wrapS)),
            .addressModeW            = (ToVkType(sam.wrapT)),
            .anisotropyEnable        = (false),
            .maxAnisotropy           = (1.0f),
            .compareEnable           = (false),
            .compareOp               = VK_COMPARE_OP_NEVER,
            .minLod                  = (0.0f),
            .maxLod                  = (float)mipmap_levels,
            .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = (false),
        };
        VkFormat   format = ToVkType(image.header.format);
        VkExtent3D ext { (u32)image_slot.width, (u32)image_slot.height, 1 };

        if (auto opt = CreateImage(m_device,
                                   ext,
                                   (u32)mipmap_levels,
                                   format,
                                   sampler_info,
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
            opt.has_value()) {
            image_paras = std::move(opt.value());
            AssignImageGeneration(image_paras);
        } else
            break;

        std::vector<VmaBufferParameters> stage_bufs;
        std::vector<VkExtent3D>          extents;

        for (usize j = 0; j < image_slot.mipmaps.size(); j++) {
            auto&               image_data = image_slot.mipmaps[j];
            VmaBufferParameters buf;
            (void)CreateStagingBuffer(m_device.vma_allocator(), (u32)image_data.size, buf);
            {
                void* v_data;
                VVK_CHECK(buf.handle.MapMemory(&v_data));
                std::memcpy(v_data, image_data.data.get(), (u32)image_data.size);
                buf.handle.UnMapMemory();
            }
            stage_bufs.emplace_back(std::move(buf));
            extents.push_back(VkExtent3D { (u32)image_data.width, (u32)image_data.height, 1 });
        }

        CopyImageData(transform<VmaBufferParameters>(stage_bufs,
                                                     [](BufferParameters e) {
                                                         return e;
                                                     }),
                      extents,
                      m_device.graphics_queue().handle,
                      m_tex_cmd,
                      ToImageParameters(image_paras));

        m_device.handle().WaitIdle();
    }
    m_tex_map[image.key] = std::move(img_slots);
    return m_tex_map[image.key];
}

void TextureCache::allocateCmd() {
    const auto& pool = m_device.cmd_pool();
    VVK_CHECK(pool.Allocate(1, VK_COMMAND_BUFFER_LEVEL_PRIMARY, m_tex_cmds));
    m_tex_cmd = vvk::CommandBuffer(m_tex_cmds[0], m_device.handle().Dispatch());
}

std::optional<VmaImageParameters> TextureCache::CreateTex(TextureKey tex_key) {
    VmaImageParameters image_paras;
    do {
        VkSamplerCreateInfo sam_info = GenSamplerInfo(tex_key);
        VkFormat            format   = ToVkType(tex_key.format);
        VkExtent3D          ext { (u32)tex_key.width, (u32)tex_key.height, 1 };
        const bool          depth_usage = tex_key.usage == TexUsage::DEPTH;
        VkImageUsageFlags   usage =
            depth_usage ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                        : VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        if (auto opt = CreateImage(m_device,
                                   ext,
                                   tex_key.mipmap_level,
                                   format,
                                   sam_info,
                                   usage,
                                   VMA_MEMORY_USAGE_GPU_ONLY,
                                   tex_key.samples);
            opt.has_value()) {
            image_paras = std::move(opt.value());
            AssignImageGeneration(image_paras);
        } else
            break;

        // Single-sample images settle in SHADER_READ_ONLY (sampled by other
        // passes). MSAA twin is never sampled — pre-transition to
        // COLOR_ATTACHMENT_OPTIMAL so the first render pass with LoadOp=LOAD
        // doesn't see UNDEFINED on a non-DONT_CARE attachment.
        if (! m_tex_cmd) allocateCmd();
        TransImgLayout(m_device.graphics_queue().handle,
                       m_tex_cmd,
                       ToImageParameters(image_paras),
                       depth_usage ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                       : tex_key.samples == VK_SAMPLE_COUNT_1_BIT
                           ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                           : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        VVK_CHECK_ACT(break, m_device.handle().WaitIdle());
        return image_paras;
    } while (false);
    return std::nullopt;
}

/* ===========================================================================
 * Video-tex pipeline
 *
 * When TextureAssetDecoder detects an MP4 / WebM container inlined in a
 * .tex body (header.type == ImageType::VIDEO), it doesn't decompress
 * pixels — it stashes the pkg's IBinaryStream + byte range in
 * ImageData. CreateTex routes those Images here:
 *
 *   1. We allocate a stable RGBA8 VkImage at (w,h), cleared to black,
 *      and register it in m_tex_map under the Image's key so the rest
 *      of the renderer (descriptor sets, sprite-anim fallback, etc.)
 *      sees an ordinary single-slot texture.
 *   2. We wrap the pkg stream in a wavsen::video::IInputStream that
 *      clamps reads/seeks to [offset, offset+size). Lifetime: the
 *      shared_ptr<sr::fs::IBinaryStream> lands inside the adapter and
 *      stays alive as long as the VideoDecoder owns it.
 *   3. We spin up a wavsen::video::VideoDecoder via open_from_stream.
 *      When hwdec is enabled, a lazily-created wavsen Producer supplies
 *      the hwdevice; otherwise the decoder stays sw-only. The avformat
 *      probe is happy with ftyp/EBML at the head of the stream.
 *   4. Each render tick PumpVideoTextures advances PTS, pulls the
 *      correct frame view for the active decoder kind, and writes the
 *      stable RGBA8 VkImage through wavsen::video::YuvToRgba. Hardware
 *      decode uses SceneRenderer's own VkDevice via Producer::from_external
 *      so conversion and material sampling stay on the same device.
 *
 * ========================================================================= */

namespace
{

// Wraps an sr::fs::IBinaryStream sub-range as a wavsen avio source.
// Holds the underlying stream alive via shared_ptr so the .pkg file
// handle survives until the decoder closes.
class PkgRangedInputStream : public wavsen::video::IInputStream {
public:
    PkgRangedInputStream(std::shared_ptr<sr::fs::IBinaryStream> base, std::int64_t offset,
                         std::int64_t length)
        : m_base(std::move(base)), m_offset(offset), m_length(length) {}

    int read(std::uint8_t* buf, int size) override {
        if (! m_base || size <= 0) return 0;
        if (m_cursor >= m_length) return 0; /* EOF — avio shim maps to AVERROR_EOF */
        std::int64_t remain = m_length - m_cursor;
        int          take   = size < remain ? size : static_cast<int>(remain);
        m_base->SeekSet(static_cast<idx>(m_offset + m_cursor));
        auto got = m_base->Read(buf, static_cast<usize>(take));
        if (got == 0) return 0;
        m_cursor += static_cast<std::int64_t>(got);
        return static_cast<int>(got);
    }

    std::int64_t seek(std::int64_t offset, int whence) override {
        constexpr int AVSEEK_SIZE = 0x10000;
        if (whence == AVSEEK_SIZE) return m_length;
        std::int64_t next;
        switch (whence) {
        case 0: /* SEEK_SET */ next = offset; break;
        case 1: /* SEEK_CUR */ next = m_cursor + offset; break;
        case 2: /* SEEK_END */ next = m_length + offset; break;
        default: return -1;
        }
        if (next < 0 || next > m_length) return -1;
        m_cursor = next;
        return m_cursor;
    }

private:
    std::shared_ptr<sr::fs::IBinaryStream> m_base;
    std::int64_t                            m_offset { 0 };
    std::int64_t                            m_length { 0 };
    std::int64_t                            m_cursor { 0 };
};

wavsen::video::HwAccel ParseHwdec(std::string_view value) {
    if (value == "vulkan") return wavsen::video::HwAccel::Vulkan;
    if (value == "vaapi") return wavsen::video::HwAccel::Vaapi;
    if (value == "none") return wavsen::video::HwAccel::None;
    return wavsen::video::HwAccel::Auto;
}

const char* HwdecLabel(wavsen::video::HwAccel h) {
    switch (h) {
    case wavsen::video::HwAccel::Auto: return "auto";
    case wavsen::video::HwAccel::Vulkan: return "vulkan";
    case wavsen::video::HwAccel::Vaapi: return "vaapi";
    case wavsen::video::HwAccel::None: return "none";
    }
    return "?";
}

const char* FrameKindLabel(wavsen::video::FrameKind k) {
    switch (k) {
    case wavsen::video::FrameKind::Sw: return "sw";
    case wavsen::video::FrameKind::VulkanShared: return "vulkan-shared";
    case wavsen::video::FrameKind::VaapiDrm: return "vaapi-drm";
    }
    return "?";
}

std::vector<const char*> ExtensionPtrs(std::span<const std::string> names) {
    std::vector<const char*> out;
    out.reserve(names.size());
    for (const auto& name : names) out.push_back(name.c_str());
    return out;
}

std::vector<wavsen::video::QueueFamily> QueueFamiliesForFfmpeg(const Device& device) {
    auto                                    props = device.gpu().GetQueueFamilyProperties();
    std::vector<wavsen::video::QueueFamily> out;
    out.reserve(props.size());
    for (std::uint32_t i = 0; i < props.size(); ++i) {
        out.push_back(wavsen::video::QueueFamily {
            .index      = i,
            .flags      = props[i].queueFlags,
            .video_caps = 0,
        });
    }
    return out;
}

wavsen::video::Producer::ExternalDeviceInfo
MakeExternalProducerInfo(const Device& device, std::uint32_t width, std::uint32_t height) {
    return wavsen::video::Producer::ExternalDeviceInfo {
        .instance                    = device.instance_handle(),
        .physical_device             = *device.gpu(),
        .device                      = *device.handle(),
        .queue                       = *device.graphics_queue().handle,
        .queue_family_index          = device.graphics_queue().family_index,
        .queue_families              = QueueFamiliesForFfmpeg(device),
        .enabled_instance_extensions = ExtensionPtrs(device.enabled_instance_extensions()),
        .enabled_device_extensions   = ExtensionPtrs(device.enabled_device_extensions()),
        .api_version                 = device.instance_api_version(),
        .width                       = width,
        .height                      = height,
    };
}

void CloseSyncFd(int fd) {
    if (fd >= 0) ::close(fd);
}

} // anonymous namespace

struct TextureCache::VideoRegistry {
    TextureCache::VideoDecodeOptions          options;
    std::unique_ptr<wavsen::video::Producer>  producer;
    std::unique_ptr<wavsen::video::YuvToRgba> yuv;
    std::uint32_t                             yuv_max_width { 0 };
    std::uint32_t                             yuv_max_height { 0 };

    struct Slot {
        std::string   key; /* matches m_tex_map */
        std::uint32_t width { 0 };
        std::uint32_t height { 0 };
        /* RGBA8 target lives in m_tex_map[key].slots[0] (transferred
         * during CreateVideoTex). Pump retrieves it via lookup so the
         * single owner stays in the cache. */
        VmaImageParameters                           image; /* moved into m_tex_map */
        std::unique_ptr<wavsen::video::VideoDecoder> decoder;
        wavsen::video::Nv12Frame                     nv12_scratch;
        double                                       pts_acc { 0.0 };
        double                                       last_pts { -1.0 };
        bool                                         have_frame { false };
    };
    std::vector<std::unique_ptr<Slot>> slots;

    const wavsen::video::Producer* ensureProducer(const Device& device, std::uint32_t width,
                                                  std::uint32_t height) {
        if (producer) return producer.get();
        auto r =
            wavsen::video::Producer::from_external(MakeExternalProducerInfo(device, width, height));
        if (r.is_err()) {
            rstd_warn(
                "CreateVideoTex: shared-device producer unavailable; falling back to sw decode: {}",
                std::move(r).unwrap_err().message);
            return nullptr;
        }
        producer = std::move(r).unwrap();
        return producer.get();
    }

    wavsen::video::YuvToRgba* ensureYuv(const Device& device, std::uint32_t width,
                                        std::uint32_t height) {
        if (yuv && width <= yuv_max_width && height <= yuv_max_height) return yuv.get();
        auto next_w = std::max(width, yuv_max_width);
        auto next_h = std::max(height, yuv_max_height);
        auto r      = wavsen::video::YuvToRgba::create(device.instance_handle(),
                                                       *device.gpu(),
                                                       *device.handle(),
                                                       device.graphics_queue().family_index,
                                                       *device.graphics_queue().handle,
                                                       next_w,
                                                       next_h);
        if (r.is_err()) {
            rstd_error("CreateVideoTex: YuvToRgba create failed: {}",
                       std::move(r).unwrap_err().message);
            return nullptr;
        }
        yuv            = std::move(r).unwrap();
        yuv_max_width  = next_w;
        yuv_max_height = next_h;
        return yuv.get();
    }
};

ImageSlotsRef TextureCache::CreateVideoTex(Image& image) {
    if (image.slots.empty() || image.slots[0].mipmaps.empty()) return {};
    auto& mip = image.slots[0].mipmaps[0];
    if (! mip.videoStream || mip.videoSize <= 0 || mip.width <= 0 || mip.height <= 0) {
        rstd_error("CreateVideoTex: incomplete video-tex slot for {}", image.key);
        return {};
    }

    if (! m_video_registry) {
        m_video_registry          = std::make_unique<VideoRegistry>();
        m_video_registry->options = m_video_decode_options;
    }
    if (! m_tex_cmd) allocateCmd();

    /* Pull the IBinaryStream back out of the opaque shared_ptr<void>
     * the parser stored. The cast is safe because TextureAssetDecoder is
     * the only writer and always populates with shared_ptr<IBinaryStream>
     * via the converting constructor. */
    std::shared_ptr<sr::fs::IBinaryStream> pkg_stream(
        mip.videoStream, static_cast<sr::fs::IBinaryStream*>(mip.videoStream.get()));

    auto slot = std::make_unique<VideoRegistry::Slot>();
    slot->key = image.key;
    /* NV12 chroma is 4:2:0 → both dims even. */
    slot->width  = static_cast<std::uint32_t>(mip.width | (mip.width & 1));
    slot->height = static_cast<std::uint32_t>(mip.height | (mip.height & 1));
    if (slot->width != static_cast<std::uint32_t>(mip.width))
        slot->width = static_cast<std::uint32_t>(mip.width + 1);
    if (slot->height != static_cast<std::uint32_t>(mip.height))
        slot->height = static_cast<std::uint32_t>(mip.height + 1);

    /* 1) Allocate the stable RGBA8 target. */
    VkSamplerCreateInfo sampler_info {
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext                   = nullptr,
        .magFilter               = ToVkType(image.header.sample.magFilter),
        .minFilter               = ToVkType(image.header.sample.minFilter),
        .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU            = ToVkType(image.header.sample.wrapS),
        .addressModeV            = ToVkType(image.header.sample.wrapS),
        .addressModeW            = ToVkType(image.header.sample.wrapT),
        .anisotropyEnable        = false,
        .maxAnisotropy           = 1.0f,
        .compareEnable           = false,
        .compareOp               = VK_COMPARE_OP_NEVER,
        .minLod                  = 0.0f,
        .maxLod                  = 1.0f,
        .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = false,
    };
    VkExtent3D ext { slot->width, slot->height, 1 };
    auto       img_opt = CreateImage(m_device,
                                     ext,
                                     /*miplevel=*/1u,
                                     VK_FORMAT_R8G8B8A8_UNORM,
                                     sampler_info,
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                         VK_IMAGE_USAGE_SAMPLED_BIT);
    if (! img_opt) {
        rstd_error("CreateVideoTex: VkImage allocation failed for {}", image.key);
        return {};
    }
    slot->image = std::move(*img_opt);
    AssignImageGeneration(slot->image);

    if (! m_video_registry->ensureYuv(m_device, slot->width, slot->height)) return {};

    /* 2) Initial layout: UNDEFINED → TRANSFER_DST → clear black →
     * SHADER_READ_ONLY. Mirrors the one-shot pattern used by the
     * existing TransImgLayout / CopyImageData helpers in this file. */
    {
        ImageParameters ip = ToImageParameters(slot->image);
        VVK_CHECK(m_tex_cmd.Begin(VkCommandBufferBeginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        }));
        VkImageSubresourceRange range {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };
        VkImageMemoryBarrier to_xfer {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = 0,
            .dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image            = ip.handle,
            .subresourceRange = range,
        };
        m_tex_cmd.PipelineBarrier(
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, to_xfer);
        VkClearColorValue clear { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } };
        m_tex_cmd.ClearColorImage(ip.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, range);
        VkImageMemoryBarrier to_shader {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask    = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image            = ip.handle,
            .subresourceRange = range,
        };
        m_tex_cmd.PipelineBarrier(
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, to_shader);
        VVK_CHECK(m_tex_cmd.End());
        VkSubmitInfo si {
            .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers    = m_tex_cmd.address(),
        };
        VVK_CHECK(m_device.graphics_queue().handle.Submit(si));
        VVK_CHECK(m_device.handle().WaitIdle());
    }

    /* 3) Open the decoder. The factory hands out a fresh
     * PkgRangedInputStream per trial; the captured shared_ptr keeps the
     * underlying pkg file handle alive. */
    auto factory = [pkg = pkg_stream,
                    off = static_cast<std::int64_t>(mip.videoOffset),
                    len = static_cast<std::int64_t>(
                        mip.videoSize)]() -> std::unique_ptr<wavsen::video::IInputStream> {
        return std::make_unique<PkgRangedInputStream>(pkg, off, len);
    };
    const auto              requested_hwdec = ParseHwdec(m_video_registry->options.hwdec);
    wavsen::video::OpenOpts opts {
        requested_hwdec,
        {},
    };
    const wavsen::video::Producer* producer = nullptr;
    if (requested_hwdec != wavsen::video::HwAccel::None) {
        producer = m_video_registry->ensureProducer(m_device, slot->width, slot->height);
        if (! producer) opts.hwaccel = wavsen::video::HwAccel::None;
    }
    auto dec_r = wavsen::video::VideoDecoder::open_from_stream(std::move(factory),
                                                               slot->width,
                                                               slot->height,
                                                               /*loop=*/true,
                                                               producer,
                                                               opts);
    if (dec_r.is_err()) {
        rstd_error("CreateVideoTex: open_from_stream failed for {}: {}",
                   image.key,
                   dec_r.unwrap_err().message);
        return {};
    }
    slot->decoder = std::move(dec_r).unwrap();
    rstd_info("CreateVideoTex: {} hwdec={} decoder kind={}",
              image.key,
              HwdecLabel(requested_hwdec),
              FrameKindLabel(slot->decoder->kind()));

    /* 4) Move VkImage ownership into m_tex_map[key] (the canonical
     * texture cache) and keep VideoRegistry::Slot referencing it by
     * key — Pump fetches via lookup. This avoids dual ownership of
     * VmaImageParameters (move-only) while keeping the same VkImage
     * handle stable across frames. */
    ImageSlots img_slots {};
    img_slots.slots.resize(1);
    img_slots.slots[0]   = std::move(slot->image);
    m_tex_map[image.key] = std::move(img_slots);
    m_video_registry->slots.push_back(std::move(slot));
    return m_tex_map[image.key];
}

void TextureCache::PumpVideoTextures(double dt_seconds) {
    if (! m_video_registry || m_video_registry->slots.empty()) return;

    for (auto& up : m_video_registry->slots) {
        auto& s = *up;
        s.pts_acc += dt_seconds;
        auto* yuv = m_video_registry->ensureYuv(m_device, s.width, s.height);
        if (! yuv) continue;

        auto it = m_tex_map.find(s.key);
        if (it == m_tex_map.end() || it->second.slots.empty()) continue;
        ImageParameters ip = ToImageParameters(it->second.slots[0]);

        const auto                  fkind = s.decoder->kind();
        wavsen::video::VkFrameView  vkv {};
        wavsen::video::DrmFrameView drmv {};

        /* Drain decoded frames until we catch up to wall time. Cap to
         * 4 frames per tick to avoid spiral-of-death on heavy stalls. */
        bool got_new = false;
        for (int i = 0; i < 4; ++i) {
            if (s.last_pts >= 0.0 && s.last_pts > s.pts_acc) break;

            rstd::Result<wavsen::video::NextFrame, wavsen::video::Error> r =
                rstd::Ok(wavsen::video::NextFrame::Ok);
            switch (fkind) {
            case wavsen::video::FrameKind::VulkanShared: r = s.decoder->next_vk_frame(vkv); break;
            case wavsen::video::FrameKind::VaapiDrm: r = s.decoder->next_drm_frame(drmv); break;
            case wavsen::video::FrameKind::Sw: r = s.decoder->next_frame(s.nv12_scratch); break;
            }
            if (r.is_err()) {
                rstd_error("PumpVideoTextures[{}]: decode {}: {}",
                           s.key,
                           FrameKindLabel(fkind),
                           std::move(r).unwrap_err().message);
                break;
            }
            auto kind = r.unwrap();
            if (kind == wavsen::video::NextFrame::Eof) {
                /* Decoder set loop=true at open; on EOF it auto-rewinds,
                 * so a second call should yield data. Bail this tick. */
                break;
            }
            switch (fkind) {
            case wavsen::video::FrameKind::VulkanShared: s.last_pts = vkv.pts_seconds; break;
            case wavsen::video::FrameKind::VaapiDrm: s.last_pts = drmv.pts_seconds; break;
            case wavsen::video::FrameKind::Sw: s.last_pts = s.nv12_scratch.pts_seconds; break;
            }
            got_new = true;
        }
        if (! got_new && s.have_frame) continue; /* nothing to upload */
        if (! got_new) continue;

        std::uint32_t cs_id = 0;
        std::uint32_t cr_id = 0;
        switch (fkind) {
        case wavsen::video::FrameKind::VulkanShared:
            cs_id = vkv.colorspace;
            cr_id = vkv.color_range;
            break;
        case wavsen::video::FrameKind::VaapiDrm:
            cs_id = drmv.colorspace;
            cr_id = drmv.color_range;
            break;
        case wavsen::video::FrameKind::Sw:
            cs_id = s.nv12_scratch.colorspace;
            cr_id = s.nv12_scratch.color_range;
            break;
        }
        const auto color_matrix =
            wavsen::video::make_color_matrix(static_cast<wavsen::video::ColorSpace>(cs_id),
                                             static_cast<wavsen::video::ColorRange>(cr_id));

        rstd::Result<int, wavsen::video::Error> cv = rstd::Ok(-1);
        switch (fkind) {
        case wavsen::video::FrameKind::VulkanShared: {
            wavsen::video::YuvToRgba::VkFrameImports im {};
            im.y_image           = vkv.img[0];
            im.uv_image          = vkv.plane_count > 1 ? vkv.img[1] : VK_NULL_HANDLE;
            im.y_sem             = vkv.sem[0];
            im.uv_sem            = vkv.plane_count > 1 ? vkv.sem[1] : vkv.sem[0];
            im.y_sem_val_in_out  = &vkv.sem_value[0];
            im.uv_sem_val_in_out = vkv.plane_count > 1 ? &vkv.sem_value[1] : &vkv.sem_value[0];
            im.y_layout_in_out   = &vkv.layout[0];
            im.uv_layout_in_out  = vkv.plane_count > 1 ? &vkv.layout[1] : &vkv.layout[0];
            im.y_qf_in_out       = &vkv.queue_family[0];
            im.uv_qf_in_out = vkv.plane_count > 1 ? &vkv.queue_family[1] : &vkv.queue_family[0];
            im.src_w        = vkv.width;
            im.src_h        = vkv.height;
            im.bit_depth    = vkv.bit_depth;
            cv              = yuv->convert_av_vk_frame(im,
                                                       ip.handle,
                                                       s.width,
                                                       s.height,
                                                       color_matrix);
            break;
        }
        case wavsen::video::FrameKind::VaapiDrm:
            cv = yuv->convert_drm_prime(drmv,
                                        ip.handle,
                                        s.width,
                                        s.height,
                                        color_matrix);
            break;
        case wavsen::video::FrameKind::Sw:
            cv = yuv->convert_nv12(ip.handle,
                                   s.width,
                                   s.height,
                                   s.nv12_scratch.data.data(),
                                   s.nv12_scratch.data.size(),
                                   color_matrix);
            break;
        }
        if (cv.is_err()) {
            rstd_error("PumpVideoTextures[{}]: yuv conversion {}: {}",
                       s.key,
                       FrameKindLabel(fkind),
                       std::move(cv).unwrap_err().message);
            continue;
        }
        CloseSyncFd(std::move(cv).unwrap());
        s.have_frame = true;
    }
}

bool TextureCache::UploadFontAtlasRegion(const std::string& key, const std::uint8_t* atlas,
                                         std::uint32_t atlas_w, std::uint32_t x, std::uint32_t y,
                                         std::uint32_t w, std::uint32_t h) {
    if (w == 0 || h == 0) return true;
    auto it = m_tex_map.find(key);
    if (it == m_tex_map.end() || it->second.slots.empty()) return false;

    ImageParameters ip = ToImageParameters(it->second.slots[0]);

    // Tightly-packed staging buffer for the AABB. Allocating per-call keeps
    // this code path independent of the video-tex ring; atlas pumps are
    // small (a handful of glyphs per frame) so cost is negligible.
    const std::uint32_t bytes = w * h;
    VmaBufferParameters stage;
    if (! CreateStagingBuffer(m_device.vma_allocator(), bytes, stage)) return false;

    {
        void* v = nullptr;
        VVK_CHECK(stage.handle.MapMemory(&v));
        auto* dst = static_cast<std::uint8_t*>(v);
        for (std::uint32_t row = 0; row < h; ++row) {
            std::memcpy(dst + row * w, atlas + (y + row) * atlas_w + x, w);
        }
        stage.handle.UnMapMemory();
    }

    if (! m_tex_cmd) allocateCmd();
    VVK_CHECK(m_tex_cmd.Begin(VkCommandBufferBeginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    }));
    VkImageSubresourceRange range {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };
    VkImageMemoryBarrier to_xfer {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask    = VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .image            = ip.handle,
        .subresourceRange = range,
    };
    m_tex_cmd.PipelineBarrier(
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, to_xfer);
    VkBufferImageCopy region {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageOffset                 = VkOffset3D { (int32_t)x, (int32_t)y, 0 };
    region.imageExtent                 = VkExtent3D { w, h, 1 };
    m_tex_cmd.CopyBufferToImage(
        *stage.handle, ip.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, region);
    VkImageMemoryBarrier to_shader {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask    = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .image            = ip.handle,
        .subresourceRange = range,
    };
    m_tex_cmd.PipelineBarrier(
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, to_shader);
    VVK_CHECK(m_tex_cmd.End());
    VkSubmitInfo si {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = m_tex_cmd.address(),
    };
    VVK_CHECK(m_device.graphics_queue().handle.Submit(si));
    VVK_CHECK(m_device.handle().WaitIdle());
    return true;
}

TextureCache::TextureCache(const Device& device): m_device(device) {}

TextureCache::~TextureCache() {};

uint64_t TextureCache::nextImageGeneration() { return m_next_image_generation++; }

void TextureCache::AssignImageGeneration(VmaImageParameters& image) {
    image.generation = nextImageGeneration();
}

void TextureCache::SetVideoDecodeOptions(VideoDecodeOptions options) {
    m_video_decode_options = std::move(options);
    if (m_video_registry) {
        m_video_registry->options = m_video_decode_options;
        if (m_video_registry->slots.empty()) m_video_registry->producer.reset();
    }
}

void TextureCache::Clear() {
    m_tex_map.clear();
    ClearTransientGraphResources();
    if (m_video_registry) m_video_registry->slots.clear();
}

void TextureCache::ClearTransientGraphResources() {
    m_query_map.clear();
    m_query_texs.clear();
}

std::optional<ImageParameters> TextureCache::Query(std::string_view key, TextureKey content_hash,
                                                   bool persist) {
    std::string query_key(key);
    TexHash      tex_hash = TextureKey::HashValue(content_hash);

    if (auto it = m_query_map.find(query_key); it != m_query_map.end()) {
        auto& query = *(it->second);

        if (query.content_hash != tex_hash) {
            query.query_keys.erase(query_key);
            query.share_ready = ! query.persist && query.query_keys.empty();
            m_query_map.erase(it);
        } else {
            query.share_ready = false;
            query.persist     = persist;

            return ToImageParameters(query.image);
        }
    }

    for (auto& query : m_query_texs) {
        if (! (query->share_ready)) continue;
        if (query->content_hash != tex_hash) continue;

        query->share_ready = false;
        query->persist     = persist;
        query->query_keys.insert(query_key);

        m_query_map[query_key] = &(*query);

        return ToImageParameters(query->image);
    }

    m_query_texs.emplace_back(std::make_unique<QueryTex>());
    auto& query            = *m_query_texs.back();
    m_query_map[query_key] = &query;

    query.index        = (idx)m_query_texs.size() - 1;
    query.content_hash = tex_hash;
    query.query_keys.insert(std::move(query_key));
    query.persist = persist;
    if (auto opt = CreateTex(content_hash); opt.has_value()) {
        query.image = std::move(opt.value());
        return ToImageParameters(query.image);
    }
    return std::nullopt;
}

void TextureCache::MarkShareReady(std::string_view key) {
    auto it = m_query_map.find(key);
    if (it != m_query_map.end()) {
        auto& query = it->second;
        if (query->persist) return;
        query->query_keys.erase(std::string(key));
        query->share_ready = query->query_keys.empty();
        m_query_map.erase(it);
    }
}

void TextureCache::RecGenerateMipmaps(vvk::CommandBuffer& cmd, const ImageParameters& image) const {
    VkImageMemoryBarrier barrier {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext               = nullptr,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image.handle,
        .subresourceRange =
            VkImageSubresourceRange {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
    };
    /*
    cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_DEPENDENCY_BY_REGION_BIT,
                        out_bar);
        */

    i32 mipWidth  = (i32)image.extent.width;
    i32 mipHeight = (i32)image.extent.height;

    for (unsigned i = 1; i < image.mipmap_level; i++) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout                     = i == 1 ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                       : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = i == 1 ? VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        VkPipelineStageFlags src_stage =
            i == 1 ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT;
        cmd.PipelineBarrier(
            src_stage, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_DEPENDENCY_BY_REGION_BIT, barrier);

        barrier.subresourceRange.baseMipLevel = i;
        barrier.oldLayout                     = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout                     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask                 = 0;
        barrier.dstAccessMask                 = VK_ACCESS_TRANSFER_WRITE_BIT;

        cmd.PipelineBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_DEPENDENCY_BY_REGION_BIT,
                            barrier);

        VkImageBlit blit {
            .srcSubresource =
                VkImageSubresourceLayers {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel       = i - 1,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
                },
            .srcOffsets = { VkOffset3D { 0, 0, 0 }, VkOffset3D { mipWidth, mipHeight, 1 } },
            .dstOffsets = { VkOffset3D { 0, 0, 0 },
                            VkOffset3D { mipWidth > 1 ? mipWidth / 2 : 1,
                                         mipHeight > 1 ? mipHeight / 2 : 1,
                                         1 } },
        };
        blit.dstSubresource =
            VkImageSubresourceLayers {
                .aspectMask     = blit.srcSubresource.aspectMask,
                .mipLevel       = blit.srcSubresource.mipLevel + 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },

        cmd.BlitImage(image.handle,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      image.handle,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      blit,
                      VK_FILTER_LINEAR);

        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout                     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout                     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask                 = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask                 = VK_ACCESS_SHADER_READ_BIT;

        cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            VK_DEPENDENCY_BY_REGION_BIT,
                            barrier);

        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    barrier.subresourceRange.baseMipLevel = image.mipmap_level - 1;
    barrier.oldLayout                     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout                     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask                 = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask                 = VK_ACCESS_SHADER_READ_BIT;

    cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_DEPENDENCY_BY_REGION_BIT,
                        barrier);
}
