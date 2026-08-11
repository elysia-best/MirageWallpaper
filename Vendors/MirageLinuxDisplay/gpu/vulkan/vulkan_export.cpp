#include "mirage_display_vulkan_export.h"

#include "mirage_display_vulkan.h"

#include "common/drm.hpp"
#include "common/util.hpp"
#include "vulkan_util.hpp"

#include <array>
#include <bit>
#include <cerrno>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>

#include <fcntl.h>

/*
 * Vulkan DMA-BUF exporter (include/mirage_display_vulkan_export.h) used by the
 * renderer side to turn Vulkan images into protocol frames.
 *
 * Exports a signaled binary semaphore as a sync_file, creates an unsignaled DRM
 * syncobj per frame for the consumer release, and recycles slots only after the
 * consumer signals release.
 */

struct md_vk_export_slot {
    VkImage image;
    VkDeviceMemory memory;
    uint32_t release_handle;
    bool acquired;
    bool busy;
    bool foreign_owned;
};

struct md_vk_exporter {
    md_vk_export_context_t context;
    /* Resolved once from the device; NULL means the driver lacks the extension. */
    PFN_vkGetMemoryFdKHR get_memory_fd;
    PFN_vkGetSemaphoreFdKHR get_semaphore_fd;
    PFN_vkGetImageDrmFormatModifierPropertiesEXT get_modifier_properties;
    /* Invalid until md_vk_exporter_new() duplicates or opens the render node. */
    mirage::UniqueFd drm_fd{mirage::kInvalidFd};
    md_buffer_pool_t pool;
    std::array<md_vk_export_slot, MIRAGE_DISPLAY_MAX_BUFFERS> slots;
    VkFormat format;
    uint32_t cursor;
    bool pool_active;
    VkCommandPool copy_command_pool;
    VkCommandBuffer copy_command_buffer;
    VkFence copy_fence;
    VkSemaphore copy_semaphore;
    bool copy_fence_pending;
};

