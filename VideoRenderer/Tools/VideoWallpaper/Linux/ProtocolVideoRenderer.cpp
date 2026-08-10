#include "ProtocolVideoRenderer.h"

#include <mirage_display.h>
#include <mirage_display_producer.h>
#include <mirage_display_vulkan_export.h>

#include <QFileInfo>
#include <QUrl>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#endif

extern "C" {
#include <libswscale/swscale.h>
}

#include <mpv/client.h>
#include <mpv/render.h>

namespace {

constexpr std::uint32_t DrmFourcc(char a, char b, char c, char d) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8u) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16u) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24u);
}

constexpr std::uint32_t kDrmXbgr8888 = DrmFourcc('X', 'B', '2', '4');
constexpr std::uint32_t kDrmAbgr8888 = DrmFourcc('A', 'B', '2', '4');

constexpr std::uint32_t kExportBufferCount = 3;

const char* FillModeName(VRVideoFillMode mode) {
    switch (mode) {
    case VRVideoFillModeContain: return "contain";
    case VRVideoFillModeStretch: return "stretch";
    case VRVideoFillModeCover:
    default: return "cover";
    }
}

int OpenRenderNode(std::uint32_t major, std::uint32_t minor) {
    char path[64];
    if (minor >= 128u && minor <= 255u) {
        int written = std::snprintf(path, sizeof(path), "/dev/dri/renderD%u", minor);
        if (written > 0 && static_cast<std::size_t>(written) < sizeof(path)) {
            int fd = ::open(path, O_RDWR | O_CLOEXEC);
            if (fd >= 0) return fd;
        }
    }
    if (major != 0 || minor != 0) {
        int written = std::snprintf(path, sizeof(path), "/dev/char/%u:%u", major, minor);
        if (written > 0 && static_cast<std::size_t>(written) < sizeof(path)) {
            int fd = ::open(path, O_RDWR | O_CLOEXEC);
            if (fd >= 0) return fd;
        }
    }
    return -1;
}

std::uint32_t ChooseMemoryType(VkPhysicalDevice physical_device, std::uint32_t type_bits,
                               VkMemoryPropertyFlags preferred) {
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);
    for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((type_bits & (UINT32_C(1) << i)) != 0 &&
            (properties.memoryTypes[i].propertyFlags & preferred) == preferred) {
            return i;
        }
    }
    for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((type_bits & (UINT32_C(1) << i)) != 0) return i;
    }
    return UINT32_MAX;
}

} // namespace

class ProtocolHost {
public:
    ProtocolHost(std::string socket_path, std::string output_id)
        : m_socket_path(std::move(socket_path)), m_output_id(std::move(output_id)) {}

    ~ProtocolHost() { stop(); }

    ProtocolHost(const ProtocolHost&) = delete;
    ProtocolHost& operator=(const ProtocolHost&) = delete;

    void setGpuInfo(std::uint32_t major, std::uint32_t minor,
                    const std::uint8_t* device_uuid, const std::uint8_t* driver_uuid) {
        std::lock_guard lock(m_producer_mutex);
        m_drm_major = major;
        m_drm_minor = minor;
        if (device_uuid != nullptr) std::memcpy(m_device_uuid, device_uuid, 16);
        if (driver_uuid != nullptr) std::memcpy(m_driver_uuid, driver_uuid, 16);
    }

    bool start() {
        if (m_socket_path.empty() || m_output_id.empty()) return false;
        {
            std::lock_guard lock(m_producer_mutex);
            if (!connectProducerLocked()) return false;
        }
        runIo();
        std::unique_lock lock(m_state_mutex);
        return m_state_cv.wait_for(lock, std::chrono::seconds(15), [this] {
            return m_config_version != 0;
        }) && m_config_version != 0;
    }

    void runIo() {
        if (m_running.exchange(true)) return;
        m_io_thread = std::thread([this] { ioLoop(); });
    }

    void stop() {
        const bool was_running = m_running.exchange(false);
        m_state_cv.notify_all();
        if (was_running) {
            std::lock_guard lock(m_producer_mutex);
            if (m_producer != nullptr) md_producer_close(m_producer);
        }
        if (m_io_thread.joinable()) m_io_thread.join();
        std::lock_guard lock(m_producer_mutex);
        if (m_producer != nullptr) {
            md_producer_free(m_producer);
            m_producer = nullptr;
        }
    }

    bool snapshotConfig(std::uint64_t last_version, std::uint64_t last_epoch,
                        md_producer_config_t& config, std::uint64_t& version,
                        std::uint64_t& epoch) const {
        std::lock_guard lock(m_state_mutex);
        if (m_config_version == 0 ||
            (m_config_version == last_version && m_connection_epoch == last_epoch)) {
            return false;
        }
        config = m_config;
        version = m_config_version;
        epoch = m_connection_epoch;
        return true;
    }

    bool currentConfig(md_producer_config_t& config, std::uint64_t& version,
                       std::uint64_t& epoch) const {
        return snapshotConfig(0, 0, config, version, epoch);
    }

    std::uint64_t takeRetireGeneration() {
        std::lock_guard lock(m_state_mutex);
        return std::exchange(m_retire_generation, UINT64_C(0));
    }

    std::uint64_t nextGeneration() { return m_next_generation.fetch_add(1); }

    int offerPool(const md_buffer_pool_t* pool) {
        if (pool == nullptr) return MD_ERR_INVALID;
        std::lock_guard lock(m_producer_mutex);
        if (m_producer == nullptr ||
            md_producer_connection_state(m_producer) != MD_CONNECTION_READY) {
            return MD_ERR_DISCONNECTED;
        }
        const int result = md_producer_offer_buffers(m_producer, pool);
        if (result != MD_OK) return result;
        // Advertise the pool as covering the whole output; the desktop
        // environment adapter scales it to the actual display geometry.
        md_display_config_t display_config {
            .generation = pool->generation,
            .source = {0.0f, 0.0f, static_cast<float>(pool->width),
                       static_cast<float>(pool->height)},
            .destination = {0.0f, 0.0f, static_cast<float>(pool->width),
                            static_cast<float>(pool->height)},
            .transform = MD_TRANSFORM_NORMAL,
            .clear_color = {0.0f, 0.0f, 0.0f, 1.0f},
        };
        return md_producer_set_config(m_producer, &display_config);
    }

