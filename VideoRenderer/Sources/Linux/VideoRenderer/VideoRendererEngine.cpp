#include "VideoRendererEngine.h"

#include <QAudioOutput>
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
    , m_autoplay(config.autoplay) {
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
    m_player->setLoops(QMediaPlayer::Infinite);
    setFillMode(m_fillMode);

    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error error, const QString& message) {
                if (error == QMediaPlayer::NoError) return;
                emit playbackError(message.isEmpty()
                                       ? QStringLiteral("video playback failed")
                                       : message);
            });
}

VRVideoRendererEngine::~VRVideoRendererEngine() {
    m_player->stop();
}

VRVideoEngineConfig VRVideoRendererEngine::defaultConfig() noexcept {
    return VRDefaultVideoEngineConfig();
}

bool VRVideoRendererEngine::openWallpaper(const VRVideoManifest& manifest, QString* error) {
    if (!manifest.videoUrl().isValid() || !manifest.videoUrl().isLocalFile()) {
        if (error) *error = QStringLiteral("invalid video wallpaper manifest");
        return false;
    }

    m_player->stop();
    m_loaded = false;
    m_player->setSource(manifest.videoUrl());
    m_player->setLoops(QMediaPlayer::Infinite);
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
