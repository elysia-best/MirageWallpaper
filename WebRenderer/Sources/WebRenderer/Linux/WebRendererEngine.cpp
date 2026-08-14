#include "WebRendererEngine.h"

#include "WallpaperManifest.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
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
  window.wallpaperRegisterAudioListener=function(callback){
    window.__mirage_audio_listener=callback;
    return callback;
  };
  window.__wr_applyProps=function(properties){
    if(window.wallpaperPropertyListener&&typeof window.wallpaperPropertyListener.applyUserProperties==='function')
      window.wallpaperPropertyListener.applyUserProperties(properties||{});
  };
  window.__wr_setPaused=function(paused){
    window.__mirage_paused=!!paused;
    if(window.wallpaperPropertyListener&&typeof window.wallpaperPropertyListener.setPaused==='function')
      window.wallpaperPropertyListener.setPaused(!!paused);
  };
  window.__wr_setVolume=function(volume){
    window.__mirage_volume=Math.max(0,Math.min(1,Number(volume)||0));
    document.querySelectorAll('audio,video').forEach(function(media){media.volume=window.__mirage_volume;});
  };
  window.__wr_setFps=function(fps){window.__mirage_fps=Math.max(1,Number(fps)||60);};
})();
)JS");
}

} // namespace

WebRendererEngine::WebRendererEngine(const Config& config, QObject* parent)
    : QObject(parent), m_config(config), m_view(new QWebEngineView) {
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
        emit contentReady();
    });
    m_captureTimer = new QTimer(this);
    connect(m_captureTimer, &QTimer::timeout, this, &WebRendererEngine::captureFrame);
    setFrameRate(m_config.frameRate);
}

WebRendererEngine::~WebRendererEngine() {
    delete m_view;
}

void WebRendererEngine::openWallpaper(const WRManifest& manifest) {
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
    evaluate(QStringLiteral("window.__wr_setPaused(%1);document.querySelectorAll('video,audio').forEach(function(x){var p=x.%2();if(p&&p.catch)p.catch(function(){});});")
                 .arg(paused ? QStringLiteral("true") : QStringLiteral("false"),
                      paused ? QStringLiteral("pause") : QStringLiteral("play")));
}

void WebRendererEngine::setVolume(float volume) {
    const float bounded = qBound(0.0f, volume, 1.0f);
    evaluate(QStringLiteral("window.__wr_setVolume(%1);").arg(bounded, 0, 'f', 4));
}

void WebRendererEngine::setMuted(bool muted) {
    m_muted = muted;
    evaluate(QStringLiteral("document.querySelectorAll('video,audio').forEach(function(x){x.muted=%1;});").arg(muted ? QStringLiteral("true") : QStringLiteral("false")));
}

void WebRendererEngine::setFrameRate(int fps) {
    const int bounded = qBound(1, fps, 240);
    m_captureTimer->setInterval(1000 / bounded);
    if (!m_captureTimer->isActive()) m_captureTimer->start();
}

void WebRendererEngine::setHostMediaPlaybackSuspended(bool suspended) {
    setPaused(suspended);
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