    int submitFrame(std::uint64_t generation, std::uint32_t index, std::uint64_t sequence,
                    int acquire_fd, int release_fd) {
        std::lock_guard lock(m_producer_mutex);
        if (m_producer == nullptr ||
            md_producer_connection_state(m_producer) != MD_CONNECTION_READY) {
            if (acquire_fd >= 0) close(acquire_fd);
            if (release_fd >= 0) close(release_fd);
            return MD_ERR_DISCONNECTED;
        }
        return md_producer_submit_frame(m_producer, generation, index, sequence,
                                        acquire_fd, release_fd);
    }

    void retireDone(std::uint64_t generation) {
        std::lock_guard lock(m_producer_mutex);
        if (m_producer != nullptr &&
            md_producer_connection_state(m_producer) == MD_CONNECTION_READY) {
            (void)md_producer_retire_done(m_producer, generation);
        }
    }

private:
    static void OnConnected(void* opaque, std::uint64_t, std::uint64_t) {
        auto* self = static_cast<ProtocolHost*>(opaque);
        {
            std::lock_guard lock(self->m_state_mutex);
            ++self->m_connection_epoch;
            self->m_retire_generation = 0;
        }
        self->m_state_cv.notify_all();
    }

    static void OnOutputConfig(void* opaque, const md_producer_config_t* config) {
        auto* self = static_cast<ProtocolHost*>(opaque);
        if (config == nullptr) return;
        {
            std::lock_guard lock(self->m_state_mutex);
            self->m_config = *config;
            ++self->m_config_version;
        }
        self->m_state_cv.notify_all();
    }

    static void OnRetire(void* opaque, std::uint64_t generation) {
        auto* self = static_cast<ProtocolHost*>(opaque);
        std::lock_guard lock(self->m_state_mutex);
        self->m_retire_generation = generation;
    }

    static void OnPointerEnter(void*, const md_pointer_enter_t*) {}
    static void OnPointerLeave(void*, std::uint64_t) {}
    static void OnPointerMotion(void*, const md_pointer_motion_t*) {}
    static void OnPointerButton(void*, const md_pointer_button_t*) {}
    static void OnPointerAxis(void*, const md_pointer_axis_t*) {}

    static void OnDisconnected(void* opaque, md_result_t, const char*) {
        auto* self = static_cast<ProtocolHost*>(opaque);
        self->m_state_cv.notify_all();
    }

    bool connectProducerLocked() {
        if (m_producer != nullptr) md_producer_free(m_producer);
        md_producer_callbacks_t callbacks {
            .on_connected = OnConnected,
            .on_output_config = OnOutputConfig,
            .on_retire_buffers = OnRetire,
            .on_pointer_enter = OnPointerEnter,
            .on_pointer_leave = OnPointerLeave,
            .on_pointer_motion = OnPointerMotion,
            .on_pointer_button = OnPointerButton,
            .on_pointer_axis = OnPointerAxis,
            .on_disconnected = OnDisconnected,
            .user_data = this,
        };
        m_producer = md_producer_new(&callbacks);
        if (m_producer == nullptr) return false;
        const md_format_cap_t formats[] = {
            {.fourcc = kDrmXbgr8888, .plane_count = 1, .modifier = 0},
            {.fourcc = kDrmAbgr8888, .plane_count = 1, .modifier = 0},
        };
        md_producer_info_t info {
            .stable_output_id = m_output_id.c_str(),
            .kind = "video",
            .drm_render_major = m_drm_major,
            .drm_render_minor = m_drm_minor,
            .device_uuid = {},
            .driver_uuid = {},
            .formats = formats,
            .format_count = static_cast<std::uint32_t>(std::size(formats)),
        };
        std::memcpy(info.device_uuid, m_device_uuid, sizeof(info.device_uuid));
        std::memcpy(info.driver_uuid, m_driver_uuid, sizeof(info.driver_uuid));
        const int result = md_producer_connect(m_producer, m_socket_path.c_str(),
                                               "VideoWallpaper", "0.1.0", &info, 3000);
        if (result == MD_OK) return true;
        md_producer_free(m_producer);
        m_producer = nullptr;
        return false;
    }

    void ioLoop() {
        while (m_running.load()) {
            int fd = -1;
            bool wants_write = false;
            {
                std::lock_guard lock(m_producer_mutex);
                if (m_producer != nullptr) {
                    fd = md_producer_get_fd(m_producer);
                    wants_write = md_producer_wants_writable(m_producer);
                }
            }
            if (fd < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                std::lock_guard lock(m_producer_mutex);
                if (m_running.load()) (void)connectProducerLocked();
                continue;
            }
            pollfd descriptor {
                .fd = fd,
                .events = static_cast<short>(POLLIN | (wants_write ? POLLOUT : 0)),
                .revents = 0,
            };
            const int ready = poll(&descriptor, 1, 100);
            if (ready < 0 && errno == EINTR) continue;
            bool reconnect = ready < 0 ||
                             (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;
            if (!reconnect && ready > 0) {
                std::lock_guard lock(m_producer_mutex);
                if (m_producer == nullptr) continue;
                if ((descriptor.revents & POLLIN) != 0 &&
                    md_producer_dispatch(m_producer) < 0) {
                    reconnect = true;
                }
                if (!reconnect && (descriptor.revents & POLLOUT) != 0 &&
                    md_producer_handle_writable(m_producer) < 0) {
                    reconnect = true;
                }
            }
            if (reconnect && m_running.load()) {
                std::lock_guard lock(m_producer_mutex);
                if (m_producer != nullptr) {
                    md_producer_free(m_producer);
                    m_producer = nullptr;
                }
            }
        }
    }

    std::string m_socket_path;
    std::string m_output_id;

    mutable std::mutex m_state_mutex;
    std::condition_variable m_state_cv;
    md_producer_config_t m_config {};
    std::uint64_t m_config_version { 0 };
    std::uint64_t m_connection_epoch { 0 };
    std::uint64_t m_retire_generation { 0 };
    std::atomic_uint64_t m_next_generation { 1 };

    std::mutex m_producer_mutex;
    md_producer_t* m_producer { nullptr };
    std::uint32_t m_drm_major { 0 };
    std::uint32_t m_drm_minor { 0 };
    std::uint8_t m_device_uuid[16] { 0 };
    std::uint8_t m_driver_uuid[16] { 0 };
    std::atomic_bool m_running { false };
    std::thread m_io_thread;
};

class VRProtocolVideoRenderer::Impl : public QObject {
public:
    explicit Impl(Config config) : m_config(std::move(config)) {}