namespace {

void clear_slot(md_vk_export_slot* const slot) {
    slot->image = VK_NULL_HANDLE;
    slot->memory = VK_NULL_HANDLE;
    slot->release_handle = 0U;
    slot->acquired = false;
    slot->busy = false;
    slot->foreign_owned = false;
}

void destroy_release_handle(md_vk_exporter_t* const exporter, const uint32_t index) {
    md_vk_export_slot& slot = exporter->slots[index];
    md_drm_destroy_syncobj(exporter->drm_fd.get(), slot.release_handle);
    slot.release_handle = 0U;
    slot.acquired = false;
    slot.busy = false;
}

/*
 * The DRM UAPI explicitly encodes a userspace pointer as uint64_t.  bit_cast
 * preserves those ABI bits without a C-style or reinterpret cast; the array
 * remains live until ioctl returns.
 */
md_result_t poll_release(md_vk_exporter_t* const exporter, const uint32_t index) {
    md_vk_export_slot& slot = exporter->slots[index];
    if (!slot.busy) {
        return MD_OK;
    }

    const std::array<uint32_t, 1U> handles{slot.release_handle};
    md_drm_syncobj_wait wait{};
    wait.handles = std::bit_cast<uint64_t>(handles.data());
    wait.timeout_nsec = 0;
    wait.count_handles = 1U;
    wait.flags = MD_DRM_SYNCOBJ_WAIT_ALL;
    const int wait_result = ioctl(exporter->drm_fd.get(), MD_DRM_IOCTL_SYNCOBJ_WAIT, &wait);
    if (wait_result == 0) {
        destroy_release_handle(exporter, index);
        return MD_OK;
    }
    if (errno == ETIME || errno == EBUSY) {
        return MD_ERR_WOULD_BLOCK;
    }
    return MD_ERR_IO;
}

/*
 * Both export paths create the same consumer-owned release syncobj.  Keeping
 * the ioctl sequence in one place makes the handle/FD ownership atomic:
 * success transfers both outputs to the caller, failure destroys the handle.
 */
md_result_t create_release_syncobj(const int32_t drm_fd, uint32_t* const out_handle,
                                   int32_t* const out_descriptor) {
    md_drm_syncobj_create create{};
    if (ioctl(drm_fd, MD_DRM_IOCTL_SYNCOBJ_CREATE, &create) != 0) {
        return MD_ERR_IO;
    }

    md_drm_syncobj_handle exported{};
    exported.handle = create.handle;
    exported.fd = mirage::kInvalidFd;
    if (ioctl(drm_fd, MD_DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD, &exported) != 0 ||
        exported.fd < 0) {
        md_drm_destroy_syncobj(drm_fd, create.handle);
        return MD_ERR_IO;
    }

    *out_handle = create.handle;
    *out_descriptor = exported.fd;
    return MD_OK;
}

void configure_color_range(VkImageSubresourceRange* const range) {
    range->aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range->baseMipLevel = 0U;
    range->levelCount = 1U;
    range->baseArrayLayer = 0U;
    range->layerCount = 1U;
}

/* This command buffer is reused serially, so an unfinished prior copy must
 * complete before command memory is reset and the next source is recorded. */
md_result_t prepare_copy_commands(md_vk_exporter_t* const exporter,
                                  const uint32_t buffer_index,
                                  const VkImage source_image,
                                  const VkImageLayout source_layout) {
    if (exporter->copy_fence_pending) {
        if (vkWaitForFences(exporter->context.device, 1U, &exporter->copy_fence,
                            VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
            return MD_ERR_IO;
        }
        exporter->copy_fence_pending = false;
    }
    if (vkResetFences(exporter->context.device, 1U, &exporter->copy_fence) != VK_SUCCESS ||
        vkResetCommandPool(exporter->context.device, exporter->copy_command_pool, 0U) !=
            VK_SUCCESS) {
        return MD_ERR_IO;
    }

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(exporter->copy_command_buffer, &begin_info) != VK_SUCCESS) {
        return MD_ERR_IO;
    }

    md_vk_export_slot& slot = exporter->slots[buffer_index];
    const VkAccessFlags source_access = source_layout == VK_IMAGE_LAYOUT_GENERAL
                                            ? VK_ACCESS_SHADER_WRITE_BIT
                                            : VK_ACCESS_SHADER_READ_BIT;
    std::array<VkImageMemoryBarrier, 2U> before{};
    before[0U].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    before[0U].srcAccessMask = source_access;
    before[0U].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    before[0U].oldLayout = source_layout;
    before[0U].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    before[0U].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before[0U].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before[0U].image = source_image;
    configure_color_range(&before[0U].subresourceRange);

    before[1U].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    before[1U].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    before[1U].oldLayout = slot.foreign_owned ? VK_IMAGE_LAYOUT_GENERAL
                                              : VK_IMAGE_LAYOUT_UNDEFINED;
    before[1U].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    before[1U].srcQueueFamilyIndex = slot.foreign_owned ? VK_QUEUE_FAMILY_FOREIGN_EXT
                                                        : VK_QUEUE_FAMILY_IGNORED;
    before[1U].dstQueueFamilyIndex = slot.foreign_owned
                                        ? exporter->context.queue_family_index
                                        : VK_QUEUE_FAMILY_IGNORED;
    before[1U].image = slot.image;
    configure_color_range(&before[1U].subresourceRange);

    vkCmdPipelineBarrier(exporter->copy_command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, nullptr, 0U, nullptr,
                         static_cast<uint32_t>(before.size()), before.data());

    /* Copy only the negotiated pool extent.  The caller has already supplied
     * a source at least that large, including any implementation alignment. */
    VkImageCopy region{};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.mipLevel = 0U;
    region.srcSubresource.baseArrayLayer = 0U;
    region.srcSubresource.layerCount = 1U;
    region.dstSubresource = region.srcSubresource;
    region.extent = VkExtent3D{exporter->pool.width, exporter->pool.height, 1U};
    vkCmdCopyImage(exporter->copy_command_buffer, source_image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, slot.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &region);

    std::array<VkImageMemoryBarrier, 2U> after{};
    after[0U].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    after[0U].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    after[0U].dstAccessMask = source_access;
    after[0U].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    after[0U].newLayout = source_layout;
    after[0U].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    after[0U].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    after[0U].image = source_image;
    configure_color_range(&after[0U].subresourceRange);

    after[1U].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    after[1U].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    after[1U].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    after[1U].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    after[1U].srcQueueFamilyIndex = exporter->context.queue_family_index;
    after[1U].dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;
    after[1U].image = slot.image;
    configure_color_range(&after[1U].subresourceRange);

    vkCmdPipelineBarrier(exporter->copy_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0U, 0U, nullptr, 0U, nullptr,
                         static_cast<uint32_t>(after.size()), after.data());
    return vkEndCommandBuffer(exporter->copy_command_buffer) == VK_SUCCESS ? MD_OK : MD_ERR_IO;
}

md_result_t allocate_slot(md_vk_exporter_t* const exporter,
                          const md_vk_export_pool_info_t* const info,
                          const uint32_t index) {
    const std::array<uint64_t, 1U> modifiers{info->modifier};
    VkImageDrmFormatModifierListCreateInfoEXT modifier_list{};
    modifier_list.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT;
    modifier_list.drmFormatModifierCount = 1U;
    modifier_list.pDrmFormatModifiers = modifiers.data();

    VkExternalMemoryImageCreateInfo external_image{};
    external_image.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external_image.pNext = &modifier_list;
    external_image.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext = &external_image;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = exporter->format;
    image_info.extent = VkExtent3D{info->width, info->height, 1U};
    image_info.mipLevels = 1U;
    image_info.arrayLayers = 1U;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image = VK_NULL_HANDLE;
    const VkResult image_result =
        vkCreateImage(exporter->context.device, &image_info, nullptr, &image);
    if (image_result != VK_SUCCESS) {
        return image_result == VK_ERROR_EXTENSION_NOT_PRESENT ? MD_ERR_UNSUPPORTED : MD_ERR_IO;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(exporter->context.device, image, &requirements);
    const std::optional<uint32_t> memory_type = mirage::vulkan::choose_memory_type(
        exporter->context.physical_device, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!memory_type.has_value()) {
        vkDestroyImage(exporter->context.device, image, nullptr);
        return MD_ERR_UNSUPPORTED;
    }

    VkMemoryDedicatedAllocateInfo dedicated{};
    dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated.image = image;

    VkExportMemoryAllocateInfo export_info{};
    export_info.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    export_info.pNext = &dedicated;
    export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.pNext = &export_info;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memory_type.value();

    VkDeviceMemory memory = VK_NULL_HANDLE;
    const VkResult allocation_result =
        vkAllocateMemory(exporter->context.device, &allocation, nullptr, &memory);
    if (allocation_result != VK_SUCCESS) {
        vkDestroyImage(exporter->context.device, image, nullptr);
        return allocation_result == VK_ERROR_OUT_OF_HOST_MEMORY ? MD_ERR_NOMEM : MD_ERR_IO;
    }
    const VkResult bind_result =
        vkBindImageMemory(exporter->context.device, image, memory, 0U);
    if (bind_result != VK_SUCCESS) {
        vkFreeMemory(exporter->context.device, memory, nullptr);
        vkDestroyImage(exporter->context.device, image, nullptr);
        return MD_ERR_IO;
    }

    VkMemoryGetFdInfoKHR fd_info{};
    fd_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    fd_info.memory = memory;
    fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    int32_t dma_buf_descriptor = mirage::kInvalidFd;
    const VkResult descriptor_result =
        exporter->get_memory_fd(exporter->context.device, &fd_info, &dma_buf_descriptor);
    if (descriptor_result != VK_SUCCESS || dma_buf_descriptor < 0) {
        vkFreeMemory(exporter->context.device, memory, nullptr);
        vkDestroyImage(exporter->context.device, image, nullptr);
        return descriptor_result == VK_ERROR_EXTENSION_NOT_PRESENT ? MD_ERR_UNSUPPORTED
                                                                    : MD_ERR_IO;
    }
    mirage::UniqueFd dma_buf_fd{dma_buf_descriptor};

    VkImageDrmFormatModifierPropertiesEXT modifier_properties{};
    modifier_properties.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT;
    const VkResult modifier_result = exporter->get_modifier_properties(
        exporter->context.device, image, &modifier_properties);
    if (modifier_result != VK_SUCCESS || modifier_properties.drmFormatModifier != info->modifier) {
        vkFreeMemory(exporter->context.device, memory, nullptr);
        vkDestroyImage(exporter->context.device, image, nullptr);
        return modifier_result == VK_ERROR_EXTENSION_NOT_PRESENT ? MD_ERR_UNSUPPORTED
                                                                  : MD_ERR_IO;
    }

    std::array<std::optional<mirage::UniqueFd>, MIRAGE_DISPLAY_MAX_PLANES> plane_fds{};
    std::array<VkSubresourceLayout, MIRAGE_DISPLAY_MAX_PLANES> layouts{};
    for (uint32_t plane_index = 0U; plane_index < info->plane_count; ++plane_index) {
        if (mirage::vulkan::memory_plane_aspect(plane_index) == VK_IMAGE_ASPECT_NONE) {
            vkFreeMemory(exporter->context.device, memory, nullptr);
            vkDestroyImage(exporter->context.device, image, nullptr);
            return MD_ERR_UNSUPPORTED;
        }
        const int duplicated_descriptor =
            plane_index == 0U ? dma_buf_fd.get()
                              : fcntl(dma_buf_fd.get(), F_DUPFD_CLOEXEC, 0);
        if (duplicated_descriptor < 0) {
            vkFreeMemory(exporter->context.device, memory, nullptr);
            vkDestroyImage(exporter->context.device, image, nullptr);
            return MD_ERR_IO;
        }
        if (plane_index == 0U) {
            plane_fds[plane_index].emplace(dma_buf_fd.release());
        } else {
            plane_fds[plane_index].emplace(static_cast<int32_t>(duplicated_descriptor));
        }

        VkImageSubresource subresource{};
        subresource.aspectMask = mirage::vulkan::memory_plane_aspect(plane_index);
        vkGetImageSubresourceLayout(exporter->context.device, image, &subresource,
                                    &layouts[plane_index]);
        if (layouts[plane_index].rowPitch > UINT32_MAX ||
            layouts[plane_index].offset > UINT32_MAX || layouts[plane_index].size == 0U) {
            vkFreeMemory(exporter->context.device, memory, nullptr);
            vkDestroyImage(exporter->context.device, image, nullptr);
            return MD_ERR_UNSUPPORTED;
        }
    }

    md_vk_export_slot& slot = exporter->slots[index];
    slot.image = image;
    slot.memory = memory;
    for (uint32_t plane_index = 0U; plane_index < info->plane_count; ++plane_index) {
        md_plane_t& plane = exporter->pool.planes[index][plane_index];
        plane.fd = plane_fds[plane_index]->release();
        plane.stride = static_cast<uint32_t>(layouts[plane_index].rowPitch);
        plane.offset = static_cast<uint32_t>(layouts[plane_index].offset);
        plane.size = layouts[plane_index].size;
    }
    return MD_OK;
}

}  // namespace


/*
 * Creates the exporter, opening the DRM render node from the context (or
 * duplicating an already-open one) so release syncobjs can be created and
 * polled on the same node the producer renders with.
 */
extern "C" md_vk_exporter_t* md_vk_exporter_new(
    const md_vk_export_context_t* const context) {
    if (context == nullptr || context->instance == VK_NULL_HANDLE ||
        context->physical_device == VK_NULL_HANDLE || context->device == VK_NULL_HANDLE ||
        context->queue == VK_NULL_HANDLE) {
        return nullptr;
    }

    std::unique_ptr<md_vk_exporter_t> exporter{new (std::nothrow) md_vk_exporter_t{}};
    if (!exporter) {
        return nullptr;
    }
    exporter->context = *context;
    const int drm_descriptor = context->drm_render_fd >= 0
                                   ? fcntl(context->drm_render_fd, F_DUPFD_CLOEXEC, 0)
                                   : md_drm_open_render_node(context->drm_render_major,
                                                             context->drm_render_minor);
    if (drm_descriptor < 0) {
        return nullptr;
    }
    exporter->drm_fd = mirage::UniqueFd{static_cast<int32_t>(drm_descriptor)};
    exporter->get_memory_fd = std::bit_cast<PFN_vkGetMemoryFdKHR>(
        vkGetDeviceProcAddr(context->device, "vkGetMemoryFdKHR"));
    exporter->get_semaphore_fd = std::bit_cast<PFN_vkGetSemaphoreFdKHR>(
        vkGetDeviceProcAddr(context->device, "vkGetSemaphoreFdKHR"));
    exporter->get_modifier_properties =
        std::bit_cast<PFN_vkGetImageDrmFormatModifierPropertiesEXT>(
            vkGetDeviceProcAddr(context->device, "vkGetImageDrmFormatModifierPropertiesEXT"));
    if (exporter->get_memory_fd == nullptr || exporter->get_semaphore_fd == nullptr ||
        exporter->get_modifier_properties == nullptr) {
        return nullptr;
    }
    md_init_pool(&exporter->pool);
    for (md_vk_export_slot& slot : exporter->slots) {
        clear_slot(&slot);
    }
    exporter->format = VK_FORMAT_UNDEFINED;

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = context->queue_family_index;
    if (vkCreateCommandPool(context->device, &pool_info, nullptr,
                            &exporter->copy_command_pool) != VK_SUCCESS) {
        return nullptr;
    }

    VkCommandBufferAllocateInfo command_info{};
    command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_info.commandPool = exporter->copy_command_pool;
    command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_info.commandBufferCount = 1U;
    if (vkAllocateCommandBuffers(context->device, &command_info,
                                 &exporter->copy_command_buffer) != VK_SUCCESS) {
        vkDestroyCommandPool(context->device, exporter->copy_command_pool, nullptr);
        return nullptr;
    }

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(context->device, &fence_info, nullptr, &exporter->copy_fence) !=
        VK_SUCCESS) {
        vkDestroyCommandPool(context->device, exporter->copy_command_pool, nullptr);
        return nullptr;
    }

