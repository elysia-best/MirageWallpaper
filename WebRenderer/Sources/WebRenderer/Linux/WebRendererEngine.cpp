#include "WebRendererEngine.h"

#include "WallpaperManifest.h"
#include "WRAudioTap.h"

#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCoreApplication>
#include <QMouseEvent>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>
#include <QWebEnginePage>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>

#include <cstdlib>

namespace {

QString WebEngineShim() {
    return QStringLiteral(R"JS(
(function(){
  if(window.__mirage_web_shim)return;
  window.__mirage_web_shim=true;
  window.chrome=window.chrome||{runtime:{}};
  window.wallpaperPropertyListener=window.wallpaperPropertyListener||{};
  window.__mirage_paused=false;
  window.__mirage_volume=1.0;
  window.__mirage_muted=false;
  window.__wr_audio_listener_count=0;
  var __wr_audio_listeners=[];
  var __wr_audio_streams=[];
  window.wallpaperRegisterAudioListener=function(callback){
    if(__wr_audio_listeners.indexOf(callback)<0){
      __wr_audio_listeners.push(callback);
      window.__wr_audio_listener_count=__wr_audio_listeners.length;
    }
  };
  window.wallpaperRemoveAudioListener=function(callback){
    var index=__wr_audio_listeners.indexOf(callback);
    if(index>=0){
      __wr_audio_listeners.splice(index,1);
      window.__wr_audio_listener_count=__wr_audio_listeners.length;
    }
  };
  window.__wr_pushAudio=function(spectrum){
    __wr_audio_listeners.forEach(function(callback){
      try{callback(spectrum);}catch(error){console.error('WebRenderer audio listener:',error);}
    });
  };
  function __wr_applyMediaState(media){
    media.volume=window.__mirage_volume;
    media.muted=window.__mirage_muted;
    if(window.__mirage_paused&& !media.paused){media.__wr_host_was_playing=true;media.pause();}
  }
  function __wr_applyMediaTree(root){
    if(root instanceof HTMLMediaElement)__wr_applyMediaState(root);
    root.querySelectorAll('audio,video').forEach(__wr_applyMediaState);
  }
  window.wallpaperRegisterAudioStream=function(media){
    if(__wr_audio_streams.indexOf(media)<0)__wr_audio_streams.push(media);
    __wr_applyMediaState(media);
    return media;
  };
  window.wallpaperRemoveAudioStream=function(media){
    var index=__wr_audio_streams.indexOf(media);
    if(index>=0)__wr_audio_streams.splice(index,1);
  };
  new MutationObserver(function(records){
    records.forEach(function(record){
      record.addedNodes.forEach(function(node){
        if(node.nodeType===Node.ELEMENT_NODE)__wr_applyMediaTree(node);
      });
    });
  }).observe(document,{subtree:true,childList:true});
  window.__wr_applyProps=function(properties){
    if(window.wallpaperPropertyListener&&typeof window.wallpaperPropertyListener.applyUserProperties==='function')
      window.wallpaperPropertyListener.applyUserProperties(properties||{});
  };
  window.__wr_setPaused=function(paused){
    window.__mirage_paused=!!paused;
    if(window.wallpaperPropertyListener&&typeof window.wallpaperPropertyListener.setPaused==='function')
      window.wallpaperPropertyListener.setPaused(!!paused);
    document.querySelectorAll('audio,video').forEach(function(media){
      if(window.__mirage_paused){
        media.__wr_host_was_playing=!media.paused;
        if(media.__wr_host_was_playing)media.pause();
      }else if(media.__wr_host_was_playing){
        media.__wr_host_was_playing=false;
        var promise=media.play();
        if(promise&&promise.catch)promise.catch(function(){});
      }
    });
  };
  window.__wr_setVolume=function(volume){
    window.__mirage_volume=Math.max(0,Math.min(1,Number(volume)||0));
    __wr_applyMediaTree(document);
  };
  window.__wr_setMuted=function(muted){
    window.__mirage_muted=!!muted;
    __wr_applyMediaTree(document);
  };
  window.__wr_setFps=function(fps){window.__mirage_fps=Math.max(1,Number(fps)||60);};
})();
)JS");
}

} // namespace

