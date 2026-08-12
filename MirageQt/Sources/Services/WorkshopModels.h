#pragma once

#include "Services/WEProject.h"

#include <QDateTime>
#include <QStringList>
#include <QUrl>
#include <QVector>

namespace Mirage {

struct WorkshopItem {
    QString publishedFileId;
    QString title;
    QString description;
    QUrl previewImageUrl;
    QStringList tags;
    int subscriptions = 0;
    int favorited = 0;
    int views = 0;
    qint64 fileSize = 0;
    QDateTime timeCreated;
    QDateTime timeUpdated;
    QString creatorSteamId;
    QString wallpaperType = QStringLiteral("scene");
    QString ageRating;

    WallpaperKind kind() const;
    bool isPreset() const;
    QString displayTypeName() const;
    QString formattedFileSize() const;
    QString formattedSubscriptions() const;
    QString formattedFavorited() const;
    QString formattedViews() const;

    static WorkshopItem dependencyPlaceholder(const QString& id);
};

enum class WorkshopSortOrder {
    Trending,
    MostRecent,
    MostSubscribed,
    TopRated,
    MostUpvoted,
    PlaytimeTrend,
    TotalPlaytime,
    AveragePlaytimeTrend,
    LifetimeAveragePlaytime,
    SessionsTrend,
    LifetimeSessions,
    LastUpdated,
};

enum class WorkshopAgeRating {
    Everyone,
    Questionable,
    Mature,
};

enum class WorkshopTrendPeriod {
    Day = 1,
    Week = 7,
    Month = 30,
    ThreeMonths = 90,
    SixMonths = 180,
    Year = 365,
};

enum class WorkshopTypeFilter {
    All,
    Scene,
    Web,
    Video,
    Preset,
};

struct WorkshopTag {
    QString value;
    QString displayName;
    QString iconName;
};

struct WorkshopQuery {
    QString searchText;
    QStringList tags;
    WorkshopSortOrder sortOrder = WorkshopSortOrder::Trending;
    WorkshopTypeFilter typeFilter = WorkshopTypeFilter::All;
    int trendDays = 7;
    int ageRatingMask = 1;
    int page = 1;
    int perPage = 30;
};

struct WorkshopQueryResult {
    QVector<WorkshopItem> items;
    int total = 0;
    QString error;
};

enum class DownloadPurpose {
    Wallpaper,
    PresetDependency,
};

// 下载状态（对齐 SteamService/WorkshopDownloader 的状态机）。
// connecting/downloading → SteamService 正在下载；resolving/validating →
// 已下载完成、正在校验展开；completed 时 outputPath 指向项目目录。
enum class DownloadStateKind {
    Queued,
    Starting,
    Connecting,
    Downloading,
    Resolving,
    Completed,
    Failed,
    Cancelled,
};

struct DownloadState {
    DownloadStateKind kind = DownloadStateKind::Queued;
    double percent = -1.0;
    QString message;
    qint64 bytesReceived = 0;
    qint64 totalBytes = 0;
    double bytesPerSecond = 0.0;
    QString outputPath;
};

struct WorkshopDownloadTask {
    WorkshopItem workshopItem;
    DownloadState state;
    QDateTime startedAt;
    QDateTime completedAt;
    DownloadPurpose purpose = DownloadPurpose::Wallpaper;
};

// Steam Workshop 订阅记录（对齐 WorkshopModels.swift 的 WorkshopSubscription）。
struct WorkshopSubscription {
    QString publishedFileId;
    qint64 subscribedAt = 0;  // Unix 秒
    qint64 updatedAt = 0;     // Unix 秒
    QString contentHash;
    qint64 fileSize = 0;
};

struct WorkshopSubscriptionPage {
    int total = 0;
    int startIndex = 0;
    QVector<WorkshopSubscription> items;
};

QString workshopSortLabel(WorkshopSortOrder order);
bool workshopSortUsesTrendPeriod(WorkshopSortOrder order);
QString workshopAgeRatingLabel(WorkshopAgeRating rating);
QString workshopAgeRatingTag(WorkshopAgeRating rating);
QString workshopTrendPeriodLabel(WorkshopTrendPeriod period);
QString workshopTypeLabel(WorkshopTypeFilter filter);
QVector<WorkshopTag> workshopTags();

} // namespace Mirage

Q_DECLARE_METATYPE(Mirage::WorkshopItem)
Q_DECLARE_METATYPE(Mirage::WorkshopQueryResult)
Q_DECLARE_METATYPE(Mirage::WorkshopDownloadTask)