    ~Impl() { stop(); }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    bool start(QString* error) {
        if (m_config.socketPath.isEmpty() || m_config.outputId.isEmpty() ||
            m_config.videoPath.isEmpty()) {
            setError(error, "protocol renderer requires socket, output id and video path");
            return false;
        }
        const QFileInfo info(m_config.videoPath);
        if (!info.exists() || !info.isFile()) {
            setError(error, QStringLiteral("video file not found: %1").arg(m_config.videoPath));
            return false;
        }

        m_host = std::make_unique<ProtocolHost>(m_config.socketPath.toStdString(),
                                                m_config.outputId.toStdString());
        if (!m_host->start()) {
            setError(error,
                     "cannot connect to mirage-display broker or receive output configuration");
            return false;
        }

        if (!createVulkan(error)) return false;
        if (!rebuildPool()) {
            setError(error, "cannot create export pool for the negotiated output configuration");
            return false;
        }

        m_running.store(true);
        m_render_thread = std::thread([this] { renderLoop(); });
        return true;
    }

    void stop() {
        if (m_stopped.exchange(true)) return;
        m_running.store(false);
        {
            std::lock_guard lock(m_control_mutex);
            if (m_mpv != nullptr) mpv_wakeup(m_mpv);
        }
        m_control_cv.notify_all();
        if (m_render_thread.joinable()) m_render_thread.join();
        if (m_host != nullptr) m_host->stop();
        destroyUploadResources();
        if (m_exporter != nullptr) {
            md_vk_exporter_free(m_exporter);
            m_exporter = nullptr;
        }
        destroyVulkan();
        m_host.reset();
    }

    void play() {
        {
            std::lock_guard lock(m_control_mutex);
            m_user_paused = false;
            if (m_eof.load()) {
                m_restart_requested = true;
                m_eof.store(false);
                m_eof_notified.store(false);
            }
        }
        postMpvCommand([this] {
            // EOF 后重播：先 seek 到 0，再解除暂停。
            if (m_restart_requested.load()) {
                m_restart_requested.store(false);
                const char* seek[] = {"seek", "0", "absolute", nullptr};
                if (mpv_command(m_mpv, seek) < 0) {
                    std::fprintf(stderr, "VideoWallpaper: mpv seek failed\n");
                }
            }
            int paused = 0;
            if (mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &paused) < 0) {
                std::fprintf(stderr, "VideoWallpaper: mpv pause=0 failed\n");
            }
        });
    }

    void pause() {
        {
            std::lock_guard lock(m_control_mutex);
            m_user_paused = true;
        }
        postMpvCommand([this] {
            int paused = 1;
            if (mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &paused) < 0) {
                std::fprintf(stderr, "VideoWallpaper: mpv pause=1 failed\n");
            }
        });
    }

    void setVolume(float volume) {
        m_config.volume = VRClampVideoVolume(volume);
        m_volume.store(m_config.volume);
        postMpvCommand([this, volume] {
            // mpv volume 属性范围 0..100；协议约定 0..1。
            double mpv_volume = static_cast<double>(VRClampVideoVolume(volume)) * 100.0;
            if (mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &mpv_volume) < 0) {
                std::fprintf(stderr, "VideoWallpaper: mpv volume set failed\n");
            }
        });
    }

    void setMuted(bool muted) {
        m_config.muted = muted;
        m_muted.store(muted);
        postMpvCommand([this, muted] {
            int mute = muted ? 1 : 0;
            if (mpv_set_property(m_mpv, "mute", MPV_FORMAT_FLAG, &mute) < 0) {
                std::fprintf(stderr, "VideoWallpaper: mpv mute set failed\n");
            }
        });
    }

    void setFillMode(VRVideoFillMode fillMode) {
        m_fill_mode.store(fillMode);
    }

