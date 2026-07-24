#include "VideoRendererEngine.h"

#include "VideoManifest.h"

#include <QAudioOutput>
#include <QBuffer>
#include <QFile>
#include <QMediaPlayer>
#include <QPalette>
#include <QVBoxLayout>
#include <QVideoWidget>

VRVideoRendererEngine::VRVideoRendererEngine(VRVideoEngineConfig config, QWidget* parent)
    : QWidget(parent)
    , m_player(new QMediaPlayer(this))
    , m_audioOutput(new QAudioOutput(this))
    , m_videoWidget(new QVideoWidget(this))
    , m_volume(VRClampVideoVolume(config.initialVolume))
    , m_muted(config.muted)
    , m_fillMode(config.fillMode)
    , m_autoplay(config.autoplay)
    , m_loadFromMemory(config.loadFromMemory) {
    setAutoFillBackground(true);
    QPalette enginePalette = palette();
    enginePalette.setColor(QPalette::Window, Qt::black);
    setPalette(enginePalette);

    m_videoWidget->setAutoFillBackground(true);
    QPalette videoPalette = m_videoWidget->palette();
    videoPalette.setColor(QPalette::Window, Qt::black);
    m_videoWidget->setPalette(videoPalette);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_videoWidget);

    m_audioOutput->setVolume(m_volume);
    m_audioOutput->setMuted(m_muted);
    m_player->setAudioOutput(m_audioOutput);
    m_player->setVideoOutput(m_videoWidget);
    m_player->setLoops(1);
    setFillMode(m_fillMode);

    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error error, const QString& message) {
                if (error == QMediaPlayer::NoError) return;
                emit playbackError(message.isEmpty()
                                       ? QStringLiteral("video playback failed")
                                       : message);
            });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status) {
                if (status != QMediaPlayer::EndOfMedia) return;
                emit videoDidEnd();
                // Keep wallpaper looping while playlist/videoSequence decides whether to advance.
                if (m_loaded && m_autoplay) m_player->play();
            });
}

VRVideoRendererEngine::~VRVideoRendererEngine() {
    m_player->stop();
    clearMemorySource();
}

VRVideoEngineConfig VRVideoRendererEngine::defaultConfig() noexcept {
    return VRDefaultVideoEngineConfig();
}

void VRVideoRendererEngine::clearMemorySource() {
    if (m_memoryBuffer) {
        if (m_memoryBuffer->isOpen()) m_memoryBuffer->close();
        m_memoryBuffer->deleteLater();
        m_memoryBuffer = nullptr;
    }
    m_memoryBytes.clear();
    m_memoryBytes.squeeze();
}

bool VRVideoRendererEngine::openWallpaper(const VRVideoManifest& manifest, QString* error) {
    if (!manifest.videoUrl().isValid() || !manifest.videoUrl().isLocalFile()) {
        if (error) *error = QStringLiteral("invalid video wallpaper manifest");
        return false;
    }

    m_player->stop();
    m_loaded = false;
    // Drop any previous in-memory payload before assigning a new source.
    m_player->setSource(QUrl());
    clearMemorySource();

    if (m_loadFromMemory) {
        QFile file(manifest.videoUrl().toLocalFile());
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) {
                *error = QStringLiteral("failed to load video into memory: %1")
                             .arg(file.errorString());
            }
            return false;
        }
        m_memoryBytes = file.readAll();
        file.close();
        if (m_memoryBytes.isEmpty()) {
            if (error) *error = QStringLiteral("failed to load video into memory: empty file");
            return false;
        }

        m_memoryBuffer = new QBuffer(&m_memoryBytes, this);
        if (!m_memoryBuffer->open(QIODevice::ReadOnly)) {
            if (error) *error = QStringLiteral("failed to open in-memory video buffer");
            clearMemorySource();
            return false;
        }
        // Keep the original file URL as a format/content hint for the demuxer.
        m_player->setSourceDevice(m_memoryBuffer, manifest.videoUrl());
    } else {
        m_player->setSource(manifest.videoUrl());
    }

    m_player->setLoops(1);
    m_audioOutput->setVolume(m_volume);
    m_audioOutput->setMuted(m_muted);
    m_loaded = true;
    if (m_autoplay) play();
    return true;
}

void VRVideoRendererEngine::play() {
    if (m_loaded) m_player->play();
}

void VRVideoRendererEngine::pause() {
    m_player->pause();
}

void VRVideoRendererEngine::setVolume(float volume) {
    m_volume = VRClampVideoVolume(volume);
    m_audioOutput->setVolume(m_volume);
}

void VRVideoRendererEngine::setMuted(bool muted) {
    m_muted = muted;
    m_audioOutput->setMuted(muted);
}

void VRVideoRendererEngine::setFillMode(VRVideoFillMode fillMode) {
    m_fillMode = fillMode;
    switch (fillMode) {
    case VRVideoFillModeContain:
        m_videoWidget->setAspectRatioMode(Qt::KeepAspectRatio);
        break;
    case VRVideoFillModeStretch:
        m_videoWidget->setAspectRatioMode(Qt::IgnoreAspectRatio);
        break;
    case VRVideoFillModeCover:
    default:
        m_videoWidget->setAspectRatioMode(Qt::KeepAspectRatioByExpanding);
        break;
    }
}
