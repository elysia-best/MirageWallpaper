#pragma once

#include <QJsonObject>
#include <QString>
#include <QUrl>

#include <optional>

class VRVideoManifest {
public:
    [[nodiscard]] static std::optional<VRVideoManifest> loadFromDirectory(
        const QString& directory, QString* error = nullptr);

    [[nodiscard]] const QString& wallpaperDirectory() const noexcept { return m_wallpaperDirectory; }
    [[nodiscard]] const QString& title() const noexcept { return m_title; }
    [[nodiscard]] const QString& preview() const noexcept { return m_preview; }
    [[nodiscard]] const QString& videoFile() const noexcept { return m_videoFile; }
    [[nodiscard]] const QUrl& videoUrl() const noexcept { return m_videoUrl; }
    [[nodiscard]] const QJsonObject& userProperties() const noexcept { return m_userProperties; }

private:
    QString m_wallpaperDirectory;
    QString m_title;
    QString m_preview;
    QString m_videoFile;
    QUrl m_videoUrl;
    QJsonObject m_userProperties;
};
