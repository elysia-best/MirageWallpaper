#include "WallpaperManifest.h"
#include "WebRendererEngine.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QWebEngineSettings>
#include <QWebEngineView>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("wallpaper-dir"), QStringLiteral("Web wallpaper directory"));
    parser.process(app);
    if (parser.positionalArguments().size() != 1) return 1;

    QString error;
    const WRManifest manifest = WRManifest::loadFromDirectory(parser.positionalArguments().constFirst(), &error);
    if (manifest.workshopDirectory().isEmpty()) return 2;
    WebRendererEngine::Config config;
    WebRendererEngine engine(config);
    // WebRendererEngine defaults to off-screen capture for WebWallpaper.
    // The standalone viewer presents the same view in a regular Qt window, so
    // clear that capture-only attribute before show() creates the native surface.
    engine.view()->setAttribute(Qt::WA_DontShowOnScreen, false);
    engine.view()->resize(1280, 720);
    engine.view()->show();
    QObject::connect(&engine, &WebRendererEngine::failed, [](const QString& message) {
        qCritical("WebViewer: %s", qPrintable(message));
    });
    engine.openWallpaper(manifest);
    return app.exec();
}