    VkExportSemaphoreCreateInfo export_semaphore{};
    export_semaphore.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
    export_semaphore.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphore_info.pNext = &export_semaphore;
    if (vkCreateSemaphore(context->device, &semaphore_info, nullptr,
                          &exporter->copy_semaphore) != VK_SUCCESS) {
        vkDestroyFence(context->device, exporter->copy_fence, nullptr);
        vkDestroyCommandPool(context->device, exporter->copy_command_pool, nullptr);
        return nullptr;
    }
    return exporter.release();
}

extern "C" void md_vk_exporter_release_pool(md_vk_exporter_t* const exporter) {
    if (exporter == nullptr) {
        return;
    }
    if (exporter->pool_active &&
        vkDeviceWaitIdle(exporter->context.device) != VK_SUCCESS) {
        /* Release is a void C API.  Device loss cannot retain caller-owned
         * descriptors or slots after this exporter is being retired. */
    }

    for (uint32_t buffer_index = 0U; buffer_index < MIRAGE_DISPLAY_MAX_BUFFERS;
         ++buffer_index) {
        destroy_release_handle(exporter, buffer_index);
        md_vk_export_slot& slot = exporter->slots[buffer_index];
        if (slot.image != VK_NULL_HANDLE) {
            vkDestroyImage(exporter->context.device, slot.image, nullptr);
        }
        if (slot.memory != VK_NULL_HANDLE) {
            vkFreeMemory(exporter->context.device, slot.memory, nullptr);
        }
        clear_slot(&slot);
    }
    md_close_pool(&exporter->pool);
    exporter->format = VK_FORMAT_UNDEFINED;
    exporter->cursor = 0U;
    exporter->pool_active = false;
    exporter->copy_fence_pending = false;
}