private:
    bool createVulkan(QString* error) {
        VkApplicationInfo app_info {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = nullptr,
            .pApplicationName = "VideoWallpaper",
            .applicationVersion = 1,
            .pEngineName = nullptr,
            .engineVersion = 0,
            .apiVersion = VK_API_VERSION_1_1,
        };
        const char* drm_ext = VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME;
        std::uint32_t instance_ext_count = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &instance_ext_count, nullptr);
        std::vector<VkExtensionProperties> instance_exts(instance_ext_count);
        vkEnumerateInstanceExtensionProperties(nullptr, &instance_ext_count,
                                               instance_exts.data());
        bool have_drm_ext = false;
        for (const auto& ext : instance_exts) {
            if (std::strcmp(ext.extensionName, drm_ext) == 0) { have_drm_ext = true; break; }
        }
        const char* enabled_instance_exts[1] = {drm_ext};
        VkInstanceCreateInfo instance_info {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .pApplicationInfo = &app_info,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = have_drm_ext ? 1u : 0u,
            .ppEnabledExtensionNames = have_drm_ext ? enabled_instance_exts : nullptr,
        };
        if (vkCreateInstance(&instance_info, nullptr, &m_instance) != VK_SUCCESS) {
            setError(error, "cannot create Vulkan instance");
            return false;
        }
        m_have_drm_ext = have_drm_ext;

        const char* required_exts[] = {
            VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
            VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
            VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
            VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME,
        };
        std::uint32_t device_count = 0;
        if (vkEnumeratePhysicalDevices(m_instance, &device_count, nullptr) != VK_SUCCESS ||
            device_count == 0) {
            setError(error, "no Vulkan physical devices");
            destroyVulkan();
            return false;
        }
        std::vector<VkPhysicalDevice> devices(device_count);
        if (vkEnumeratePhysicalDevices(m_instance, &device_count, devices.data()) != VK_SUCCESS) {
            setError(error, "cannot enumerate Vulkan physical devices");
            destroyVulkan();
            return false;
        }
        for (VkPhysicalDevice device : devices) {
            std::uint32_t ext_count = 0;
            vkEnumerateDeviceExtensionProperties(device, nullptr, &ext_count, nullptr);
            std::vector<VkExtensionProperties> exts(ext_count);
            vkEnumerateDeviceExtensionProperties(device, nullptr, &ext_count, exts.data());
            bool ok = true;
            for (const char* required : required_exts) {
                bool found = false;
                for (const auto& ext : exts) {
                    if (std::strcmp(ext.extensionName, required) == 0) { found = true; break; }
                }
                if (!found) { ok = false; break; }
            }
            if (!ok) continue;
            std::uint32_t family_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);
            std::vector<VkQueueFamilyProperties> families(family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, families.data());
            std::uint32_t graphics_family = UINT32_MAX;
            for (std::uint32_t i = 0; i < family_count; ++i) {
                if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                    graphics_family = i;
                    break;
                }
            }
            if (graphics_family == UINT32_MAX) continue;
            m_physical_device = device;
            m_queue_family = graphics_family;
            break;
        }
        if (m_physical_device == VK_NULL_HANDLE) {
            setError(error, "no Vulkan device supports the DMA-BUF export extension set");
            destroyVulkan();
            return false;
        }
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue_info {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = m_queue_family,
            .queueCount = 1,
            .pQueuePriorities = &priority,
        };
        VkDeviceCreateInfo device_info {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queue_info,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = static_cast<std::uint32_t>(std::size(required_exts)),
            .ppEnabledExtensionNames = required_exts,
            .pEnabledFeatures = nullptr,
        };
        if (vkCreateDevice(m_physical_device, &device_info, nullptr, &m_device) != VK_SUCCESS) {
            setError(error, "cannot create Vulkan device");
            destroyVulkan();
            return false;
        }
        vkGetDeviceQueue(m_device, m_queue_family, 0, &m_queue);

        PFN_vkGetPhysicalDeviceProperties2 get_properties2 =
            reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
                vkGetInstanceProcAddr(m_instance, "vkGetPhysicalDeviceProperties2"));
        if (get_properties2 != nullptr && m_have_drm_ext) {
            VkPhysicalDeviceDrmPropertiesEXT drm {};
            drm.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT;
            VkPhysicalDeviceIDProperties id_props {};
            id_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
            id_props.pNext = &drm;
            VkPhysicalDeviceProperties2 properties {};
            properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            properties.pNext = &id_props;
            get_properties2(m_physical_device, &properties);
            m_drm_major = drm.hasRender == VK_TRUE ? drm.renderMajor : 0u;
            m_drm_minor = drm.hasRender == VK_TRUE ? drm.renderMinor : 0u;
            std::memcpy(m_device_uuid, id_props.deviceUUID, sizeof(m_device_uuid));
            std::memcpy(m_driver_uuid, id_props.driverUUID, sizeof(m_driver_uuid));
        }

        m_drm_fd = OpenRenderNode(m_drm_major, m_drm_minor);
        if (m_drm_fd < 0) {
            for (std::uint32_t minor = 128; minor <= 255 && m_drm_fd < 0; ++minor) {
                m_drm_fd = OpenRenderNode(0, minor);
                if (m_drm_fd >= 0) m_drm_minor = minor;
            }
        }
        if (m_host != nullptr) {
            m_host->setGpuInfo(m_drm_major, m_drm_minor, m_device_uuid, m_driver_uuid);
        }

        md_vk_export_context_t context {
            .instance = m_instance,
            .physical_device = m_physical_device,
            .device = m_device,
            .queue = m_queue,
            .queue_family_index = m_queue_family,
            .drm_render_fd = m_drm_fd,
            .drm_render_major = m_drm_major,
            .drm_render_minor = m_drm_minor,
        };
        m_exporter = md_vk_exporter_new(&context);
        if (m_exporter == nullptr) {
            setError(error, "cannot create Vulkan export helper");
            destroyVulkan();
            return false;
        }

        VkCommandPoolCreateInfo pool_info {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = m_queue_family,
        };
        if (vkCreateCommandPool(m_device, &pool_info, nullptr, &m_upload_pool) != VK_SUCCESS) {
            setError(error, "cannot create Vulkan upload command pool");
            destroyVulkan();
            return false;
        }
        VkCommandBufferAllocateInfo command_info {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = m_upload_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        if (vkAllocateCommandBuffers(m_device, &command_info, &m_upload_cmd) != VK_SUCCESS) {
            setError(error, "cannot create Vulkan upload command buffer");
            destroyVulkan();
            return false;
        }
        VkFenceCreateInfo fence_info {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
        };
        if (vkCreateFence(m_device, &fence_info, nullptr, &m_upload_fence) != VK_SUCCESS) {
            setError(error, "cannot create Vulkan upload fence");
            destroyVulkan();
            return false;
        }
        return true;
    }

    void destroyVulkan() {
        if (m_device != VK_NULL_HANDLE) vkDeviceWaitIdle(m_device);
        if (m_upload_fence != VK_NULL_HANDLE) {
            vkDestroyFence(m_device, m_upload_fence, nullptr);
            m_upload_fence = VK_NULL_HANDLE;
        }
        if (m_upload_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_device, m_upload_pool, nullptr);
            m_upload_pool = VK_NULL_HANDLE;
            m_upload_cmd = VK_NULL_HANDLE;
        }
        if (m_device != VK_NULL_HANDLE) vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
        if (m_instance != VK_NULL_HANDLE) vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
        m_physical_device = VK_NULL_HANDLE;
        m_queue = VK_NULL_HANDLE;
        if (m_drm_fd >= 0) {
            close(m_drm_fd);
            m_drm_fd = -1;
        }
    }

    bool createUploadResources(std::uint32_t width, std::uint32_t height) {
        destroyUploadResources();
        VkImageCreateInfo image_info {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .extent = {width, height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        if (vkCreateImage(m_device, &image_info, nullptr, &m_upload_image) != VK_SUCCESS) {
            return false;
        }
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(m_device, m_upload_image, &requirements);
        const std::uint32_t memory_type = ChooseMemoryType(m_physical_device,
                                                           requirements.memoryTypeBits,
                                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memory_type == UINT32_MAX) {
            destroyUploadResources();
            return false;
        }
        VkMemoryAllocateInfo allocate_info {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = requirements.size,
            .memoryTypeIndex = memory_type,
        };
        if (vkAllocateMemory(m_device, &allocate_info, nullptr, &m_upload_memory) != VK_SUCCESS ||
            vkBindImageMemory(m_device, m_upload_image, m_upload_memory, 0) != VK_SUCCESS) {
            destroyUploadResources();
            return false;
        }
        const VkDeviceSize staging_size = static_cast<VkDeviceSize>(width) * height * 4u;
        VkBufferCreateInfo buffer_info {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = staging_size,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
        };
        if (vkCreateBuffer(m_device, &buffer_info, nullptr, &m_staging_buffer) != VK_SUCCESS) {
            destroyUploadResources();
            return false;
        }
        vkGetBufferMemoryRequirements(m_device, m_staging_buffer, &requirements);
        const std::uint32_t staging_type = ChooseMemoryType(
            m_physical_device, requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (staging_type == UINT32_MAX) {
            destroyUploadResources();
            return false;
        }
        VkMemoryAllocateInfo staging_allocate {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = requirements.size,
            .memoryTypeIndex = staging_type,
        };
        if (vkAllocateMemory(m_device, &staging_allocate, nullptr, &m_staging_memory) != VK_SUCCESS ||
            vkBindBufferMemory(m_device, m_staging_buffer, m_staging_memory, 0) != VK_SUCCESS ||
            vkMapMemory(m_device, m_staging_memory, 0, staging_size, 0, &m_staging_map) != VK_SUCCESS) {
            destroyUploadResources();
            return false;
        }
        m_upload_width = width;
        m_upload_height = height;

        if (vkResetCommandPool(m_device, m_upload_pool, 0) != VK_SUCCESS ||
            vkResetFences(m_device, 1, &m_upload_fence) != VK_SUCCESS) {
            destroyUploadResources();
            return false;
        }
        VkCommandBufferBeginInfo begin_info {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr,
        };
        if (vkBeginCommandBuffer(m_upload_cmd, &begin_info) != VK_SUCCESS) {
            destroyUploadResources();
            return false;
        }
        VkImageMemoryBarrier barrier {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = m_upload_image,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        vkCmdPipelineBarrier(m_upload_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
                             1, &barrier);
        if (vkEndCommandBuffer(m_upload_cmd) != VK_SUCCESS) {
            destroyUploadResources();
            return false;
        }
        VkSubmitInfo submit_info {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 0,
            .pWaitSemaphores = nullptr,
            .pWaitDstStageMask = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers = &m_upload_cmd,
            .signalSemaphoreCount = 0,
            .pSignalSemaphores = nullptr,
        };
        if (vkQueueSubmit(m_queue, 1, &submit_info, m_upload_fence) != VK_SUCCESS ||
            vkWaitForFences(m_device, 1, &m_upload_fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
            destroyUploadResources();
            return false;
        }
        return true;
    }

    void destroyUploadResources() {
        if (m_device == VK_NULL_HANDLE) return;
        vkDeviceWaitIdle(m_device);
        if (m_staging_map != nullptr) {
            vkUnmapMemory(m_device, m_staging_memory);
            m_staging_map = nullptr;
        }
        if (m_staging_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, m_staging_buffer, nullptr);
            m_staging_buffer = VK_NULL_HANDLE;
        }
        if (m_staging_memory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, m_staging_memory, nullptr);
            m_staging_memory = VK_NULL_HANDLE;
        }
        if (m_upload_image != VK_NULL_HANDLE) {
            vkDestroyImage(m_device, m_upload_image, nullptr);
            m_upload_image = VK_NULL_HANDLE;
        }
        if (m_upload_memory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, m_upload_memory, nullptr);
            m_upload_memory = VK_NULL_HANDLE;
        }
    }

    bool rebuildPool() {
        md_producer_config_t config {};
        std::uint64_t version = 0;
        std::uint64_t epoch = 0;
        if (m_host == nullptr || !m_host->currentConfig(config, version, epoch)) return false;
        if (config.physical_width == 0 || config.physical_height == 0) return false;

        const std::uint64_t generation = m_host->nextGeneration();
        md_vk_export_pool_info_t pool_info {
            .generation = generation,
            .buffer_count = kExportBufferCount,
            .width = config.physical_width,
            .height = config.physical_height,
            .fourcc = config.fourcc,
            .plane_count = config.plane_count,
            .modifier = config.modifier,
        };
        if (m_exporter == nullptr || md_vk_exporter_create_pool(m_exporter, &pool_info) != MD_OK) {
            return false;
        }
        const md_buffer_pool_t* pool = md_vk_exporter_pool(m_exporter);
        if (m_host->offerPool(pool) != MD_OK) {
            md_vk_exporter_release_pool(m_exporter);
            return false;
        }
        m_generation = generation;
        m_config_version = version;
        m_connection_epoch = epoch;
        m_pool_width = config.physical_width;
        m_pool_height = config.physical_height;
        m_canvas.assign(static_cast<std::size_t>(m_pool_width) * m_pool_height * 4u, 0);
        return createUploadResources(m_pool_width, m_pool_height);
    }

    void serviceHostAndPool() {
        const std::uint64_t retire_generation = m_host->takeRetireGeneration();
        if (retire_generation != 0 && retire_generation == m_generation) {
            md_vk_exporter_release_pool(m_exporter);
            m_generation = 0;
            m_host->retireDone(retire_generation);
        }
        md_producer_config_t config {};
        std::uint64_t version = 0;
        std::uint64_t epoch = 0;
        if (m_host->snapshotConfig(m_config_version, m_connection_epoch, config, version, epoch)) {
            m_config_version = version;
            m_connection_epoch = epoch;
            if (!rebuildPool()) {
                fail(QStringLiteral("cannot rebuild export pool for new output configuration"));
            }
        }
    }

    // —— libmpv 集成（解码/音频/同步/循环全部交由 libmpv）——
    // 向渲染线程（mpv 线程）投递命令。mpv_wakeup 是唯一允许跨线程调用的
    // mpv API；其余 mpv_* 调用必须发生在渲染线程（见 renderLoop）。wakeup
    // 保持在锁内，与 cleanupMpv() 中 m_mpv 置空互斥，避免指向已销毁 handle。
    void postMpvCommand(std::function<void()> command) {
        std::lock_guard lock(m_control_mutex);
        m_mpv_commands.push_back(std::move(command));
        if (m_mpv != nullptr) mpv_wakeup(m_mpv);
    }

    bool openWithMpv() {
        m_mpv = mpv_create();
        if (m_mpv == nullptr) {
            m_last_error = "cannot create libmpv handle";
            return false;
        }
        // 行为可预测：不读用户 mpv.conf、不加载脚本；必须显式 vo=libmpv，
        // 否则 mpv 会打开默认 VO 窗口。
        const struct {
            const char* name;
            const char* value;
        } options[] = {
            {"config", "no"},
            {"load-scripts", "no"},
            {"vo", "libmpv"},
            {"hwdec", "auto-copy"}, // GPU 解码+帧回 CPU，mpv 内建决策（NVIDIA/Intel/AMD 全覆盖）
            {"ao", "pipewire,pulseaudio,alsa"},
            {"loop-file", "no"},  // 无自动循环：EOF 触发 video-did-end，由外部 play() 重播
            {"keep-open", "yes"}, // EOF 后保持核心存活，供 play() seek 0 重播
        };
        for (const auto& option : options) {
            if (mpv_set_option_string(m_mpv, option.name, option.value) < 0) {
                m_last_error = std::string("cannot set libmpv option ") + option.name;
                return false;
            }
        }
        if (mpv_initialize(m_mpv) < 0) {
            m_last_error = "libmpv initialization failed";
            return false;
        }
        const char* api = MPV_RENDER_API_TYPE_SW;
        mpv_render_param render_params[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(api)},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        if (mpv_render_context_create(&m_render, m_mpv, render_params) < 0) {
            m_last_error = "libmpv software render context creation failed";
            return false;
        }
        // 初始属性（壁纸总是自动播放；volume 0..100）。
        double initial_volume =
            static_cast<double>(VRClampVideoVolume(m_config.volume)) * 100.0;
        int initial_mute = m_config.muted ? 1 : 0;
        int initial_pause = 0;
        if (mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &initial_volume) < 0 ||
            mpv_set_property(m_mpv, "mute", MPV_FORMAT_FLAG, &initial_mute) < 0 ||
            mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &initial_pause) < 0) {
            m_last_error = "cannot set libmpv initial properties";
            return false;
        }
        const QByteArray path = m_config.videoPath.toUtf8();
        const char* load_command[] = {"loadfile", path.constData(), nullptr};
        if (mpv_command(m_mpv, load_command) < 0) {
            m_last_error = "libmpv loadfile failed";
            return false;
        }
        // 等待 FILE_LOADED 或加载错误（最多 15s），与引擎 open() 语义一致。
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (std::chrono::steady_clock::now() < deadline) {
            if (!m_running.load()) {
                m_last_error = "renderer stopped while opening video";
                return false;
            }
            mpv_event* event = mpv_wait_event(m_mpv, 0.05);
            if (event->event_id == MPV_EVENT_FILE_LOADED) return true;
            if (event->event_id == MPV_EVENT_END_FILE) {
                const auto* end = static_cast<const mpv_event_end_file*>(event->data);
                if (end->reason == MPV_END_FILE_REASON_ERROR) {
                    const char* text = mpv_error_string(end->error);
                    m_last_error = std::string("libmpv failed to load video: ") +
                                   (text != nullptr ? text : "unknown error");
                    return false;
                }
            }
        }
        m_last_error = "timeout loading video with libmpv";
        return false;
    }

    void handleMpvEvent(const mpv_event* event) {
        switch (event->event_id) {
        case MPV_EVENT_END_FILE: {
            const auto* end = static_cast<const mpv_event_end_file*>(event->data);
            if (end->reason == MPV_END_FILE_REASON_EOF) {
                if (m_eof.exchange(true)) return;
                if (!m_eof_notified.exchange(true) && m_config.videoDidEndCallback) {
                    m_config.videoDidEndCallback();
                }
            } else if (end->reason == MPV_END_FILE_REASON_ERROR) {
                const char* text = mpv_error_string(end->error);
                fail(QStringLiteral("libmpv playback error: %1")
                         .arg(text != nullptr ? QString::fromUtf8(text)
                                              : QStringLiteral("unknown error")));
            }
            break;
        }
        default:
            break;
        }
    }

    void processMpvCommands() {
        std::deque<std::function<void()>> commands;
        {
            std::lock_guard lock(m_control_mutex);
            commands.swap(m_mpv_commands);
        }
        for (auto& command : commands) command();
    }

    void cleanupMpv() {
        // mpv 资源在渲染线程释放（与创建同线程）；置空加锁避免与
        // postMpvCommand()/stop() 跨线程读 m_mpv 竞争。
        mpv_render_context* render = nullptr;
        mpv_handle* handle = nullptr;
        {
            std::lock_guard lock(m_control_mutex);
            render = m_render;
            m_render = nullptr;
            handle = m_mpv;
            m_mpv = nullptr;
        }
        if (render != nullptr) mpv_render_context_free(render);
        if (handle != nullptr) mpv_terminate_destroy(handle);
        if (m_sws != nullptr) {
            sws_freeContext(m_sws);
            m_sws = nullptr;
        }
    }

    struct FitRect {
        std::uint32_t x;
        std::uint32_t y;
        std::uint32_t w;
        std::uint32_t h;
    };

    FitRect computeFitRect(int source_w, int source_h) const {
        const double source_aspect = static_cast<double>(source_w) / source_h;
        const double pool_aspect = static_cast<double>(m_pool_width) / m_pool_height;
        FitRect rect {0, 0, m_pool_width, m_pool_height};
        switch (m_fill_mode.load()) {
        case VRVideoFillModeContain:
            if (source_aspect > pool_aspect) {
                rect.w = m_pool_width;
                rect.h = static_cast<std::uint32_t>(m_pool_width / source_aspect);
            } else {
                rect.h = m_pool_height;
                rect.w = static_cast<std::uint32_t>(m_pool_height * source_aspect);
            }
            rect.x = (m_pool_width - rect.w) / 2;
            rect.y = (m_pool_height - rect.h) / 2;
            break;
        case VRVideoFillModeCover:
            if (source_aspect > pool_aspect) {
                rect.h = m_pool_height;
                rect.w = static_cast<std::uint32_t>(m_pool_height * source_aspect);
            } else {
                rect.w = m_pool_width;
                rect.h = static_cast<std::uint32_t>(m_pool_width / source_aspect);
            }
            rect.x = (m_pool_width - rect.w) / 2;
            rect.y = (m_pool_height - rect.h) / 2;
            break;
        case VRVideoFillModeStretch:
        default:
            break;
        }
        return rect;
    }

    // 从 mpv 取当前帧（SW RGBA，视频原始尺寸），按 fillMode 缩放合成进
    // pool 画布并上传导出。合成逻辑沿用原 CPU 端实现（sws 缩放+黑边画布），
    // 行为与迁移前一致。
    void presentMpvFrame() {
        if (m_pool_width == 0 || m_upload_image == VK_NULL_HANDLE) return;
        serviceHostAndPool();
        if (m_exporter == nullptr || md_vk_exporter_pool(m_exporter) == nullptr ||
            m_upload_image == VK_NULL_HANDLE) {
            return;
        }

        long long video_w = 0;
        long long video_h = 0;
        if (mpv_get_property(m_mpv, "video-params/w", MPV_FORMAT_INT64, &video_w) < 0 ||
            mpv_get_property(m_mpv, "video-params/h", MPV_FORMAT_INT64, &video_h) < 0 ||
            video_w <= 0 || video_h <= 0) {
            return; // 尚无视频参数（加载中），跳过本帧
        }
        if (static_cast<long long>(m_mpv_buf_w) != video_w ||
            static_cast<long long>(m_mpv_buf_h) != video_h) {
            m_mpv_buf_w = static_cast<int>(video_w);
            m_mpv_buf_h = static_cast<int>(video_h);
            m_mpv_buf_stride = m_mpv_buf_w * 4;
            m_mpv_buf.assign(static_cast<std::size_t>(m_mpv_buf_stride) *
                                 static_cast<std::size_t>(m_mpv_buf_h),
                             0);
        }
        if (m_mpv_buf.empty()) return;
        int sw_size[2] = {m_mpv_buf_w, m_mpv_buf_h};
        mpv_render_param render_params[] = {
            {MPV_RENDER_PARAM_SW_SIZE, sw_size},
            {MPV_RENDER_PARAM_SW_FORMAT, const_cast<char*>("rgba")},
            {MPV_RENDER_PARAM_SW_STRIDE, &m_mpv_buf_stride},
            {MPV_RENDER_PARAM_SW_POINTER, m_mpv_buf.data()},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        if (mpv_render_context_render(m_render, render_params) < 0) return;

        const FitRect rect = computeFitRect(m_mpv_buf_w, m_mpv_buf_h);
        if (rect.w == 0 || rect.h == 0) return;
        // 源格式固定为 RGBA（mpv SW 输出），仅按源/目标尺寸重建 sws 上下文。
        if (m_sws == nullptr ||
            m_sws_source_w != m_mpv_buf_w || m_sws_source_h != m_mpv_buf_h ||
            m_sws_target_w != rect.w || m_sws_target_h != rect.h) {
            if (m_sws != nullptr) sws_freeContext(m_sws);
            m_sws = sws_getContext(m_mpv_buf_w, m_mpv_buf_h, AV_PIX_FMT_RGBA,
                                   static_cast<int>(rect.w), static_cast<int>(rect.h),
                                   AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            // 创建失败时源尺寸置 0，保证下一帧会重新尝试重建（否则
            // m_sws==nullptr 但尺寸匹配导致缩放永久失效）。
            m_sws_source_w = m_sws != nullptr ? m_mpv_buf_w : 0;
            m_sws_source_h = m_sws != nullptr ? m_mpv_buf_h : 0;
            m_sws_target_w = rect.w;
            m_sws_target_h = rect.h;
            m_scaled.assign(static_cast<std::size_t>(rect.w) * rect.h * 4u, 0);
        }
        if (m_sws == nullptr) return;
        // RGBA 为单平面：源指针/步长只使用第 0 平面。
        const std::uint8_t* src_planes[4] = {m_mpv_buf.data(), nullptr, nullptr, nullptr};
        const int src_linesize[4] = {m_mpv_buf_stride, 0, 0, 0};
        uint8_t* dst = m_scaled.data();
        int dst_stride = static_cast<int>(rect.w) * 4;
        if (sws_scale(m_sws, src_planes, src_linesize, 0, m_mpv_buf_h, &dst,
                      &dst_stride) <= 0) {
            return;
        }

        /* Composite the fitted region into the pool-sized canvas. */
        std::memset(m_canvas.data(), 0, m_canvas.size());
        for (std::uint32_t y = 0; y < rect.h; ++y) {
            std::memcpy(m_canvas.data() + (static_cast<std::size_t>(rect.y + y) * m_pool_width +
                                           rect.x) * 4u,
                        m_scaled.data() + static_cast<std::size_t>(y) * rect.w * 4u,
                        static_cast<std::size_t>(rect.w) * 4u);
        }
        uploadAndSubmit();
    }

    bool uploadAndSubmit() {
        if (m_staging_map == nullptr) return false;
        const std::size_t bytes = static_cast<std::size_t>(m_pool_width) * m_pool_height * 4u;
        std::memcpy(m_staging_map, m_canvas.data(), bytes);

        if (vkResetCommandPool(m_device, m_upload_pool, 0) != VK_SUCCESS ||
            vkResetFences(m_device, 1, &m_upload_fence) != VK_SUCCESS) {
            return false;
        }
        VkCommandBufferBeginInfo begin_info {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr,
        };
        if (vkBeginCommandBuffer(m_upload_cmd, &begin_info) != VK_SUCCESS) return false;
        VkImageMemoryBarrier to_dst {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = m_upload_image,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        vkCmdPipelineBarrier(m_upload_cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                             1, &to_dst);
        VkBufferImageCopy region {
            .bufferOffset = 0,
            .bufferRowLength = m_pool_width,
            .bufferImageHeight = m_pool_height,
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .imageOffset = {0, 0, 0},
            .imageExtent = {m_pool_width, m_pool_height, 1},
        };
        vkCmdCopyBufferToImage(m_upload_cmd, m_staging_buffer, m_upload_image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        VkImageMemoryBarrier to_general {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = m_upload_image,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        vkCmdPipelineBarrier(m_upload_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
                             1, &to_general);
        if (vkEndCommandBuffer(m_upload_cmd) != VK_SUCCESS) return false;
        VkSubmitInfo submit_info {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 0,
            .pWaitSemaphores = nullptr,
            .pWaitDstStageMask = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers = &m_upload_cmd,
            .signalSemaphoreCount = 0,
            .pSignalSemaphores = nullptr,
        };
        if (vkQueueSubmit(m_queue, 1, &submit_info, m_upload_fence) != VK_SUCCESS) {
            return false;
        }
        if (vkWaitForFences(m_device, 1, &m_upload_fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
            return false;
        }

        if (md_vk_exporter_pool(m_exporter) == nullptr) return true;
        std::uint32_t buffer_index = 0;
        if (md_vk_exporter_acquire(m_exporter, &buffer_index) != MD_OK) {
            return true; /* all slots owned by the consumer; drop the frame */
        }
        int acquire_fd = -1;
        int release_fd = -1;
        int rc = md_vk_exporter_copy_frame(m_exporter, buffer_index, m_upload_image,
                                           VK_IMAGE_LAYOUT_GENERAL, m_pool_width,
                                           m_pool_height, &acquire_fd, &release_fd);
        if (rc != MD_OK) {
            md_vk_exporter_cancel_frame(m_exporter, buffer_index);
            return true;
        }
        rc = m_host->submitFrame(m_generation, buffer_index, m_sequence++,
                                 acquire_fd, release_fd);
        if (rc != MD_OK) {
            md_vk_exporter_cancel_frame(m_exporter, buffer_index);
            return true;
        }
        if (!m_first_frame.exchange(true) && m_config.firstFrameCallback) {
            m_config.firstFrameCallback();
        }
        return true;
    }

    // 渲染线程 = mpv 线程：创建 mpv、处理事件与命令、SW 渲染取帧合成上传。
    void renderLoop() {
        if (!openWithMpv()) {
            fail(QString::fromStdString(m_last_error));
            cleanupMpv(); // openWithMpv 中途失败可能已创建 mpv/render context，必须在此释放
            return;
        }
        auto last_report = std::chrono::steady_clock::now();
        while (m_running.load()) {
            processMpvCommands();
            // 阻塞短暂超时：有事件立即返回，无事件每 10ms 轮询一次渲染。
            mpv_event* event = mpv_wait_event(m_mpv, 0.01);
            if (event->event_id != MPV_EVENT_NONE) handleMpvEvent(event);
            if (!m_running.load()) break;

            const uint64_t flags = mpv_render_context_update(m_render);
            if ((flags & MPV_RENDER_UPDATE_FRAME) != 0u) {
                presentMpvFrame();
            }

            // hwdec-current 诊断日志：报告 mpv 实际选用的解码路径（软解为
            // "no"）。仅读取属性打日志，不参与任何决策分支。
            const auto now = std::chrono::steady_clock::now();
            if (now - last_report >= std::chrono::seconds(2)) {
                last_report = now;
                char* hwdec = mpv_get_property_string(m_mpv, "hwdec-current");
                std::fprintf(stderr, "VideoWallpaper: hwdec-current=%s\n",
                             hwdec != nullptr ? hwdec : "?");
                mpv_free(hwdec);
            }
        }
        cleanupMpv();
    }

    void fail(const QString& message) {
        std::fprintf(stderr, "VideoWallpaper: %s\n", message.toLocal8Bit().constData());
        if (m_config.errorCallback) m_config.errorCallback(message);
        m_running.store(false);
        m_control_cv.notify_all();
    }

    static void setError(QString* error, const QString& message) {
        if (error != nullptr) *error = message;
    }

    Config m_config;

    std::unique_ptr<ProtocolHost> m_host;

    VkInstance m_instance { VK_NULL_HANDLE };
    VkPhysicalDevice m_physical_device { VK_NULL_HANDLE };
    VkDevice m_device { VK_NULL_HANDLE };
    VkQueue m_queue { VK_NULL_HANDLE };
    std::uint32_t m_queue_family { 0 };
    bool m_have_drm_ext { false };
    std::uint32_t m_drm_major { 0 };
    std::uint32_t m_drm_minor { 0 };
    std::uint8_t m_device_uuid[16] { 0 };
    std::uint8_t m_driver_uuid[16] { 0 };
    int m_drm_fd { -1 };

    md_vk_exporter_t* m_exporter { nullptr };
    VkImage m_upload_image { VK_NULL_HANDLE };
    VkDeviceMemory m_upload_memory { VK_NULL_HANDLE };
    VkBuffer m_staging_buffer { VK_NULL_HANDLE };
    VkDeviceMemory m_staging_memory { VK_NULL_HANDLE };
    void* m_staging_map { nullptr };
    VkCommandPool m_upload_pool { VK_NULL_HANDLE };
    VkCommandBuffer m_upload_cmd { VK_NULL_HANDLE };
    VkFence m_upload_fence { VK_NULL_HANDLE };
    std::uint32_t m_upload_width { 0 };
    std::uint32_t m_upload_height { 0 };

    std::uint32_t m_pool_width { 0 };
    std::uint32_t m_pool_height { 0 };
    std::vector<std::uint8_t> m_canvas;
    std::vector<std::uint8_t> m_scaled;
    std::atomic<VRVideoFillMode> m_fill_mode { VRVideoFillModeCover };
    std::atomic_bool m_first_frame { false };
    std::atomic_bool m_eof_notified { false };
    std::atomic_bool m_stopped { false };
    std::atomic<float> m_volume { 1.0f };
    std::atomic_bool m_muted { false };

    std::uint64_t m_generation { 0 };
    std::uint64_t m_config_version { 0 };
    std::uint64_t m_connection_epoch { 0 };
    std::uint64_t m_sequence { 1 };

    std::thread m_render_thread;
    std::atomic_bool m_running { false };
    std::atomic_bool m_user_paused { false };
    std::atomic_bool m_eof { false };
    std::atomic_bool m_restart_requested { false };
    std::mutex m_control_mutex;
    std::condition_variable m_control_cv;
    // libmpv（渲染线程独占；m_mpv 置空/读取在 m_control_mutex 保护下）
    mpv_handle* m_mpv { nullptr };
    mpv_render_context* m_render { nullptr };
    std::deque<std::function<void()>> m_mpv_commands;

    // mpv SW 渲染缓冲（视频原始尺寸 RGBA）
    std::vector<std::uint8_t> m_mpv_buf;
    int m_mpv_buf_w { 0 };
    int m_mpv_buf_h { 0 };
    int m_mpv_buf_stride { 0 };

    // 缩放合成（RGBA→RGBA，fillMode）
    SwsContext* m_sws { nullptr };
    int m_sws_source_w { 0 };
    int m_sws_source_h { 0 };
    std::uint32_t m_sws_target_w { 0 };
    std::uint32_t m_sws_target_h { 0 };
    std::string m_last_error;
};

VRProtocolVideoRenderer::VRProtocolVideoRenderer(Config config)
    : m_impl(std::make_unique<Impl>(std::move(config))) {}

VRProtocolVideoRenderer::~VRProtocolVideoRenderer() = default;

bool VRProtocolVideoRenderer::start(QString* error) {
    return m_impl->start(error);
}

void VRProtocolVideoRenderer::stop() {
    m_impl->stop();
}

void VRProtocolVideoRenderer::play() {
    m_impl->play();
}

void VRProtocolVideoRenderer::pause() {
    m_impl->pause();
}

void VRProtocolVideoRenderer::setVolume(float volume) {
    m_impl->setVolume(volume);
}

void VRProtocolVideoRenderer::setMuted(bool muted) {
    m_impl->setMuted(muted);
}

void VRProtocolVideoRenderer::setFillMode(VRVideoFillMode fillMode) {
    m_impl->setFillMode(fillMode);
}