WebRendererEngine::WebRendererEngine(const Config& config, QObject* parent)
    : QObject(parent), m_config(config), m_view(new QWebEngineView),
      m_audioTap(std::make_unique<WRAudioTap>()) {
    m_view->setAttribute(Qt::WA_DontShowOnScreen, true);
    m_view->setContextMenuPolicy(Qt::NoContextMenu);
    // WKWebView disables the user-gesture gate when audio playback is enabled.
    // QtWebEngine keeps Chromium's gate enabled by default, which prevents
    // Wallpaper Engine pages from starting their configured background audio
    // during document load. Keep the setting explicit so --no-audio remains a
    // real policy choice instead of relying on a backend default.
    m_view->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture,
                                     !m_config.enableAudioPlayback);
    QWebEngineScript shim;
    shim.setName(QStringLiteral("mirage-web-shim"));
    shim.setSourceCode(WebEngineShim());
    shim.setInjectionPoint(QWebEngineScript::DocumentCreation);
    shim.setWorldId(QWebEngineScript::MainWorld);
    shim.setRunsOnSubFrames(true);
    m_view->page()->scripts().insert(shim);
    connect(m_view, &QWebEngineView::loadFinished, this, [this](bool ok) {
        if (!ok) {
            emit failed(QStringLiteral("QtWebEngine failed to load wallpaper"));
            return;
        }
        setVolume(m_config.initialVolume);
        setMuted(m_muted);
        applyUserProperties(m_initialProperties);
        m_pageLoaded = true;
        if (std::getenv("WR_DEBUG") != nullptr) {
            qInfo("WebRenderer: navigation finished; polling audio listener demand");
        }
        pollAudioDemand();
        emit contentReady();
    });
    m_captureTimer = new QTimer(this);
    connect(m_captureTimer, &QTimer::timeout, this, &WebRendererEngine::captureFrame);
    m_audioDemandTimer = new QTimer(this);
    m_audioDemandTimer->setInterval(200);
    connect(m_audioDemandTimer, &QTimer::timeout, this, &WebRendererEngine::pollAudioDemand);
    m_audioTimer = new QTimer(this);
    m_audioTimer->setInterval(1000 / 30);
    connect(m_audioTimer, &QTimer::timeout, this, &WebRendererEngine::tickAudioSpectrum);
    setFrameRate(m_config.frameRate);
}

WebRendererEngine::~WebRendererEngine() {
    stopAudioSpectrum();
    delete m_view;
}

void WebRendererEngine::openWallpaper(const WRManifest& manifest) {
    m_pageLoaded = false;
    m_audioListenerDemand = false;
    reconcileAudioSpectrum();
    m_workshopDirectory = manifest.workshopDirectory();
    m_initialProperties = manifest.userProperties();
    const QString entry = QDir(m_workshopDirectory).filePath(manifest.entryHtml());
    if (!QFileInfo::exists(entry)) {
        emit failed(QStringLiteral("wallpaper entry does not exist: %1").arg(entry));
        return;
    }
    m_view->load(QUrl::fromLocalFile(entry));
}

void WebRendererEngine::evaluate(const QString& script) {
    m_view->page()->runJavaScript(script);
}

void WebRendererEngine::applyUserProperty(const QString& key, const QJsonValue& value) {
    QJsonObject object;
    object.insert(key, value);
    applyUserProperties(object);
}

void WebRendererEngine::applyUserProperties(const QJsonObject& properties) {
    const QString json = QString::fromUtf8(QJsonDocument(properties).toJson(QJsonDocument::Compact));
    evaluate(QStringLiteral("window.__wr_applyProps(%1);").arg(json));
}

void WebRendererEngine::setPaused(bool paused) {
    m_paused = paused;
    // Host-driven resume is not a user gesture. Consume a rejected play()
    // promise here so a backend policy decision cannot surface as an uncaught
    // page exception; page-owned play() calls keep their normal semantics.
    evaluate(QStringLiteral("window.__wr_setPaused(%1);")
                 .arg(paused ? QStringLiteral("true") : QStringLiteral("false")));
    reconcileAudioSpectrum();
    emit audioSpectrumDemandChanged(m_audioListenerDemand && !m_paused);
}

void WebRendererEngine::setVolume(float volume) {
    const float bounded = qBound(0.0f, volume, 1.0f);
    evaluate(QStringLiteral("window.__wr_setVolume(%1);").arg(bounded, 0, 'f', 4));
}

void WebRendererEngine::setMuted(bool muted) {
    m_muted = muted;
    evaluate(QStringLiteral("window.__wr_setMuted(%1);").arg(muted ? QStringLiteral("true") : QStringLiteral("false")));
}

void WebRendererEngine::setFrameRate(int fps) {
    const int bounded = qBound(1, fps, 240);
    m_captureTimer->setInterval(1000 / bounded);
    if (!m_captureTimer->isActive()) m_captureTimer->start();
}

void WebRendererEngine::setHostMediaPlaybackSuspended(bool suspended) {
    setPaused(suspended);
}

void WebRendererEngine::startAudioSpectrum() {
    m_audioSpectrumRequested = true;
    if (!m_audioDemandTimer->isActive()) m_audioDemandTimer->start();
    pollAudioDemand();
}

void WebRendererEngine::stopAudioSpectrum() {
    m_audioSpectrumRequested = false;
    m_audioDemandTimer->stop();
    m_audioTimer->stop();
    m_audioTap->stop();
    m_audioSpectrumStarted = false;
    m_audioSpectrumObserved = false;
}

