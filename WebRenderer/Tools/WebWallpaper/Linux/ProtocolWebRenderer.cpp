#include "ProtocolWebRenderer.h"

#include <mirage_display.h>
#include <mirage_display_producer.h>
#include <mirage_display_vulkan_export.h>

#include <QMetaObject>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <limits>
#include <poll.h>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t Fourcc(char a, char b, char c, char d) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8u) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16u) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24u);
}
constexpr std::uint32_t kXrgb8888 = Fourcc('X', 'B', '2', '4');
// mirage-display transports Linux input-event button codes. Mapping them at
// this boundary mirrors SceneWallpaper and keeps the renderer Qt-typed.
constexpr std::uint32_t kButtonLeft = 0x110u;
constexpr std::uint32_t kButtonRight = 0x111u;
constexpr std::uint32_t kButtonMiddle = 0x112u;
constexpr std::uint32_t kButtonSide = 0x113u;
constexpr std::uint32_t kButtonExtra = 0x114u;

std::uint32_t MemoryType(VkPhysicalDevice device, std::uint32_t bits,
                         VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties properties {};
    vkGetPhysicalDeviceMemoryProperties(device, &properties);
    for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((bits & (UINT32_C(1) << i)) != 0 &&
            (properties.memoryTypes[i].propertyFlags & required) == required) return i;
    }
    return UINT32_MAX;
}

int RenderNode(std::uint32_t major, std::uint32_t minor) {
    /* The broker selected this exact consumer node. Opening a different DRM
     * alias or scanning renderD values would permit cross-GPU DMA-BUF use. */
    if (major == 0U || minor < 128U || minor > 255U) return -1;
    char path[64] {};
    int written = std::snprintf(path, sizeof(path), "/dev/dri/renderD%u", minor);
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(path)) return -1;
    return ::open(path, O_RDWR | O_CLOEXEC);
}

} // namespace

