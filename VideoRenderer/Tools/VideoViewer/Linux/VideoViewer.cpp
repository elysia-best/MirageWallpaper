#include "VideoManifest.h"
#include "VideoRendererEngine.h"

#include <QApplication>
#include <QCoreApplication>
#include <QTimer>

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace {

struct ViewerArgs {
    const char* workshop = nullptr;
    int width = 1280;
    int height = 720;
    float volume = 1.0f;
    bool muted = false;
    int runSeconds = 0;
    VRVideoFillMode fillMode = VRVideoFillModeCover;
};

void printUsage(const char* argv0) {
    std::fprintf(stderr,
                 "Usage: %s <wallpaper-dir> [options]\n\n"
                 "Options:\n"
                 "  --width N              window width  (default 1280)\n"
                 "  --height N             window height (default 720)\n"
                 "  --volume 0..1          audio volume (default 1.0)\n"
                 "  --muted                start muted\n"
                 "  --fill MODE            cover | contain | stretch (default cover)\n"
                 "  --run-seconds N        exit after N seconds (test helper)\n"
                 "  -h, --help             show this help\n",
                 argv0);
}

const char* takeValue(int& index, int argc, char** argv, const char* option) {
    if (index + 1 >= argc) {
        std::fprintf(stderr, "%s requires a value\n", option);
        return nullptr;
    }
    return argv[++index];
}

bool parseArgs(int argc, char** argv, ViewerArgs& out) {
    for (int index = 1; index < argc; ++index) {
        const char* argument = argv[index];
        if (std::strcmp(argument, "-h") == 0 || std::strcmp(argument, "--help") == 0) {
            printUsage(argv[0]);
            std::exit(0);
        }
        if (std::strcmp(argument, "--width") == 0) {
            const char* value = takeValue(index, argc, argv, argument);
            if (!value) return false;
            out.width = std::atoi(value);
        } else if (std::strcmp(argument, "--height") == 0) {
            const char* value = takeValue(index, argc, argv, argument);
            if (!value) return false;
            out.height = std::atoi(value);
        } else if (std::strcmp(argument, "--volume") == 0) {
            const char* value = takeValue(index, argc, argv, argument);
            if (!value) return false;
            out.volume = std::strtof(value, nullptr);
        } else if (std::strcmp(argument, "--muted") == 0) {
            out.muted = true;
        } else if (std::strcmp(argument, "--fill") == 0) {
            const char* value = takeValue(index, argc, argv, argument);
            if (!value || !VRParseVideoFillMode(value, out.fillMode)) return false;
        } else if (std::strcmp(argument, "--run-seconds") == 0) {
            const char* value = takeValue(index, argc, argv, argument);
            if (!value) return false;
            out.runSeconds = std::atoi(value);
        } else if (argument[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", argument);
            return false;
        } else if (!out.workshop) {
            out.workshop = argument;
        } else {
            std::fprintf(stderr, "unexpected positional argument: %s\n", argument);
            return false;
        }
    }

    if (!out.workshop) {
        printUsage(argv[0]);
        return false;
    }
    if (out.width < 64) out.width = 64;
    if (out.height < 64) out.height = 64;
    out.volume = VRClampVideoVolume(out.volume);
    if (out.runSeconds < 0) out.runSeconds = 0;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_MEDIA_BACKEND")) {
        qputenv("QT_MEDIA_BACKEND", "ffmpeg");
    }

    ViewerArgs args;
    if (!parseArgs(argc, argv, args)) return 1;

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("VideoViewer"));

    QString manifestError;
    const auto manifest = VRVideoManifest::loadFromDirectory(
        QString::fromLocal8Bit(args.workshop), &manifestError);
    if (!manifest) {
        std::fprintf(stderr, "VideoViewer: %s\n", manifestError.toLocal8Bit().constData());
        return 2;
    }

    VRVideoEngineConfig config = VRVideoRendererEngine::defaultConfig();
    config.fillMode = args.fillMode;
    config.initialVolume = args.volume;
    config.muted = args.muted;
    config.autoplay = true;

    VRVideoRendererEngine engine(config);
    QString openError;
    if (!engine.openWallpaper(*manifest, &openError)) {
        std::fprintf(stderr, "VideoViewer: %s\n", openError.toLocal8Bit().constData());
        return 3;
    }
    engine.setWindowTitle(manifest->title());
    engine.resize(args.width, args.height);
    QObject::connect(&engine, &VRVideoRendererEngine::playbackError, &app,
                     [&app](const QString& message) {
                         std::fprintf(stderr, "VideoViewer: %s\n", message.toLocal8Bit().constData());
                         app.exit(3);
                     });
    engine.show();

    if (args.runSeconds > 0) {
        QTimer::singleShot(args.runSeconds * 1000, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
