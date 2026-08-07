#include "VideoRendererEngine.h"

#include "VideoManifest.h"

#include <SDL.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace {

constexpr std::int32_t kOutputSampleRate = 48000;
constexpr std::int32_t kOutputChannels = 2;

#if defined(SDL_HINT_AUDIODRIVER)
constexpr const char* kAudioDriverHint = SDL_HINT_AUDIODRIVER;
#elif defined(SDL_HINT_AUDIO_DRIVER)
constexpr const char* kAudioDriverHint = SDL_HINT_AUDIO_DRIVER;
#else
constexpr const char* kAudioDriverHint = "SDL_AUDIODRIVER";
#endif

} // namespace

class VRVideoRendererEngine::Impl {
public:
    Impl(VRVideoEngineConfig config, Callbacks callbacks)
        : m_config(config), m_callbacks(std::move(callbacks)) {}

    ~Impl() { close(); }

    bool open(const VRVideoManifest& manifest, std::string* error) {
        if (m_opened) {
            setError(error, "video engine is already open");
            return false;
        }
        const std::string path = manifest.videoUrl().toLocalFile().toStdString();
        if (path.empty()) {
            setError(error, "video manifest has no local file");
            return false;
        }
        m_video_path = path;

        if (! openMedia(error)) return false;
        if (m_video_stream != nullptr) {
            std::fprintf(stderr, "VideoRenderer: video stream %dx%d (avg %d/%d fps)\n",
                         m_video_codec->width, m_video_codec->height,
                         m_video_stream->avg_frame_rate.num, m_video_stream->avg_frame_rate.den);
        }
        std::fprintf(stderr, "VideoRenderer: audio stream: %s\n",
                     m_audio_codec != nullptr ? "yes" : "none");
        openAudioOutput();
        m_opened = true;
        m_playing.store(m_config.autoplay);
        m_play_start = std::chrono::steady_clock::now();
        m_thread = std::thread([this] { decodeLoop(); });
        return true;
    }

    void play() {
        {
            std::lock_guard<std::mutex> lock(m_control_mutex);
            if (! m_playing.exchange(true)) {
                const auto now = std::chrono::steady_clock::now();
                if (m_pause_begin != std::chrono::steady_clock::time_point {}) {
                    m_play_start += now - m_pause_begin;
                    m_pause_begin = std::chrono::steady_clock::time_point {};
                }
            }
        }
        m_cv.notify_all();
    }
    void pause() {
        std::lock_guard<std::mutex> lock(m_control_mutex);
        if (m_playing.exchange(false)) {
            m_pause_begin = std::chrono::steady_clock::now();
        }
    }
    void setVolume(float value) { m_volume.store(VRClampVideoVolume(value)); }
    void setMuted(bool value) { m_muted.store(value); }
    void setFillMode(VRVideoFillMode mode) { m_fill_mode.store(mode); }

    bool loaded() const noexcept { return m_opened; }
    float volume() const noexcept { return m_volume.load(); }
    bool muted() const noexcept { return m_muted.load(); }
    VRVideoFillMode fillMode() const noexcept { return m_fill_mode.load(); }

    bool currentFrame(Frame& out) const {
        std::lock_guard<std::mutex> lock(m_frame_mutex);
        if (m_frame.empty() || m_frame_w <= 0 || m_frame_h <= 0) return false;
        out.rgb = m_frame.data();
        out.width = m_frame_w;
        out.height = m_frame_h;
        out.stride = m_frame_stride;
        out.serial = m_frame_serial;
        return true;
    }

private:
    void setError(std::string* error, const std::string& message) const {
        if (error) *error = message;
        if (m_callbacks.playbackError) m_callbacks.playbackError(message);
    }