extern "C" void md_vk_exporter_free(md_vk_exporter_t* const exporter) {
    if (exporter == nullptr) {
        return;
    }
    md_vk_exporter_release_pool(exporter);
    if (exporter->copy_semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(exporter->context.device, exporter->copy_semaphore, nullptr);
    }
    if (exporter->copy_fence != VK_NULL_HANDLE) {
        vkDestroyFence(exporter->context.device, exporter->copy_fence, nullptr);
    }
    if (exporter->copy_command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(exporter->context.device, exporter->copy_command_pool, nullptr);
    }
    delete exporter;
}


/*
 * Creates the DMA-BUF pool: images, per-plane exported memory, and the
 * per-slot release syncobj handles.  Callers must wait for their own device work
 * before replacing the current pool.
 */
extern "C" md_result_t md_vk_exporter_create_pool(
    md_vk_exporter_t* const exporter, const md_vk_export_pool_info_t* const info) {
    if (exporter == nullptr || info == nullptr || info->generation == 0U ||
        info->buffer_count < 2U || info->buffer_count > MIRAGE_DISPLAY_MAX_BUFFERS ||
        info->width == 0U || info->height == 0U || info->plane_count == 0U ||
        info->plane_count > MIRAGE_DISPLAY_MAX_PLANES) {
        return MD_ERR_INVALID;
    }

    VkComponentMapping mapping{};
    VkFormat format{};
    const md_result_t format_result = md_vk_fourcc_to_format(info->fourcc, &format, &mapping);
    if (format_result != MD_OK || format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM) {
        return MD_ERR_UNSUPPORTED;
    }

    md_vk_exporter_release_pool(exporter);
    exporter->format = format;
    exporter->pool.generation = info->generation;
    exporter->pool.buffer_count = info->buffer_count;
    exporter->pool.width = info->width;
    exporter->pool.height = info->height;
    exporter->pool.fourcc = info->fourcc;
    exporter->pool.plane_count = info->plane_count;
    exporter->pool.modifier = info->modifier;
    for (uint32_t buffer_index = 0U; buffer_index < info->buffer_count; ++buffer_index) {
        const md_result_t allocation_result = allocate_slot(exporter, info, buffer_index);
        if (allocation_result != MD_OK) {
            md_vk_exporter_release_pool(exporter);
            return allocation_result;
        }
    }
    exporter->pool_active = true;
    return MD_OK;
}

