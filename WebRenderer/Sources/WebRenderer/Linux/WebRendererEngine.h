#pragma once

#include <QImage>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <array>
#include <memory>

class QWebEngineView;
class WRManifest;

// QtWebEngine implementation of the shared WebRenderer contract. The page is
// owned by the GUI thread; frameReady is emitted there and the receiver owns
// the copied QImage. GPU export is isolated behind this interface so the
// mirage-display producer can be replaced without changing WE JavaScript APIs.
class WebRendererEngine final : public QObject {
    Q_OBJECT
public:
    struct Config {
        bool enableAudioSpectrum = true;
        bool enableAudioPlayback = true;
        float initialVolume = 1.0f;
        int frameRate = 60;
        bool loadFromMemory = false;
        QStringList assetOverlayDirectories;
    };

    explicit WebRendererEngine(const Config& config, QObject* parent = nullptr);
    ~WebRendererEngine() override;

    QWebEngineView* view() const { return m_view; }
    void openWallpaper(const WRManifest& manifest);
    void applyUserProperty(const QString& key, const QJsonValue& value);
    void applyUserProperties(const QJsonObject& properties);
    void setPaused(bool paused);
    void setVolume(float volume);
    void setMuted(bool muted);
    void setFrameRate(int fps);
    void setHostMediaPlaybackSuspended(bool suspended);
    void startAudioSpectrum();
    void stopAudioSpectrum();
    void pushAudioSpectrum(const std::array<float, 128>& spectrum);
    void takeSnapshotToPath(const QString& path);
    void sendPointerMotion(float x, float y);
    void sendPointerEnter(float x, float y);
    void sendPointerButton(float x, float y, uint32_t button, bool pressed);
    void sendPointerAxis(float x, float y, float deltaX, float deltaY);

signals:
    void frameReady(const QImage& image);
    void contentReady();
    void audioSpectrumDemandChanged(bool needed);
    void snapshotFinished(bool ok);
    void failed(const QString& message);

private:
    void captureFrame();
    void pollAudioDemand();
    void reconcileAudioSpectrum();
    void tickAudioSpectrum();
    void evaluate(const QString& script);

    Config m_config;
    QWebEngineView* m_view = nullptr;
    QTimer* m_captureTimer = nullptr;
    QTimer* m_audioDemandTimer = nullptr;
    QTimer* m_audioTimer = nullptr;
    std::unique_ptr<class WRAudioTap> m_audioTap;
    QString m_workshopDirectory;
    QJsonObject m_initialProperties;
    bool m_paused = false;
    bool m_muted = false;
    bool m_pageLoaded = false;
    bool m_audioSpectrumRequested = false;
    bool m_audioSpectrumStarted = false;
    bool m_audioSpectrumObserved = false;
    bool m_audioListenerDemand = false;
};