    bool openMedia(std::string* error) {
        avformat_network_init();
        if (avformat_open_input(&m_format, m_video_path.c_str(), nullptr, nullptr) != 0) {
            setError(error, "cannot open video file");
            return false;
        }
        if (avformat_find_stream_info(m_format, nullptr) < 0) {
            setError(error, "cannot read video stream info");
            return false;
        }
        for (unsigned i = 0; i < m_format->nb_streams; ++i) {
            AVStream* stream = m_format->streams[i];
            if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && m_video_index < 0) {
                m_video_index = static_cast<int>(i);
            } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && m_audio_index < 0) {
                m_audio_index = static_cast<int>(i);
            }
        }
        if (m_video_index < 0) {
            setError(error, "video has no video stream");
            return false;
        }
        const AVCodec* video_codec =
            avcodec_find_decoder(m_format->streams[m_video_index]->codecpar->codec_id);
        if (video_codec == nullptr) {
            setError(error, "unsupported video codec");
            return false;
        }
        m_video_codec = avcodec_alloc_context3(video_codec);
        if (avcodec_parameters_to_context(m_video_codec,
                                          m_format->streams[m_video_index]->codecpar) < 0 ||
            avcodec_open2(m_video_codec, video_codec, nullptr) < 0) {
            setError(error, "cannot open video decoder");
            return false;
        }
        m_video_stream = m_format->streams[m_video_index];

        if (m_audio_index >= 0) {
            const AVCodec* audio_codec =
                avcodec_find_decoder(m_format->streams[m_audio_index]->codecpar->codec_id);
            if (audio_codec != nullptr) {
                m_audio_codec = avcodec_alloc_context3(audio_codec);
                if (avcodec_parameters_to_context(m_audio_codec,
                                                  m_format->streams[m_audio_index]->codecpar) == 0 &&
                    avcodec_open2(m_audio_codec, audio_codec, nullptr) == 0) {
                    m_audio_stream = m_format->streams[m_audio_index];
                } else {
                    avcodec_free_context(&m_audio_codec);
                    m_audio_codec = nullptr;
                }
            }
        }
        return true;
    }

    void openAudioOutput() {
        if (m_audio_codec == nullptr) return;
        SDL_SetHintWithPriority(kAudioDriverHint, "pipewire,pulseaudio,alsa",
                                SDL_HINT_OVERRIDE);
        if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            std::fprintf(stderr, "VideoRenderer: SDL audio init failed: %s\n", SDL_GetError());
            return;
        }
        SDL_AudioSpec desired {};
        desired.freq = kOutputSampleRate;
        desired.format = AUDIO_S16LSB;
        desired.channels = kOutputChannels;
        desired.samples = 2048;
        SDL_AudioSpec obtained {};
        m_audio_device = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained,
                                             SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
                                                 SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
        if (m_audio_device == 0) {
            std::fprintf(stderr, "VideoRenderer: SDL_OpenAudioDevice failed: %s\n",
                         SDL_GetError());
            return;
        }
        const char* driver = SDL_GetCurrentAudioDriver();
        std::fprintf(stderr, "VideoRenderer: SDL audio driver=%s device opened: freq=%d fmt=0x%x ch=%d\n",
                     driver != nullptr ? driver : "?",
                     obtained.freq, obtained.format, obtained.channels);
        SDL_PauseAudioDevice(m_audio_device, 0);
        m_swr = swr_alloc();
        AVChannelLayout in_layout = m_audio_codec->ch_layout;
        if (in_layout.order != AV_CHANNEL_ORDER_NATIVE &&
            in_layout.order != AV_CHANNEL_ORDER_AMBISONIC) {
            av_channel_layout_default(&in_layout, kOutputChannels);
        }
        av_opt_set_chlayout(m_swr, "in_chlayout", &in_layout, 0);
        av_opt_set_int(m_swr, "in_sample_rate", m_audio_codec->sample_rate, 0);
        if (m_audio_codec->sample_fmt != AV_SAMPLE_FMT_NONE) {
            av_opt_set_sample_fmt(m_swr, "in_sample_fmt", m_audio_codec->sample_fmt, 0);
        }
        AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
        av_opt_set_chlayout(m_swr, "out_chlayout", &out_layout, 0);
        av_opt_set_int(m_swr, "out_sample_rate", kOutputSampleRate, 0);
        av_opt_set_sample_fmt(m_swr, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
        if (swr_init(m_swr) < 0) {
            swr_free(&m_swr);
            SDL_CloseAudioDevice(m_audio_device);
            m_audio_device = 0;
        }
    }

    void writeAudio(const AVFrame* frame) {
        if (m_audio_device == 0 || m_swr == nullptr) return;
        const int out_samples = av_rescale_rnd(
            swr_get_delay(m_swr, frame->sample_rate) + frame->nb_samples,
            kOutputSampleRate,
            frame->sample_rate,
            AV_ROUND_UP);
        m_pcm.resize(static_cast<std::size_t>(out_samples) * kOutputChannels * 2u);
        uint8_t* out = m_pcm.data();
        const int converted = swr_convert(m_swr, &out, out_samples,
                                          const_cast<const std::uint8_t**>(frame->data),
                                          frame->nb_samples);
        if (converted <= 0) return;

        auto* samples = reinterpret_cast<std::int16_t*>(m_pcm.data());
        const float gain = m_muted.load() ? 0.0f : m_volume.load();
        const std::size_t count = static_cast<std::size_t>(converted) * kOutputChannels;
        for (std::size_t i = 0; i < count; ++i) {
            const float scaled = static_cast<float>(samples[i]) * gain;
            samples[i] = scaled > 32767.0f ? 32767
                         : scaled < -32768.0f ? -32768
                         : static_cast<std::int16_t>(scaled);
        }
        const std::uint32_t bytes = static_cast<std::uint32_t>(converted) *
                                     kOutputChannels * 2u;
        SDL_QueueAudio(m_audio_device, m_pcm.data(), bytes);
        m_audio_frames += 1;
        m_audio_bytes += bytes;
        const auto now = std::chrono::steady_clock::now();
        if (now - m_last_audio_report >= std::chrono::seconds(2)) {
            std::fprintf(stderr,
                         "VideoRenderer: audio frames=%llu bytes=%llu queued=%u\n",
                         static_cast<unsigned long long>(m_audio_frames),
                         static_cast<unsigned long long>(m_audio_bytes),
                         static_cast<unsigned>(SDL_GetQueuedAudioSize(m_audio_device)));
            m_last_audio_report = now;
        }
    }

    // Sleep until this video frame's presentation time so playback follows
    // the video timeline instead of decoding as fast as the CPU allows
    // (which would fast-forward and lose audio sync). Falls back to the
    // average frame rate when the stream carries no PTS.
    void paceToPresentationTime(const AVFrame* frame) {
        if (m_video_stream == nullptr) return;
        double presentSeconds = -1.0;
        if (frame->pts != AV_NOPTS_VALUE) {
            if (m_first_pts == AV_NOPTS_VALUE) m_first_pts = frame->pts;
            const double delta = static_cast<double>(frame->pts - m_first_pts);
            presentSeconds = delta * av_q2d(m_video_stream->time_base);
        } else if (m_video_stream->avg_frame_rate.den > 0 &&
                   m_video_stream->avg_frame_rate.num > 0) {
            presentSeconds = static_cast<double>(m_frame_count) /
                             av_q2d(m_video_stream->avg_frame_rate);
            ++m_frame_count;
        }
        if (presentSeconds < 0.0) return;
        const auto target =
            m_play_start + std::chrono::duration<double>(presentSeconds);
        std::unique_lock<std::mutex> lock(m_control_mutex);
        m_cv.wait_until(lock, target, [this] { return m_stop.load() || !m_playing.load(); });
    }

    void decodeLoop() {
        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        std::vector<std::uint8_t> rgb;
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(m_control_mutex);
                m_cv.wait_for(lock, std::chrono::milliseconds(10), [this] {
                    return m_stop.load() || m_playing.load();
                });
            }
            if (m_stop.load()) break;
            if (! m_playing.load()) continue;

            const int read_result = av_read_frame(m_format, packet);
            if (read_result < 0) {
                if (read_result == AVERROR_EOF || (m_format->pb != nullptr && avio_feof(m_format->pb))) {
                    if (m_config.autoplay) {
                        avformat_seek_file(m_format, -1, INT64_MIN, 0, 0, 0);
                        avcodec_flush_buffers(m_video_codec);
                        if (m_audio_device != 0) SDL_ClearQueuedAudio(m_audio_device);
                        {
                            std::lock_guard<std::mutex> lock(m_control_mutex);
                            m_play_start = std::chrono::steady_clock::now();
                            m_pause_begin = std::chrono::steady_clock::time_point {};
                            m_first_pts = AV_NOPTS_VALUE;
                            m_frame_count = 0;
                        }
                        continue;
                    }
                    m_ended.store(true);
                    break;
                }
                av_packet_unref(packet);
                continue;
            }

            if (packet->stream_index == m_video_index) {
                if (avcodec_send_packet(m_video_codec, packet) == 0) {
                    while (avcodec_receive_frame(m_video_codec, frame) == 0) {
                        if (m_sws == nullptr || m_sws_w != frame->width ||
                            m_sws_h != frame->height || m_sws_fmt != frame->format) {
                            if (m_sws != nullptr) sws_freeContext(m_sws);
                            m_sws = sws_getContext(frame->width,
                                                   frame->height,
                                                   static_cast<AVPixelFormat>(frame->format),
                                                   frame->width,
                                                   frame->height,
                                                   AV_PIX_FMT_RGBA,
                                                   SWS_BILINEAR,
                                                   nullptr,
                                                   nullptr,
                                                   nullptr);
                            m_sws_w = frame->width;
                            m_sws_h = frame->height;
                            m_sws_fmt = frame->format;
                        }
                        if (m_sws != nullptr) {
                            rgb.resize(static_cast<std::size_t>(frame->width) *
                                       static_cast<std::size_t>(frame->height) * 4u);
                            uint8_t* dst = rgb.data();
                            const int stride = frame->width * 4;
                            sws_scale(m_sws,
                                      frame->data,
                                      frame->linesize,
                                      0,
                                      frame->height,
                                      &dst,
                                      &stride);
                            {
                                std::lock_guard<std::mutex> lock(m_frame_mutex);
                                m_frame.swap(rgb);
                                m_frame_w = frame->width;
                                m_frame_h = frame->height;
                                m_frame_stride = stride;
                                m_frame_serial += 1;
                            }
                            paceToPresentationTime(frame);
                        }
                        av_frame_unref(frame);
                    }
                }
            } else if (packet->stream_index == m_audio_index) {
                if (m_audio_codec != nullptr && avcodec_send_packet(m_audio_codec, packet) == 0) {
                    while (avcodec_receive_frame(m_audio_codec, frame) == 0) {
                        writeAudio(frame);
                        av_frame_unref(frame);
                    }
                }
            }
            av_packet_unref(packet);
        }
        av_frame_free(&frame);
        av_packet_free(&packet);
    }

    void close() {
        m_stop.store(true);
        m_cv.notify_all();
        if (m_thread.joinable()) m_thread.join();

        if (m_audio_device != 0) {
            SDL_CloseAudioDevice(m_audio_device);
            m_audio_device = 0;
        }
        if (SDL_WasInit(SDL_INIT_AUDIO) != 0) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
        if (m_swr != nullptr) {
            swr_free(&m_swr);
        }
        if (m_sws != nullptr) {
            sws_freeContext(m_sws);
            m_sws = nullptr;
        }
        if (m_audio_codec != nullptr) {
            avcodec_free_context(&m_audio_codec);
            m_audio_codec = nullptr;
        }
        if (m_video_codec != nullptr) {
            avcodec_free_context(&m_video_codec);
            m_video_codec = nullptr;
        }
        if (m_format != nullptr) {
            avformat_close_input(&m_format);
            m_format = nullptr;
        }
        m_video_stream = nullptr;
        m_audio_stream = nullptr;
        m_video_index = -1;
        m_audio_index = -1;
        m_opened = false;
    }

    VRVideoEngineConfig m_config;
    Callbacks m_callbacks;

    std::string m_video_path;
    bool m_opened { false };

    // FFmpeg state
    AVFormatContext* m_format { nullptr };
    AVCodecContext* m_video_codec { nullptr };
    AVCodecContext* m_audio_codec { nullptr };
    AVStream* m_video_stream { nullptr };
    AVStream* m_audio_stream { nullptr };
    int m_video_index { -1 };
    int m_audio_index { -1 };
    SwsContext* m_sws { nullptr };
    int m_sws_w { 0 };
    int m_sws_h { 0 };
    int m_sws_fmt { 0 };
    SwrContext* m_swr { nullptr };
    std::vector<std::uint8_t> m_pcm;

    // SDL audio
    SDL_AudioDeviceID m_audio_device { 0 };
    std::uint64_t m_audio_frames { 0 };
    std::uint64_t m_audio_bytes { 0 };
    std::chrono::steady_clock::time_point m_last_audio_report {};

    // Playback clock for PTS-based pacing
    std::chrono::steady_clock::time_point m_play_start {};
    std::chrono::steady_clock::time_point m_pause_begin {};
    std::int64_t m_first_pts { AV_NOPTS_VALUE };
    std::uint64_t m_frame_count { 0 };

    // Frame handoff (decode thread -> viewer)
    mutable std::mutex m_frame_mutex;
    std::vector<std::uint8_t> m_frame;
    int m_frame_w { 0 };
    int m_frame_h { 0 };
    int m_frame_stride { 0 };
    std::uint64_t m_frame_serial { 0 };

    // Control
    std::thread m_thread;
    std::mutex m_control_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_stop { false };
    std::atomic<bool> m_playing { false };
    std::atomic<bool> m_ended { false };
    std::atomic<float> m_volume { 1.0f };
    std::atomic<bool> m_muted { false };
    std::atomic<VRVideoFillMode> m_fill_mode { VRVideoFillModeCover };
};