extern "C" const md_buffer_pool_t* md_vk_exporter_pool(
    const md_vk_exporter_t* const exporter) {
    if (exporter == nullptr || !exporter->pool_active) {
        return nullptr;
    }
    return &exporter->pool;
}

extern "C" VkImage md_vk_exporter_image(const md_vk_exporter_t* const exporter,
                                          const uint32_t buffer_index) {
    if (exporter == nullptr || !exporter->pool_active ||
        buffer_index >= exporter->pool.buffer_count) {
        return VK_NULL_HANDLE;
    }
    return exporter->slots[buffer_index].image;
}

extern "C" VkFormat md_vk_exporter_format(const md_vk_exporter_t* const exporter) {
    return exporter != nullptr && exporter->pool_active ? exporter->format
                                                        : VK_FORMAT_UNDEFINED;
}


/*
 * Polls every slot's release syncobj and returns the first free index.  A
 * slot is never reused before its consumer signals release, so MD_ERR_WOULD_BLOCK
 * means the producer must skip the current frame.
 */
extern "C" md_result_t md_vk_exporter_acquire(md_vk_exporter_t* const exporter,
                                                uint32_t* const out_buffer_index) {
    if (exporter == nullptr || out_buffer_index == nullptr || !exporter->pool_active) {
        return MD_ERR_STATE;
    }

    md_result_t first_error = MD_OK;
    for (uint32_t offset = 0U; offset < exporter->pool.buffer_count; ++offset) {
        const uint32_t index = (exporter->cursor + offset) % exporter->pool.buffer_count;
        const md_result_t poll_result = poll_release(exporter, index);
        md_vk_export_slot& slot = exporter->slots[index];
        if (poll_result == MD_OK && !slot.busy && !slot.acquired) {
            exporter->cursor = (index + 1U) % exporter->pool.buffer_count;
            slot.acquired = true;
            *out_buffer_index = index;
            return MD_OK;
        }
        if (poll_result != MD_ERR_WOULD_BLOCK && first_error == MD_OK) {
            first_error = poll_result;
        }
    }
    return first_error != MD_OK ? first_error : MD_ERR_WOULD_BLOCK;
}


