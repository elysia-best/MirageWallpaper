#include "VideoManifest.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>

namespace {

const QSet<QString>& videoExtensions() {
    static const QSet<QString> extensions {
        QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("mov"),
        QStringLiteral("qt"), QStringLiteral("avi"), QStringLiteral("mkv"),
        QStringLiteral("webm"), QStringLiteral("mpg"), QStringLiteral("mpeg"),
    };
    return extensions;
}

QString findFirstVideoFile(const QDir& directory) {
    const QFileInfoList children = directory.entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo& child : children) {
        if (videoExtensions().contains(child.suffix().toLower())) return child.fileName();
    }
    return {};
}

void setError(QString* error, const QString& message) {
    if (error) *error = message;
}

} // namespace

std::optional<VRVideoManifest> VRVideoManifest::loadFromDirectory(
    const QString& inputDirectory, QString* error) {
    const QFileInfo directoryInfo(QDir::cleanPath(inputDirectory));
    if (!directoryInfo.exists() || !directoryInfo.isDir()) {
        setError(error, QStringLiteral("wallpaper directory not found: %1").arg(inputDirectory));
        return std::nullopt;
    }

    const QString directoryPath = directoryInfo.absoluteFilePath();
    const QDir directory(directoryPath);
    const QString projectPath = directory.filePath(QStringLiteral("project.json"));
    QFile projectFile(projectPath);
    if (!projectFile.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("cannot open %1: %2").arg(projectPath, projectFile.errorString()));
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(projectFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("invalid project.json: %1: %2")
                            .arg(projectPath, parseError.errorString()));
        return std::nullopt;
    }

    const QJsonObject json = document.object();
    const QString type = json.value(QStringLiteral("type")).toString().toLower();
    if (type != QStringLiteral("video")) {
        setError(error, QStringLiteral("project.json type is '%1', expected 'video'")
                            .arg(type.isEmpty() ? QStringLiteral("<missing>") : type));
        return std::nullopt;
    }

    QString videoFile = json.value(QStringLiteral("file")).toString();
    if (videoFile.isEmpty()) videoFile = findFirstVideoFile(directory);
    if (videoFile.isEmpty()) {
        setError(error, QStringLiteral("video wallpaper has no playable file entry"));
        return std::nullopt;
    }

    const QFileInfo videoInfo(QDir::cleanPath(directory.filePath(videoFile)));
    if (!videoInfo.exists() || !videoInfo.isFile()) {
        setError(error, QStringLiteral("video file not found: %1").arg(videoInfo.absoluteFilePath()));
        return std::nullopt;
    }

    VRVideoManifest manifest;
    manifest.m_wallpaperDirectory = directoryPath;
    manifest.m_title = json.value(QStringLiteral("title")).toString();
    if (manifest.m_title.isEmpty()) {
        manifest.m_title = directoryInfo.fileName().isEmpty()
            ? QStringLiteral("VideoWallpaper")
            : directoryInfo.fileName();
    }
    manifest.m_preview = json.value(QStringLiteral("preview")).toString();
    manifest.m_videoFile = videoFile;
    manifest.m_videoUrl = QUrl::fromLocalFile(videoInfo.absoluteFilePath());

    const QJsonObject general = json.value(QStringLiteral("general")).toObject();
    manifest.m_userProperties = general.value(QStringLiteral("properties")).toObject();
    return manifest;
}
