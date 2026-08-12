#pragma once

#include "Services/SteamWebAPI.h"
#include "Services/WallpaperLibrary.h"

#include <QObject>
#include <QFutureWatcher>
#include <QHash>
#include <QSet>
#include <QTimer>

#include <optional>

namespace Mirage {

class SteamServiceManager;

enum class DiscoverCollection {
    Trending,
    MostRecent,
    MostSubscribed,
    TopRated,
    MostUpvoted,
    LastUpdated,
    PlaytimeTrend,
    AveragePlaytimeTrend,
    SessionsTrend,
    TotalPlaytime,
    LifetimeAveragePlaytime,
    LifetimeSessions,
    Anime,
    Nature,
    Abstract,
    Landscape,
};

enum class SteamSetupState {
    NeedsLogin,  // Steam 未登录或服务不可用
    Ready,       // 已登录，可下载
};

struct InstalledWorkshopState {
    QSet<QString> installedIds;
    QSet<QString> presetsNeedingDependency;
};

class WorkshopViewModel : public QObject {
    Q_OBJECT

public:
    explicit WorkshopViewModel(SteamWebAPI* api,
                               SteamServiceManager* service,
                               WallpaperLibrary* library,
                               QObject* parent = nullptr);

    const QVector<WorkshopItem>& items() const;
    const QVector<WorkshopItem>& discoverItems(DiscoverCollection collection) const;
    const QVector<WorkshopItem>& bannerItems() const;
    const QVector<WorkshopDownloadTask>& downloadQueue() const;
    const std::optional<WorkshopItem>& selectedItem() const;

    QString searchText() const;
    const QSet<QString>& selectedTags() const;
    WorkshopSortOrder sortOrder() const;
    WorkshopTypeFilter typeFilter() const;
    int ageRatingMask() const;
    int trendDays() const;
    int discoverTrendDays() const;
    int currentPage() const;
    int totalPages() const;
    bool isLoading() const;
    bool isDiscoverLoading() const;
    QString error() const;
    SteamSetupState steamSetupState() const;
    QString steamSetupSummary() const;
    int activeDownloadCount() const;

    bool isItemDownloaded(const QString& workshopId) const;
    bool presetNeedsDependency(const QString& workshopId) const;
    std::optional<Wallpaper> installedItem(const QString& workshopId) const;
    std::optional<DownloadState> downloadStateFor(const QString& workshopId) const;

    // 订阅管理（对齐上游 WorkshopViewModel.swift）。
    const QVector<WorkshopSubscription>& subscriptions() const;
    int subscriptionTotal() const;
    int subscriptionStartIndex() const;
    bool isSubscriptionsLoading() const;
    bool hasMoreSubscriptions() const;

public slots:
    void setSearchText(const QString& text);
    void submitSearch();
    void setSortOrder(Mirage::WorkshopSortOrder order);
    void setTrendDays(int days);
    void setDiscoverTrendDays(int days);
    void setTypeFilter(Mirage::WorkshopTypeFilter filter);
    void setAgeRatingEnabled(Mirage::WorkshopAgeRating rating, bool enabled);
    void toggleTag(const QString& tag);
    void selectAllTags();
    void clearTags();
    void clearFilters();
    void loadPreviousPage();
    void loadNextPage();
    void search();
    void loadDiscover();
    void refreshDiscover();
    void reloadOnlineContent();
    void checkSteamSetup();
    void logout();

    // 订阅管理。
    void loadSubscriptions(int startIndex = 0);
    void loadNextSubscriptionsPage();
    void subscribe(const QString& workshopId);
    void unsubscribe(const QString& workshopId);

    void selectWorkshopItem(const Mirage::WorkshopItem& item);
    void downloadItem(const Mirage::WorkshopItem& item,
                      Mirage::DownloadPurpose purpose = Mirage::DownloadPurpose::Wallpaper);
    // 仅凭 workshopId 下载（订阅列表等无完整详情场景）。
    void downloadItemById(const QString& workshopId);
    void cancelDownload(const QString& workshopId);
    void retryDownload(const QString& workshopId);
    void clearCompletedDownloads();
    void requestPresetDependency(const QString& workshopId);
    void confirmPresetDependencyDownload(const QString& presetId,
                                         const QString& dependencyId,
                                         const Mirage::WorkshopItem& dependencyItem);