/*
 * Exports a signaled binary Vulkan semaphore as a sync_file (the acquire FD)
 * and creates a fresh unsignaled DRM syncobj (the release FD).  Both descriptors
 * are caller-owned and feed md_producer_submit_frame directly.
 */
extern "C" md_result_t md_vk_exporter_export_frame(
    md_vk_exporter_t* const exporter, const uint32_t buffer_index,
    const VkSemaphore acquire_semaphore, int32_t* const out_acquire_sync_fd,
    int32_t* const out_release_syncobj_fd) {
    if (out_acquire_sync_fd != nullptr) {
        *out_acquire_sync_fd = mirage::kInvalidFd;
    }
    if (out_release_syncobj_fd != nullptr) {
        *out_release_syncobj_fd = mirage::kInvalidFd;
    }
    if (exporter == nullptr || !exporter->pool_active ||
        acquire_semaphore == VK_NULL_HANDLE || out_acquire_sync_fd == nullptr ||
        out_release_syncobj_fd == nullptr || buffer_index >= exporter->pool.buffer_count ||
        exporter->slots[buffer_index].busy || !exporter->slots[buffer_index].acquired) {
        return MD_ERR_INVALID;
    }

    VkSemaphoreGetFdInfoKHR semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
    semaphore_info.semaphore = acquire_semaphore;
    semaphore_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    int32_t acquire_descriptor = mirage::kInvalidFd;
    const VkResult acquire_result = exporter->get_semaphore_fd(
        exporter->context.device,
                                                         &semaphore_info,
                                                         &acquire_descriptor);
    if (acquire_result != VK_SUCCESS || acquire_descriptor < 0) {
        return acquire_result == VK_ERROR_EXTENSION_NOT_PRESENT ? MD_ERR_UNSUPPORTED : MD_ERR_IO;
    }
    mirage::UniqueFd acquire_fd{acquire_descriptor};

    uint32_t release_handle = 0U;
    int32_t release_descriptor = mirage::kInvalidFd;
    const md_result_t release_result = create_release_syncobj(
        exporter->drm_fd.get(), &release_handle, &release_descriptor);
    if (release_result != MD_OK) {
        return release_result;
    }
    mirage::UniqueFd release_fd{release_descriptor};

    md_vk_export_slot& slot = exporter->slots[buffer_index];
    slot.release_handle = release_handle;
    slot.acquired = false;
    slot.busy = true;
    *out_acquire_sync_fd = acquire_fd.release();
    *out_release_syncobj_fd = release_fd.release();
    return MD_OK;
}


