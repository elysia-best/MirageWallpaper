#include "ControlChannel.h"
#include "WallpaperManifest.h"
#include "WebRendererEngine.h"
#include "ProtocolWebRenderer.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QMetaObject>
#include <QJsonObject>
#include <QTimer>
#include <QWebEngineView>

#include <cstdio>
#include <utility>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("display-output-id"), QStringLiteral("mirage-display output id"), QStringLiteral("id")});
    parser.addOption({QStringLiteral("display-socket"), QStringLiteral("mirage-display broker socket"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("fps"), QStringLiteral("target frame rate"), QStringLiteral("fps"), QStringLiteral("60")});
    parser.addOption({QStringLiteral("muted"), QStringLiteral("start muted")});
    parser.addOption({QStringLiteral("control-stdin"), QStringLiteral("accept JSON commands")});
    parser.addPositionalArgument(QStringLiteral("wallpaper-dir"), QStringLiteral("Web wallpaper directory"));
    parser.process(app);
    if (parser.positionalArguments().size() != 1) return 1;
    if (!parser.isSet(QStringLiteral("display-output-id")) || !parser.isSet(QStringLiteral("display-socket"))) {
        std::fprintf(stderr, "WebWallpaper: --display-output-id and --display-socket are required\n");
        return 1;
    }

    QString error;
    const WRManifest manifest = WRManifest::loadFromDirectory(parser.positionalArguments().constFirst(), &error);
    if (manifest.workshopDirectory().isEmpty()) {
        std::fprintf(stderr, "WebWallpaper: %s\n", qPrintable(error));
        return 2;
    }
    WebRendererEngine::Config config;
    config.frameRate = parser.value(QStringLiteral("fps")).toInt();
    WebRendererEngine engine(config);
    engine.view()->resize(1920, 1080);
    ProtocolWebRenderer::Config protocolConfig {
        .socketPath = parser.value(QStringLiteral("display-socket")),
        .outputId = parser.value(QStringLiteral("display-output-id")),
        .pointerEnter = [&engine](float x, float y) {
            QMetaObject::invokeMethod(&engine, [&, x, y] { engine.sendPointerEnter(x, y); }, Qt::QueuedConnection);
        },
        .pointerMotion = [&engine](float x, float y) {
            QMetaObject::invokeMethod(&engine, [&, x, y] { engine.sendPointerMotion(x, y); }, Qt::QueuedConnection);
        },
        .pointerButton = [&engine](float x, float y, uint32_t button, bool pressed) {
            QMetaObject::invokeMethod(&engine, [&, x, y, button, pressed] { engine.sendPointerButton(x, y, button, pressed); }, Qt::QueuedConnection);
        },
        .pointerAxis = [&engine](float x, float y, float dx, float dy) {
            QMetaObject::invokeMethod(&engine, [&, x, y, dx, dy] { engine.sendPointerAxis(x, y, dx, dy); }, Qt::QueuedConnection);
        },
        .outputSizeChanged = [&engine](uint32_t width, uint32_t height) {
            QMetaObject::invokeMethod(&engine, [&, width, height] {
                engine.view()->resize(static_cast<int>(width), static_cast<int>(height));
            }, Qt::QueuedConnection);
        },
    };
    ProtocolWebRenderer protocol(std::move(protocolConfig), &engine);
    QObject::connect(&engine, &WebRendererEngine::frameReady, &protocol,
                     &ProtocolWebRenderer::submitFrame, Qt::QueuedConnection);
    QString protocolError;
    if (!protocol.start(&protocolError)) {
        std::fprintf(stderr, "WebWallpaper: %s\n", qPrintable(protocolError));
        return 3;
    }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &protocol, &ProtocolWebRenderer::stop);
    engine.openWallpaper(manifest);
    engine.setMuted(parser.isSet(QStringLiteral("muted")));
    QObject::connect(&engine, &WebRendererEngine::contentReady, [] {
        std::fprintf(stdout, "{\"event\":\"content-ready\"}\n");
        std::fflush(stdout);
    });
    QObject::connect(&engine, &WebRendererEngine::failed, [](const QString& message) {
        std::fprintf(stderr, "WebWallpaper: %s\n", qPrintable(message));
    });
    ControlChannel channel([&engine](const QJsonObject& command) {
        const QString name = command.value(QStringLiteral("cmd")).toString();
        if (name == QStringLiteral("pause")) engine.setPaused(true);
        else if (name == QStringLiteral("resume") || name == QStringLiteral("play")) engine.setPaused(false);
        else if (name == QStringLiteral("muted")) engine.setMuted(command.value(QStringLiteral("value")).toBool());
        else if (name == QStringLiteral("volume")) engine.setVolume(static_cast<float>(command.value(QStringLiteral("value")).toDouble()));
        else if (name == QStringLiteral("fps")) engine.setFrameRate(command.value(QStringLiteral("value")).toInt());
        else if (name == QStringLiteral("setProperty")) engine.applyUserProperty(command.value(QStringLiteral("key")).toString(), command.value(QStringLiteral("value")));
        else if (name == QStringLiteral("snapshot")) engine.takeSnapshotToPath(command.value(QStringLiteral("path")).toString());
    }, [&app] { app.quit(); }, &app);
    if (parser.isSet(QStringLiteral("control-stdin"))) channel.start();
    return app.exec();
}