VRVideoRendererEngine::VRVideoRendererEngine(VRVideoEngineConfig config, Callbacks callbacks)
    : m_impl(std::make_unique<Impl>(config, std::move(callbacks))) {}

VRVideoRendererEngine::~VRVideoRendererEngine() = default;

VRVideoEngineConfig VRVideoRendererEngine::defaultConfig() noexcept {
    return VRDefaultVideoEngineConfig();
}

bool VRVideoRendererEngine::openWallpaper(const VRVideoManifest& manifest, std::string* error) {
    return m_impl->open(manifest, error);
}

void VRVideoRendererEngine::play() { m_impl->play(); }
void VRVideoRendererEngine::pause() { m_impl->pause(); }
void VRVideoRendererEngine::setVolume(float volume) { m_impl->setVolume(volume); }
void VRVideoRendererEngine::setMuted(bool muted) { m_impl->setMuted(muted); }
void VRVideoRendererEngine::setFillMode(VRVideoFillMode mode) { m_impl->setFillMode(mode); }

bool VRVideoRendererEngine::loaded() const noexcept { return m_impl->loaded(); }
float VRVideoRendererEngine::volume() const noexcept { return m_impl->volume(); }
bool VRVideoRendererEngine::muted() const noexcept { return m_impl->muted(); }
VRVideoFillMode VRVideoRendererEngine::fillMode() const noexcept { return m_impl->fillMode(); }

bool VRVideoRendererEngine::currentFrame(Frame& out) const { return m_impl->currentFrame(out); }