void WebRendererEngine::pushAudioSpectrum(const std::array<float, 128>& spectrum) {
    if (!m_pageLoaded || !m_audioListenerDemand || m_paused) return;
    QJsonArray samples;
    for (float sample : spectrum) samples.append(static_cast<double>(sample));
    const QString json = QString::fromUtf8(QJsonDocument(samples).toJson(QJsonDocument::Compact));
    evaluate(QStringLiteral("window.__wr_pushAudio(%1);").arg(json));
}

void WebRendererEngine::pollAudioDemand() {
    if (!m_audioSpectrumRequested || !m_pageLoaded) return;
    // The shim owns this numeric field, so the callback consumes a known
    // protocol value instead of inspecting page-defined listener objects.
    m_view->page()->runJavaScript(QStringLiteral("window.__wr_audio_listener_count"),
                                  [this](const QVariant& result) {
        if (std::getenv("WR_DEBUG") != nullptr) {
            qInfo() << "WebRenderer: audio listener count query=" << result;
        }
        const bool needed = result.toInt() > 0;
        if (m_audioListenerDemand == needed) return;
        m_audioListenerDemand = needed;
        if (std::getenv("WR_DEBUG") != nullptr) {
            qInfo("WebRenderer: audio listener demand=%d", needed ? 1 : 0);
        }
        reconcileAudioSpectrum();
        emit audioSpectrumDemandChanged(needed && !m_paused);
    });
}

void WebRendererEngine::reconcileAudioSpectrum() {
    const bool needed = m_audioSpectrumRequested && m_config.enableAudioSpectrum &&
                        m_audioListenerDemand && !m_paused;
    if (!needed) {
        if (m_audioSpectrumStarted) {
            m_audioTimer->stop();
            m_audioTap->stop();
            m_audioSpectrumStarted = false;
            m_audioSpectrumObserved = false;
        }
        return;
    }
    if (m_audioSpectrumStarted) return;
    QString error;
    if (!m_audioTap->start(&error)) {
        qWarning("WebRenderer: audio spectrum disabled: %s", qPrintable(error));
        return;
    }
    m_audioSpectrumStarted = true;
    m_audioSpectrumObserved = false;
    if (std::getenv("WR_DEBUG") != nullptr) {
        qInfo("WebRenderer: audio spectrum capture started");
    }
    m_audioTimer->start();
}

void WebRendererEngine::tickAudioSpectrum() {
    std::array<float, 64> left {};
    std::array<float, 64> right {};
    if (!m_audioTap->copySpectrum(left, right)) return;
    if (!m_audioSpectrumObserved && std::getenv("WR_DEBUG") != nullptr) {
        qInfo("WebRenderer: audio spectrum first frame received");
    }
    m_audioSpectrumObserved = true;
    std::array<float, 128> spectrum {};
    for (std::size_t index = 0; index < left.size(); ++index) {
        spectrum[index] = left[index];
        spectrum[left.size() + index] = right[index];
    }
    pushAudioSpectrum(spectrum);
}

void WebRendererEngine::captureFrame() {
    if (m_paused || m_view->width() <= 0 || m_view->height() <= 0) return;
    const QPixmap pixmap = m_view->grab();
    if (!pixmap.isNull()) emit frameReady(pixmap.toImage().convertToFormat(QImage::Format_RGBA8888));
}

void WebRendererEngine::takeSnapshotToPath(const QString& path) {
    const QPixmap pixmap = m_view->grab();
    const bool ok = !pixmap.isNull() && pixmap.save(path);
    emit snapshotFinished(ok);
}

void WebRendererEngine::sendPointerMotion(float x, float y) {
    const QPointF position(x, y);
    QMouseEvent event(QEvent::MouseMove, position, position, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(m_view, &event);
}

void WebRendererEngine::sendPointerEnter(float x, float y) {
    const QPointF position(x, y);
    QEnterEvent event(position, position, position);
    QCoreApplication::sendEvent(m_view, &event);
}

void WebRendererEngine::sendPointerButton(float x, float y, uint32_t button, bool pressed) {
    Qt::MouseButton mouseButton = Qt::NoButton;
    if (button == 1u) mouseButton = Qt::LeftButton;
    else if (button == 2u) mouseButton = Qt::RightButton;
    else if (button == 3u) mouseButton = Qt::MiddleButton;
    if (mouseButton == Qt::NoButton) return;
    const QPointF position(x, y);
    QMouseEvent event(pressed ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease,
                      position, position, mouseButton, pressed ? mouseButton : Qt::NoButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(m_view, &event);
}

void WebRendererEngine::sendPointerAxis(float x, float y, float deltaX, float deltaY) {
    const QPointF position(x, y);
    QWheelEvent event(position, position, QPoint(), QPoint(static_cast<int>(deltaX), static_cast<int>(deltaY)),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(m_view, &event);
}