class ProtocolWebRenderer::Impl {
public:
    explicit Impl(Config config) : m_config(std::move(config)) {
        // QtWebEngine starts Chromium GPU workers while its view is created.
        // Initialize the Vulkan loader first to avoid concurrent ICD setup.
        VkApplicationInfo app {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = nullptr,
            .pApplicationName = "WebWallpaper",
            .applicationVersion = 1,
            .pEngineName = nullptr,
            .engineVersion = 0,
            .apiVersion = VK_API_VERSION_1_1,
        };
        VkInstanceCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .pApplicationInfo = &app,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = 0,
            .ppEnabledExtensionNames = nullptr,
        };
        m_instanceReady = vkCreateInstance(&createInfo, nullptr, &m_instance) == VK_SUCCESS;
        if (!m_instanceReady) m_instanceError = QStringLiteral("vkCreateInstance failed");
    }
    ~Impl() { stop(); }

    bool start(QString* error) {
        if (m_config.socketPath.isEmpty() || m_config.outputId.isEmpty()) {
            if (error != nullptr) *error = QStringLiteral("display socket and output id are required");
            return false;
        }
        /* OUTPUT_CONFIG transports the consumer GPU identity. It must arrive
         * before Vulkan is created, otherwise a hybrid system can select the
         * wrong physical device and export unreadable DMA-BUFs. */
        if (!connectProducer()) {
            if (error != nullptr && error->isEmpty()) *error = QStringLiteral("cannot initialize Vulkan display producer");
            stop();
            return false;
        }
        m_running.store(true);
        m_ioThread = std::thread([this] { ioLoop(); });
        {
            // rebuildPool() snapshots the negotiated configuration under the
            // same mutex. End the wait lock first so startup cannot deadlock
            // itself before the initial buffer pool is offered.
            std::unique_lock lock(m_stateMutex);
            if (!m_stateCv.wait_for(lock, std::chrono::seconds(15), [this] { return m_configVersion != 0; })) {
                if (error != nullptr) *error = QStringLiteral("display producer did not receive output configuration");
                return false;
            }
        }
        if (!createVulkan(error)) {
            stop();
            return false;
        }
        md_producer_gpu_info_t gpu {};
        gpu.drm_render_major = m_drmMajor;
        gpu.drm_render_minor = m_drmMinor;
        std::memcpy(gpu.device_uuid, m_deviceUuid, sizeof(gpu.device_uuid));
        std::memcpy(gpu.driver_uuid, m_driverUuid, sizeof(gpu.driver_uuid));
        bool gpu_bound = false;
        {
            std::lock_guard lock(m_producerMutex);
            gpu_bound = m_producer != nullptr && md_producer_bind_gpu(m_producer, &gpu) == MD_OK;
        }
        if (!gpu_bound) {
            if (error != nullptr) *error = QStringLiteral("mirage-display rejected target GPU binding");
            stop();
            return false;
        }
        if (!rebuildPool(error)) {
            stop();
            return false;
        }
        return true;
    }

    void stop() {
        // stop() is called both from aboutToQuit and Impl destruction. Clear
        // every Vulkan handle immediately after destruction so the second
        // call remains a no-op instead of passing a stale fence to Vulkan.
        if (!m_running.exchange(false)) {
            if (m_ioThread.joinable()) m_ioThread.join();
        } else {
            std::lock_guard lock(m_producerMutex);
            if (m_producer != nullptr) md_producer_close(m_producer);
        }
        if (m_ioThread.joinable()) m_ioThread.join();
        if (m_device != VK_NULL_HANDLE) vkDeviceWaitIdle(m_device);
        if (m_exporter != nullptr) {
            md_vk_exporter_free(m_exporter);
            m_exporter = nullptr;
        }
        destroyUpload();
        if (m_device != VK_NULL_HANDLE) {
            if (m_fence != VK_NULL_HANDLE) {
                vkDestroyFence(m_device, m_fence, nullptr);
                m_fence = VK_NULL_HANDLE;
            }
            if (m_commandPool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(m_device, m_commandPool, nullptr);
                m_commandPool = VK_NULL_HANDLE;
            }
            vkDestroyDevice(m_device, nullptr);
            m_device = VK_NULL_HANDLE;
        }
        if (m_instance != VK_NULL_HANDLE) {
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
        }
        m_instanceReady = false;
        if (m_drmFd >= 0) ::close(m_drmFd);
        m_drmFd = -1;
        m_queue = VK_NULL_HANDLE;
        m_commandBuffer = VK_NULL_HANDLE;
        m_physicalDevice = VK_NULL_HANDLE;
        std::lock_guard lock(m_producerMutex);
        if (m_producer != nullptr) {
            md_producer_free(m_producer);
            m_producer = nullptr;
        }
    }

    void submitFrame(const QImage& image) {
        if (m_device == VK_NULL_HANDLE || m_exporter == nullptr || image.isNull()) return;
        std::lock_guard lock(m_renderMutex);
        std::uint64_t configVersion = 0;
        {
            std::lock_guard stateLock(m_stateMutex);
            configVersion = m_configVersion;
        }
        if (configVersion != m_appliedConfigVersion) {
            QString error;
            if (!rebuildPool(&error)) {
                emitFailure(error.isEmpty() ? QStringLiteral("cannot rebuild display buffer pool") : error);
                return;
            }
        }
        if (m_uploadWidth == 0 || m_uploadHeight == 0) return;
        const QImage scaled = image.scaled(static_cast<int>(m_uploadWidth), static_cast<int>(m_uploadHeight),
                                           Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                  .convertToFormat(QImage::Format_RGBA8888);
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(m_uploadWidth) * m_uploadHeight * 4u;
        std::memcpy(m_stagingMap, scaled.constBits(), static_cast<std::size_t>(bytes));
        if (vkResetCommandPool(m_device, m_commandPool, 0) != VK_SUCCESS ||
            vkResetFences(m_device, 1, &m_fence) != VK_SUCCESS) return;
        VkCommandBufferBeginInfo begin {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(m_commandBuffer, &begin) != VK_SUCCESS) return;
        VkImageMemoryBarrier toTransfer {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toTransfer.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toTransfer.oldLayout = m_uploadInitialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = m_uploadImage;
        toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(m_commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);
        VkBufferImageCopy region {};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {m_uploadWidth, m_uploadHeight, 1};
        vkCmdCopyBufferToImage(m_commandBuffer, m_stagingBuffer, m_uploadImage,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        VkImageMemoryBarrier toGeneral = toTransfer;
        toGeneral.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toGeneral.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        toGeneral.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        vkCmdPipelineBarrier(m_commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &toGeneral);
        if (vkEndCommandBuffer(m_commandBuffer) != VK_SUCCESS) return;
        VkSubmitInfo submit {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &m_commandBuffer;
        if (vkQueueSubmit(m_queue, 1, &submit, m_fence) != VK_SUCCESS ||
            vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) return;
        m_uploadInitialized = true;
        std::uint32_t index = 0;
        if (md_vk_exporter_acquire(m_exporter, &index) != MD_OK) return;
        int acquireFd = -1;
        int releaseFd = -1;
        if (md_vk_exporter_copy_frame(m_exporter, index, m_uploadImage, VK_IMAGE_LAYOUT_GENERAL,
                                      m_uploadWidth, m_uploadHeight, &acquireFd, &releaseFd) != MD_OK) {
            md_vk_exporter_cancel_frame(m_exporter, index);
            return;
        }
        std::lock_guard producerLock(m_producerMutex);
        if (m_producer == nullptr || md_producer_submit_frame(m_producer, m_generation, index,
                                                               ++m_sequence, acquireFd, releaseFd) != MD_OK) {
            md_vk_exporter_cancel_frame(m_exporter, index);
        }
    }

private:
    bool connectProducer() {
        md_producer_callbacks_t callbacks {
            .on_connected = &OnConnected, .on_output_config = &OnOutputConfig,
            .on_retire_buffers = &OnRetire, .on_pointer_enter = &OnPointerEnter,
            .on_pointer_leave = &OnPointerLeave, .on_pointer_motion = &OnPointerMotion,
            .on_pointer_button = &OnPointerButton, .on_pointer_axis = &OnPointerAxis,
            .on_window_state = nullptr, .on_disconnected = &OnDisconnected, .user_data = this,
        };
        m_producer = md_producer_new(&callbacks);
        if (m_producer == nullptr) return false;
        const md_format_cap_t formats[] = {{kXrgb8888, 1, 0}};
        const QByteArray outputId = m_config.outputId.toUtf8();
        md_producer_info_t info {
            .stable_output_id = outputId.constData(), .kind = "web",
            .drm_render_major = m_drmMajor, .drm_render_minor = m_drmMinor, .device_uuid = {}, .driver_uuid = {},
            .formats = formats, .format_count = 1,
        };
        std::memcpy(info.device_uuid, m_deviceUuid, sizeof(info.device_uuid));
        std::memcpy(info.driver_uuid, m_driverUuid, sizeof(info.driver_uuid));
        const QByteArray socket = m_config.socketPath.toUtf8();
        return md_producer_connect(m_producer, socket.constData(), "WebWallpaper", "0.1.0", &info, 3000) == MD_OK;
    }

    bool createVulkan(QString* error) {
        md_producer_config_t target {};
        {
            std::lock_guard lock(m_stateMutex);
            target = m_outputConfig;
        }
        if ((target.target_gpu_flags & MD_TARGET_GPU_RENDER_NODE_VALID) == 0U ||
            target.target_drm_render_major == 0U || target.target_drm_render_minor < 128U ||
            target.target_drm_render_minor > 255U) {
            if (error != nullptr) *error = QStringLiteral("broker did not provide a valid consumer DRM render node");
            return false;
        }
        if (!m_instanceReady) {
            if (error != nullptr) *error = m_instanceError;
            return false;
        }
        std::uint32_t count = 0;
        const VkResult deviceQuery = vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
        if (deviceQuery != VK_SUCCESS || count == 0U) {
            if (error != nullptr) *error = QStringLiteral("no Vulkan physical devices");
            return false;
        }
        std::vector<VkPhysicalDevice> devices(count);
        const VkResult deviceRead = vkEnumeratePhysicalDevices(m_instance, &count, devices.data());
        if (deviceRead != VK_SUCCESS) {
            if (error != nullptr) *error = QStringLiteral("cannot enumerate Vulkan physical devices");
            return false;
        }
        const char* deviceExtensions[] = {
            VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME,
            VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
            VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
            VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
            VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME,
        };
        for (VkPhysicalDevice device : devices) {
            std::uint32_t deviceExtensionCount = 0;
            const VkResult extensionQuery =
                vkEnumerateDeviceExtensionProperties(device, nullptr, &deviceExtensionCount, nullptr);
            if (extensionQuery != VK_SUCCESS) {
                if (error != nullptr) *error = QStringLiteral("cannot enumerate Vulkan device extensions");
                return false;
            }
            std::vector<VkExtensionProperties> deviceProperties(deviceExtensionCount);
            const VkResult extensionRead = vkEnumerateDeviceExtensionProperties(
                device, nullptr, &deviceExtensionCount, deviceProperties.data());
            if (extensionRead != VK_SUCCESS) {
                if (error != nullptr) *error = QStringLiteral("cannot read Vulkan device extensions");
                return false;
            }
            bool extensionsSupported = true;
            for (const char* required : deviceExtensions) {
                bool found = false;
                for (const VkExtensionProperties& property : deviceProperties) {
                    if (std::strcmp(property.extensionName, required) == 0) { found = true; break; }
                }
                if (!found) { extensionsSupported = false; break; }
            }
            if (!extensionsSupported) continue;

            VkPhysicalDeviceDrmPropertiesEXT drm {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT};
            VkPhysicalDeviceIDProperties id {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
            id.pNext = &drm;
            VkPhysicalDeviceProperties2 properties {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
            properties.pNext = &id;
            vkGetPhysicalDeviceProperties2(device, &properties);
            if (drm.hasRender != VK_TRUE || drm.renderMajor < 0 || drm.renderMinor < 0 ||
                static_cast<std::uint32_t>(drm.renderMajor) != target.target_drm_render_major ||
                static_cast<std::uint32_t>(drm.renderMinor) != target.target_drm_render_minor ||
                ((target.target_gpu_flags & MD_TARGET_GPU_DEVICE_UUID_VALID) != 0U &&
                 std::memcmp(id.deviceUUID, target.target_device_uuid, sizeof(id.deviceUUID)) != 0) ||
                ((target.target_gpu_flags & MD_TARGET_GPU_DRIVER_UUID_VALID) != 0U &&
                 std::memcmp(id.driverUUID, target.target_driver_uuid, sizeof(id.driverUUID)) != 0)) {
                continue;
            }
            std::uint32_t families = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &families, nullptr);
            std::vector<VkQueueFamilyProperties> props(families);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &families, props.data());
            for (std::uint32_t i = 0; i < families; ++i) {
                if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                    m_physicalDevice = device;
                    m_queueFamily = i;
                    m_drmMajor = static_cast<std::uint32_t>(drm.renderMajor);
                    m_drmMinor = static_cast<std::uint32_t>(drm.renderMinor);
                    std::memcpy(m_deviceUuid, id.deviceUUID, sizeof(m_deviceUuid));
                    std::memcpy(m_driverUuid, id.driverUUID, sizeof(m_driverUuid));
                    break;
                }
            }
            if (m_physicalDevice != VK_NULL_HANDLE) break;
        }
        if (m_physicalDevice == VK_NULL_HANDLE) {
            if (error != nullptr) *error = QStringLiteral("no Vulkan DMA-BUF exporter matches consumer renderD%1").arg(target.target_drm_render_minor);
            return false;
        }
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = m_queueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        deviceInfo.enabledExtensionCount = static_cast<std::uint32_t>(std::size(deviceExtensions));
        deviceInfo.ppEnabledExtensionNames = deviceExtensions;
        if (vkCreateDevice(m_physicalDevice, &deviceInfo, nullptr, &m_device) != VK_SUCCESS) return false;
        vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
        m_drmFd = RenderNode(m_drmMajor, m_drmMinor);
        if (m_drmFd < 0) {
            if (error != nullptr) *error = QStringLiteral("cannot open consumer renderD%1").arg(m_drmMinor);
            return false;
        }
        md_vk_export_context_t context {m_instance, m_physicalDevice, m_device, m_queue,
                                        m_queueFamily, m_drmFd, m_drmMajor, m_drmMinor};
        m_exporter = md_vk_exporter_new(&context);
        if (m_exporter == nullptr) return false;
        VkCommandPoolCreateInfo pool {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = m_queueFamily;
        if (vkCreateCommandPool(m_device, &pool, nullptr, &m_commandPool) != VK_SUCCESS) return false;
        VkCommandBufferAllocateInfo alloc {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool = m_commandPool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(m_device, &alloc, &m_commandBuffer) != VK_SUCCESS) return false;
        VkFenceCreateInfo fence {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        return vkCreateFence(m_device, &fence, nullptr, &m_fence) == VK_SUCCESS;
    }

    bool rebuildPool(QString* error) {
        md_producer_config_t config {};
        { std::lock_guard lock(m_stateMutex); config = m_outputConfig; }
        if (config.physical_width == 0 || config.physical_height == 0) {
            if (error != nullptr) *error = QStringLiteral("invalid output configuration");
            return false;
        }
        const md_vk_export_pool_info_t info {m_nextGeneration++, 3, config.physical_width,
                                             config.physical_height, config.fourcc != 0 ? config.fourcc : kXrgb8888,
                                             config.plane_count != 0 ? config.plane_count : 1, config.modifier};
        if (md_vk_exporter_create_pool(m_exporter, &info) != MD_OK) return false;
        const md_buffer_pool_t* pool = md_vk_exporter_pool(m_exporter);
        md_display_config_t display {info.generation, {0, 0, static_cast<float>(info.width), static_cast<float>(info.height)},
                                     {0, 0, static_cast<float>(info.width), static_cast<float>(info.height)},
                                     MD_TRANSFORM_NORMAL, {0, 0, 0, 1}};
        std::lock_guard lock(m_producerMutex);
        if (md_producer_offer_buffers(m_producer, pool) != MD_OK || md_producer_set_config(m_producer, &display) != MD_OK) return false;
        m_generation = info.generation;
        m_uploadWidth = info.width;
        m_uploadHeight = info.height;
        if (!createUpload(info.width, info.height)) return false;
        if (m_config.outputSizeChanged) m_config.outputSizeChanged(info.width, info.height);
        {
            std::lock_guard lock(m_stateMutex);
            m_appliedConfigVersion = m_configVersion;
        }
        return true;
    }

    bool createUpload(std::uint32_t width, std::uint32_t height) {
        destroyUpload();
        VkImageCreateInfo image {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = VK_FORMAT_R8G8B8A8_UNORM;
        image.extent = {width, height, 1};
        image.mipLevels = 1; image.arrayLayers = 1; image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(m_device, &image, nullptr, &m_uploadImage) != VK_SUCCESS) return false;
        VkMemoryRequirements requirements {};
        vkGetImageMemoryRequirements(m_device, m_uploadImage, &requirements);
        const std::uint32_t imageType = MemoryType(m_physicalDevice, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (imageType == UINT32_MAX) return false;
        VkMemoryAllocateInfo allocate {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, requirements.size, imageType};
        if (vkAllocateMemory(m_device, &allocate, nullptr, &m_uploadMemory) != VK_SUCCESS ||
            vkBindImageMemory(m_device, m_uploadImage, m_uploadMemory, 0) != VK_SUCCESS) return false;
        VkBufferCreateInfo buffer {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer.size = static_cast<VkDeviceSize>(width) * height * 4u;
        buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_device, &buffer, nullptr, &m_stagingBuffer) != VK_SUCCESS) return false;
        vkGetBufferMemoryRequirements(m_device, m_stagingBuffer, &requirements);
        const std::uint32_t stagingType = MemoryType(m_physicalDevice, requirements.memoryTypeBits,
                                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (stagingType == UINT32_MAX) return false;
        VkMemoryAllocateInfo staging {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, requirements.size, stagingType};
        return vkAllocateMemory(m_device, &staging, nullptr, &m_stagingMemory) == VK_SUCCESS &&
               vkBindBufferMemory(m_device, m_stagingBuffer, m_stagingMemory, 0) == VK_SUCCESS &&
               vkMapMemory(m_device, m_stagingMemory, 0, buffer.size, 0, &m_stagingMap) == VK_SUCCESS &&
               (m_uploadInitialized = false, true);
    }

    void destroyUpload() {
        if (m_device == VK_NULL_HANDLE) return;
        if (m_stagingMap != nullptr) vkUnmapMemory(m_device, m_stagingMemory);
        if (m_stagingBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, m_stagingBuffer, nullptr);
        if (m_stagingMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, m_stagingMemory, nullptr);
        if (m_uploadImage != VK_NULL_HANDLE) vkDestroyImage(m_device, m_uploadImage, nullptr);
        if (m_uploadMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, m_uploadMemory, nullptr);
        m_stagingMap = nullptr; m_stagingBuffer = VK_NULL_HANDLE; m_stagingMemory = VK_NULL_HANDLE;
        m_uploadImage = VK_NULL_HANDLE; m_uploadMemory = VK_NULL_HANDLE;
    }

    void ioLoop() {
        while (m_running.load()) {
            int fd = -1; bool writable = false;
            { std::lock_guard lock(m_producerMutex); if (m_producer != nullptr) { fd = md_producer_get_fd(m_producer); writable = md_producer_wants_writable(m_producer); } }
            if (fd < 0) break;
            pollfd descriptor {fd, static_cast<short>(POLLIN | (writable ? POLLOUT : 0)), 0};
            if (poll(&descriptor, 1, 100) <= 0) continue;
            std::lock_guard lock(m_producerMutex);
            if ((descriptor.revents & POLLIN) != 0) md_producer_dispatch(m_producer);
            if ((descriptor.revents & POLLOUT) != 0) md_producer_handle_writable(m_producer);
        }
    }

    void emitFailure(const QString& message) {
        std::fprintf(stderr, "WebWallpaper: %s\n", qPrintable(message));
    }

    // SceneWallpaper and SceneViewer expose pointer positions to their
    // renderers as normalized coordinates. Applying the same protocol mapping
    // here keeps Qt logical pixels independent from KDE's output scale.
    bool mapPointerPosition(float physicalX, float physicalY, float& x, float& y) {
        md_producer_config_t config {};
        {
            std::lock_guard lock(m_stateMutex);
            config = m_outputConfig;
        }
        if (config.physical_width == 0U || config.physical_height == 0U) return false;
        x = std::clamp(physicalX / static_cast<float>(config.physical_width), 0.0f, 1.0f);
        y = std::clamp(physicalY / static_cast<float>(config.physical_height), 0.0f, 1.0f);
        return true;
    }

    static void OnConnected(void*, std::uint64_t, std::uint64_t) {}
    static void OnOutputConfig(void* opaque, const md_producer_config_t* config) {
        auto* self = static_cast<Impl*>(opaque); if (config == nullptr) return;
        { std::lock_guard lock(self->m_stateMutex); self->m_outputConfig = *config; ++self->m_configVersion; }
        self->m_stateCv.notify_all();
    }
    static void OnRetire(void*, std::uint64_t) {}
    static void OnPointerEnter(void* opaque, const md_pointer_enter_t* event) {
        auto* self = static_cast<Impl*>(opaque);
        if (event == nullptr || !self->m_config.pointerEnter) return;
        float x;
        float y;
        if (!self->mapPointerPosition(event->x, event->y, x, y)) return;
        self->m_config.pointerEnter(x, y);
    }
    static void OnPointerLeave(void* opaque, std::uint64_t) {
        auto* self = static_cast<Impl*>(opaque);
        if (self->m_config.pointerLeave) self->m_config.pointerLeave();
    }
    static void OnPointerMotion(void* opaque, const md_pointer_motion_t* event) {
        auto* self = static_cast<Impl*>(opaque);
        if (event == nullptr || !self->m_config.pointerMotion) return;
        float x;
        float y;
        if (!self->mapPointerPosition(event->x, event->y, x, y)) return;
        self->m_config.pointerMotion(x, y);
    }
    static void OnPointerButton(void* opaque, const md_pointer_button_t* event) {
        auto* self = static_cast<Impl*>(opaque);
        if (event == nullptr || !self->m_config.pointerButton) return;
        Qt::MouseButton button;
        switch (event->button) {
        case kButtonLeft: button = Qt::LeftButton; break;
        case kButtonRight: button = Qt::RightButton; break;
        case kButtonMiddle: button = Qt::MiddleButton; break;
        case kButtonSide: button = Qt::BackButton; break;
        case kButtonExtra: button = Qt::ForwardButton; break;
        default: return;
        }
        float x;
        float y;
        if (!self->mapPointerPosition(event->x, event->y, x, y)) return;
        self->m_config.pointerButton(x, y, button,
                                     event->state == MD_BUTTON_PRESSED);
    }
    static void OnPointerAxis(void* opaque, const md_pointer_axis_t* event) {
        auto* self = static_cast<Impl*>(opaque);
        if (event == nullptr || !self->m_config.pointerAxis) return;
        float x;
        float y;
        if (!self->mapPointerPosition(event->x, event->y, x, y)) return;
        const bool pixelBased = event->source != MD_AXIS_WHEEL;
        self->m_config.pointerAxis(x, y, event->delta_x, event->delta_y,
                                   pixelBased);
    }
    static void OnDisconnected(void*, md_result_t, const char*) {}

    Config m_config;
    std::mutex m_producerMutex;
    md_producer_t* m_producer = nullptr;
    std::atomic_bool m_running {false};
    std::thread m_ioThread;
    std::mutex m_stateMutex;
    std::condition_variable m_stateCv;
    md_producer_config_t m_outputConfig {};
    std::uint64_t m_configVersion = 0;
    std::uint64_t m_appliedConfigVersion = 0;
    std::uint64_t m_nextGeneration = 1;
    std::uint64_t m_generation = 0;
    std::uint64_t m_sequence = 0;
    std::mutex m_renderMutex;
    VkInstance m_instance = VK_NULL_HANDLE;
    bool m_instanceReady = false;
    QString m_instanceError;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    std::uint32_t m_queueFamily = 0;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
    md_vk_exporter_t* m_exporter = nullptr;
    VkImage m_uploadImage = VK_NULL_HANDLE;
    VkDeviceMemory m_uploadMemory = VK_NULL_HANDLE;
    VkBuffer m_stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_stagingMemory = VK_NULL_HANDLE;
    void* m_stagingMap = nullptr;
    std::uint32_t m_uploadWidth = 0;
    std::uint32_t m_uploadHeight = 0;
    int m_drmFd = -1;
    std::uint32_t m_drmMajor = 0;
    std::uint32_t m_drmMinor = 0;
    std::uint8_t m_deviceUuid[16] {};
    std::uint8_t m_driverUuid[16] {};
    bool m_uploadInitialized = false;
};

ProtocolWebRenderer::ProtocolWebRenderer(Config config, QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(std::move(config))) {}
ProtocolWebRenderer::~ProtocolWebRenderer() = default;
bool ProtocolWebRenderer::start(QString* error) { return m_impl->start(error); }
void ProtocolWebRenderer::stop() { m_impl->stop(); }
void ProtocolWebRenderer::submitFrame(const QImage& image) { m_impl->submitFrame(image); }