/*
 * GPU-copies a caller-owned RGBA image into an acquired export slot and then
 * exports the copy as a protocol frame, restoring the source layout and
 * performing the FOREIGN queue-family handoffs required by protocol v1.
 */
extern "C" md_result_t md_vk_exporter_copy_frame(
    md_vk_exporter_t* const exporter, const uint32_t buffer_index,
    const VkImage source_image, const VkImageLayout source_layout,
    const uint32_t source_width, const uint32_t source_height,
    int32_t* const out_acquire_sync_fd, int32_t* const out_release_syncobj_fd) {
    if (out_acquire_sync_fd != nullptr) {
        *out_acquire_sync_fd = mirage::kInvalidFd;
    }
    if (out_release_syncobj_fd != nullptr) {
        *out_release_syncobj_fd = mirage::kInvalidFd;
    }
    if (exporter == nullptr || !exporter->pool_active || source_image == VK_NULL_HANDLE ||
        source_width == 0U || source_height == 0U || source_width < exporter->pool.width ||
        source_height < exporter->pool.height || out_acquire_sync_fd == nullptr ||
        out_release_syncobj_fd == nullptr || buffer_index >= exporter->pool.buffer_count ||
        exporter->slots[buffer_index].busy || !exporter->slots[buffer_index].acquired) {
        return MD_ERR_INVALID;
    }

    const md_result_t command_result =
        prepare_copy_commands(exporter, buffer_index, source_image, source_layout);
    if (command_result != MD_OK) {
        return command_result;
    }

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1U;
    submit_info.pCommandBuffers = &exporter->copy_command_buffer;
    submit_info.signalSemaphoreCount = 1U;
    submit_info.pSignalSemaphores = &exporter->copy_semaphore;
    if (vkQueueSubmit(exporter->context.queue, 1U, &submit_info,
                      exporter->copy_fence) != VK_SUCCESS) {
        return MD_ERR_IO;
    }
    exporter->copy_fence_pending = true;
    exporter->slots[buffer_index].foreign_owned = true;

    VkSemaphoreGetFdInfoKHR semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
    semaphore_info.semaphore = exporter->copy_semaphore;
    semaphore_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    int32_t acquire_descriptor = mirage::kInvalidFd;
    const VkResult acquire_result = exporter->get_semaphore_fd(
        exporter->context.device,
                                                         &semaphore_info,
                                                         &acquire_descriptor);
    if (acquire_result != VK_SUCCESS || acquire_descriptor < 0) {
        md_vk_exporter_cancel_frame(exporter, buffer_index);
        return acquire_result == VK_ERROR_EXTENSION_NOT_PRESENT ? MD_ERR_UNSUPPORTED : MD_ERR_IO;
    }
    mirage::UniqueFd acquire_fd{acquire_descriptor};

    uint32_t release_handle = 0U;
    int32_t release_descriptor = mirage::kInvalidFd;
    const md_result_t release_result = create_release_syncobj(
        exporter->drm_fd.get(), &release_handle, &release_descriptor);
    if (release_result != MD_OK) {
        md_vk_exporter_cancel_frame(exporter, buffer_index);
        return release_result;
    }
    mirage::UniqueFd release_fd{release_descriptor};

    md_vk_export_slot& slot = exporter->slots[buffer_index];
    slot.release_handle = release_handle;
    slot.acquired = false;
    slot.busy = true;
    *out_acquire_sync_fd = acquire_fd.release();
    *out_release_syncobj_fd = release_fd.release();
    return MD_OK;
}


/*
 * Rolls a slot back to idle when frame submission failed after
 * export_frame/copy_frame, destroying the release handle so the slot can be
 * re-acquired on the next frame.
 */
extern "C" void md_vk_exporter_cancel_frame(md_vk_exporter_t* const exporter,
                                              const uint32_t buffer_index) {
    if (exporter == nullptr || buffer_index >= MIRAGE_DISPLAY_MAX_BUFFERS) {
        return;
    }
    destroy_release_handle(exporter, buffer_index);
}