    void navigateToWorkshopWithTag(const QString& tag);
    void navigateToWorkshopWithSort(Mirage::WorkshopSortOrder order);

signals:
    void browseChanged();
    void discoverChanged();
    void filtersChanged();
    void selectedItemChanged(const Mirage::WorkshopItem& item, bool hasSelection);
    void installedWallpaperRequested(const Mirage::Wallpaper& wallpaper);
    void presetDependencyRequested(const QString& presetId,
                                   const QString& presetTitle,
                                   const QString& dependencyId,
                                   const Mirage::WorkshopItem& dependencyItem);
    void downloadQueueChanged();
    void installedStateChanged();
    void steamSetupChanged();
    void steamSetupRequested();
    void navigateToWorkshopRequested();
    void workshopItemDownloaded(const QString& workshopId);
    void subscriptionsChanged();

private:
    struct PendingDependency {
        QString presetId;
        QString presetTitle;
        QString dependencyId;
    };

    void handleQueryFinished(quint64 requestId, const WorkshopQueryResult& result);
    void handleDetailsFinished(quint64 requestId,
                               const QVector<WorkshopItem>& items,
                               const QString& error);
    // SteamServiceManager::downloadStateChanged 信号（taskId=workshopId）。
    void handleDownloadState(const QString& taskId, DownloadStateKind kind,
                             qint64 receivedBytes, qint64 totalBytes, double bytesPerSecond,
                             const QString& outputPath, const QString& message);
    void refreshSteamSetupState();
    void processDownloadQueue();
    void handleCompletedDownload(const QString& workshopId);
    void refreshInstalledState();
    void cancelDiscoverRequests();
    void setSelectedItem(const std::optional<WorkshopItem>& item);
    void issueDiscoverRequest(DiscoverCollection collection,
                              WorkshopSortOrder order,
                              const QString& tag,
                              int count,
                              int trendDays = 7);

    SteamWebAPI* m_api = nullptr;
    SteamServiceManager* m_steamService = nullptr;
    WallpaperLibrary* m_library = nullptr;

    QVector<WorkshopItem> m_items;
    QHash<DiscoverCollection, QVector<WorkshopItem>> m_discoverItems;
    QHash<DiscoverCollection, QVector<WorkshopItem>> m_pendingDiscoverItems;
    QVector<WorkshopItem> m_bannerItems;
    QVector<WorkshopDownloadTask> m_downloadQueue;
    std::optional<WorkshopItem> m_selectedItem;

    QString m_searchText;
    QSet<QString> m_selectedTags;
    WorkshopSortOrder m_sortOrder = WorkshopSortOrder::Trending;
    WorkshopTypeFilter m_typeFilter = WorkshopTypeFilter::All;
    int m_ageRatingMask = 1;
    int m_trendDays = 7;
    int m_discoverTrendDays = 7;
    int m_currentPage = 1;
    int m_totalItems = 0;
    bool m_isLoading = false;
    bool m_isDiscoverLoading = false;
    QString m_error;
    SteamSetupState m_steamSetupState = SteamSetupState::NeedsLogin;

    // 订阅状态。
    QVector<WorkshopSubscription> m_subscriptions;
    int m_subscriptionTotal = 0;
    int m_subscriptionStartIndex = 0;
    bool m_subscriptionsLoading = false;

    QTimer m_searchDebounce;
    QFutureWatcher<InstalledWorkshopState> m_installedStateWatcher;
    QSet<QString> m_installedWorkshopIds;
    QSet<QString> m_presetsNeedingDependency;
    bool m_installedStateRefreshPending = false;
    quint64 m_searchRequestId = 0;
    QHash<quint64, DiscoverCollection> m_discoverRequests;
    QHash<quint64, PendingDependency> m_dependencyRequests;
    QString m_pendingPresetId;
    QString m_pendingDependencyId;
};

} // namespace Mirage

Q_DECLARE_METATYPE(Mirage::WorkshopSortOrder)
Q_DECLARE_METATYPE(Mirage::WorkshopTypeFilter)
