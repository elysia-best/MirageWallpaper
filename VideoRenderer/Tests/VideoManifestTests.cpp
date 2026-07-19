#include "VideoManifest.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdio>

namespace {

bool writeFile(const QString& path, const QByteArray& contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(contents) == contents.size();
}

bool writeProject(const QString& directory, const QJsonObject& object) {
    return writeFile(QDir(directory).filePath(QStringLiteral("project.json")),
                     QJsonDocument(object).toJson(QJsonDocument::Compact));
}

bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "VideoManifestTests: %s\n", message);
    return condition;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;

    bool passed = true;
    QString error;
    passed &= expect(!VRVideoManifest::loadFromDirectory(
                         QDir(temporary.path()).filePath(QStringLiteral("missing")), &error),
                     "missing directory rejected");

    const QString projectPath = QDir(temporary.path()).filePath(QStringLiteral("project.json"));
    passed &= expect(writeFile(projectPath, QByteArrayLiteral("{")), "write invalid project");
    error.clear();
    passed &= expect(!VRVideoManifest::loadFromDirectory(temporary.path(), &error),
                     "invalid JSON rejected");

    passed &= expect(writeProject(temporary.path(), {{QStringLiteral("type"), QStringLiteral("scene")}}),
                     "write wrong type project");
    error.clear();
    passed &= expect(!VRVideoManifest::loadFromDirectory(temporary.path(), &error),
                     "wrong wallpaper type rejected");

    passed &= expect(writeProject(temporary.path(), {
                         {QStringLiteral("type"), QStringLiteral("video")},
                         {QStringLiteral("file"), QStringLiteral("missing.mp4")},
                     }), "write missing video project");
    error.clear();
    passed &= expect(!VRVideoManifest::loadFromDirectory(temporary.path(), &error),
                     "missing video rejected");

    const QString videoPath = QDir(temporary.path()).filePath(QStringLiteral("fallback.MKV"));
    passed &= expect(writeFile(videoPath, QByteArrayLiteral("test")), "write fallback video");
    passed &= expect(writeProject(temporary.path(), {
                         {QStringLiteral("type"), QStringLiteral("video")},
                         {QStringLiteral("title"), QStringLiteral("Manifest Test")},
                     }), "write valid project");
    error.clear();
    const auto manifest = VRVideoManifest::loadFromDirectory(temporary.path(), &error);
    passed &= expect(manifest.has_value(), "valid manifest loaded");
    if (manifest) {
        passed &= expect(manifest->title() == QStringLiteral("Manifest Test"), "title loaded");
        passed &= expect(manifest->videoFile() == QStringLiteral("fallback.MKV"),
                         "extension fallback loaded");
        passed &= expect(manifest->videoUrl().toLocalFile() == videoPath, "video URL resolved");
    }
    return passed ? 0 : 1;
}
