#pragma once

#include "Services/FavoritesManager.h"
#include "Services/GlobalSettingsService.h"
#include "Services/PlaybackController.h"
#include "Services/PlaylistManager.h"
#include "Services/PlaylistModels.h"
#include "Services/RendererController.h"
#include "Services/SteamCMDManager.h"
#include "Services/SteamSetupViewModel.h"
#include "Services/SteamWebAPI.h"
#include "Services/TrustedWallpaperService.h"
#include "Services/WallpaperLibrary.h"
#include "Services/WallpaperRuntimeStore.h"
#include "Services/WorkshopModels.h"
#include "Services/WorkshopViewModel.h"

#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

namespace Mirage {

// 序列化辅助（定义在 MirageControllerMapping.cpp）：
// MirageController.cpp 的 downloadQueue()/updatePlaylistSettings()/workshopItem()
// 与 Mapping 的序列化成员函数共享同一份实现。
QString downloadStateKey(DownloadStateKind kind);
bool isActiveDownload(DownloadStateKind kind);
void updatePlaylistSettingsFromMap(PlaylistSettings& settings, const QVariantMap& values);
// 发现页区块定义：集合 + 展示标题（inline 定义使所有使用方共享同一数组）。
struct DiscoverSectionDefinition {
    DiscoverCollection collection;
    const char* title;
};
inline const DiscoverSectionDefinition kDiscoverSections[] = {
    {DiscoverCollection::Trending, "本周最热"},
    {DiscoverCollection::MostUpvoted, "最多投票"},
    {DiscoverCollection::MostRecent, "最新上架"},
    {DiscoverCollection::MostSubscribed, "订阅最多"},
    {DiscoverCollection::TopRated, "评分最高"},
    {DiscoverCollection::LastUpdated, "最近更新"},
    {DiscoverCollection::PlaytimeTrend, "本周播放时长最多"},
    {DiscoverCollection::AveragePlaytimeTrend, "本周平均播放时长最长"},
    {DiscoverCollection::SessionsTrend, "本周播放次数最多"},
    {DiscoverCollection::TotalPlaytime, "总播放时长最多"},
    {DiscoverCollection::LifetimeAveragePlaytime, "终身平均播放时长"},
    {DiscoverCollection::LifetimeSessions, "总播放次数最多"},
    {DiscoverCollection::Anime, "动漫精选"},
    {DiscoverCollection::Nature, "自然风光"},
    {DiscoverCollection::Abstract, "抽象艺术"},
    {DiscoverCollection::Landscape, "风景壁纸"},
};

class MirageController : public QObject {
    Q_OBJECT

    friend class PlaybackController;

    Q_PROPERTY(QVariantList wallpapers READ wallpapers NOTIFY wallpapersChanged)
    Q_PROPERTY(QVariantMap selectedWallpaper READ selectedWallpaper NOTIFY selectedWallpaperChanged)
    Q_PROPERTY(QString selectedWallpaperId READ selectedWallpaperId WRITE selectWallpaper NOTIFY selectedWallpaperChanged)
    Q_PROPERTY(QVariantList playlistItems READ playlistItems NOTIFY playlistChanged)
    Q_PROPERTY(QVariantList workshopItems READ workshopItems NOTIFY workshopItemsChanged)
    Q_PROPERTY(QVariantList discoverSections READ discoverSections NOTIFY discoverChanged)
    Q_PROPERTY(QVariantMap selectedWorkshopItem READ selectedWorkshopItem NOTIFY selectedWorkshopItemChanged)
    Q_PROPERTY(bool workshopLoading READ workshopLoading NOTIFY workshopStateChanged)
    Q_PROPERTY(bool discoverLoading READ discoverLoading NOTIFY discoverChanged)
    Q_PROPERTY(QString workshopError READ workshopError NOTIFY workshopStateChanged)
    Q_PROPERTY(int workshopPage READ workshopPage NOTIFY workshopStateChanged)
    Q_PROPERTY(int workshopPageCount READ workshopPageCount NOTIFY workshopStateChanged)
    Q_PROPERTY(int activeDownloadCount READ activeDownloadCount NOTIFY workshopStateChanged)
    Q_PROPERTY(QVariantList downloadQueue READ downloadQueue NOTIFY workshopStateChanged)
    Q_PROPERTY(bool steamReady READ steamReady NOTIFY workshopStateChanged)
    Q_PROPERTY(QString steamSetupSummary READ steamSetupSummary NOTIFY workshopStateChanged)
    Q_PROPERTY(QString steamCMDPath READ steamCMDPath NOTIFY steamChanged)
    Q_PROPERTY(QString steamUsername READ steamUsername NOTIFY steamChanged)
    Q_PROPERTY(bool steamLoggedIn READ steamLoggedIn NOTIFY steamChanged)
    Q_PROPERTY(QString steamInstallState READ steamInstallState NOTIFY steamChanged)
    Q_PROPERTY(double steamInstallProgress READ steamInstallProgress NOTIFY steamChanged)
    Q_PROPERTY(QString steamInstallMessage READ steamInstallMessage NOTIFY steamChanged)
    Q_PROPERTY(QString steamLoginState READ steamLoginState NOTIFY steamChanged)
    Q_PROPERTY(QString steamLoginMessage READ steamLoginMessage NOTIFY steamChanged)
    Q_PROPERTY(QStringList steamLoginLog READ steamLoginLog NOTIFY steamChanged)
    Q_PROPERTY(QString steamGuardType READ steamGuardType NOTIFY steamChanged)
    Q_PROPERTY(bool steamSessionReusable READ steamSessionReusable NOTIFY steamChanged)
    Q_PROPERTY(bool firstLaunch READ firstLaunch NOTIFY firstLaunchChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList selectedProperties READ selectedProperties NOTIFY selectedRuntimeChanged)
    Q_PROPERTY(int playlistScreen READ playlistScreen WRITE setPlaylistScreen NOTIFY playlistChanged)
    Q_PROPERTY(int screenCount READ screenCount NOTIFY displaysChanged)
    Q_PROPERTY(QVariantList displays READ displays NOTIFY displaysChanged)
    Q_PROPERTY(QVariantList savedPlaylists READ savedPlaylists NOTIFY playlistsSavedChanged)
    Q_PROPERTY(QVariantMap playlistSettings READ playlistSettings NOTIFY playlistChanged)
    Q_PROPERTY(double selectedVolume READ selectedVolume WRITE setSelectedVolume NOTIFY selectedRuntimeChanged)
    Q_PROPERTY(double selectedSpeed READ selectedSpeed WRITE setSelectedSpeed NOTIFY selectedRuntimeChanged)
    Q_PROPERTY(QString selectedFillMode READ selectedFillMode WRITE setSelectedFillMode NOTIFY selectedRuntimeChanged)

public:
    explicit MirageController(QObject* parent = nullptr);
    ~MirageController() override;

    QVariantList wallpapers() const;
    QVariantMap selectedWallpaper() const;
    QString selectedWallpaperId() const;
    QVariantList playlistItems() const;
    QVariantList workshopItems() const;
    QVariantList discoverSections() const;
    QVariantMap selectedWorkshopItem() const;
    bool workshopLoading() const;
    bool discoverLoading() const;
    QString workshopError() const;
    int workshopPage() const;
    int workshopPageCount() const;
    int activeDownloadCount() const;
    QVariantList downloadQueue() const;
    bool steamReady() const;
    QString steamSetupSummary() const;
    QString steamCMDPath() const;
    QString steamUsername() const;
    bool steamLoggedIn() const;
    QString steamInstallState() const;
    double steamInstallProgress() const;
    QString steamInstallMessage() const;
    QString steamLoginState() const;
    QString steamLoginMessage() const;
    QStringList steamLoginLog() const;
    QString steamGuardType() const;
    bool steamSessionReusable() const;
    bool firstLaunch() const;
    QString statusMessage() const;
    QVariantMap settings() const;
    QVariantList selectedProperties() const;
    int playlistScreen() const;
    int screenCount() const;
    QVariantList displays() const;
    QVariantList savedPlaylists() const;
    QVariantMap playlistSettings() const;
    double selectedVolume() const;
    double selectedSpeed() const;
    QString selectedFillMode() const;

    Q_INVOKABLE void reloadWallpapers();
    Q_INVOKABLE void selectWallpaper(const QString& id);
    Q_INVOKABLE bool isWallpaperTrusted(const QString& id) const;
    Q_INVOKABLE void trustWallpaper(const QString& id, bool persist);
    Q_INVOKABLE void applySelected(bool allScreens = false);
    Q_INVOKABLE void applyWallpaper(const QString& id, bool allScreens = false);
    Q_INVOKABLE void toggleSelectedFavorite();
    Q_INVOKABLE void updateSelectedMetadata(const QString& title, const QVariantList& tags);
    Q_INVOKABLE void deleteSelectedWallpaper();
    Q_INVOKABLE void importWallpaperPath(const QString& path);
    Q_INVOKABLE void stopWallpapers();
    Q_INVOKABLE void applySelectedToScreen(int screen);
    Q_INVOKABLE void stopScreen(int screen);
    Q_INVOKABLE void addSelectedToPlaylist();
    Q_INVOKABLE void playPlaylistItem(const QString& id);
    Q_INVOKABLE void removePlaylistItem(const QString& id);
    Q_INVOKABLE void clearPlaylist();
    Q_INVOKABLE void trimPlaylistItems(int limit);
    Q_INVOKABLE void movePlaylistItem(int source, int destination);
    Q_INVOKABLE void savePlaylist(const QString& name);
    Q_INVOKABLE void loadSavedPlaylist(const QString& id);
    Q_INVOKABLE void deleteSavedPlaylist(const QString& id);
    Q_INVOKABLE void updatePlaylistSettings(const QVariantMap& values);
    Q_INVOKABLE void setSelectedProperty(const QString& key, const QVariant& value);
    Q_INVOKABLE void resetSelectedProperties();
    Q_INVOKABLE void completeFirstLaunch(bool hideUntilNextUpdate);
    Q_INVOKABLE void loadDiscover();
    Q_INVOKABLE void refreshDiscover();
    Q_INVOKABLE void setDiscoverTrendDays(int days);
    Q_INVOKABLE void setWorkshopSearchText(const QString& text);
    Q_INVOKABLE void submitWorkshopSearch();
    Q_INVOKABLE void setWorkshopSortOrder(const QString& key);
    Q_INVOKABLE void setWorkshopTypeFilter(const QString& key);
    Q_INVOKABLE void setWorkshopAgeRatingEnabled(const QString& key, bool enabled);
    Q_INVOKABLE void toggleWorkshopTag(const QString& tag);
    Q_INVOKABLE void clearWorkshopFilters();
    Q_INVOKABLE void loadPreviousWorkshopPage();
    Q_INVOKABLE void loadNextWorkshopPage();
    Q_INVOKABLE void selectWorkshopItem(const QString& id);
    Q_INVOKABLE void downloadWorkshopItem(const QString& id);
    Q_INVOKABLE void requestWorkshopPresetDependency(const QString& id);
    Q_INVOKABLE void cancelWorkshopDownload(const QString& id);
    Q_INVOKABLE void retryWorkshopDownload(const QString& id);
    Q_INVOKABLE void clearCompletedDownloads();
    Q_INVOKABLE void detectSteamCMD();
    Q_INVOKABLE void installSteamCMD();
    Q_INVOKABLE void cancelSteamCMDInstallation();
    Q_INVOKABLE void loginSteam(const QString& username, const QString& password);
    Q_INVOKABLE void submitSteamGuardCode(const QString& code);
    Q_INVOKABLE void confirmSteamMobileLogin();
    Q_INVOKABLE void useSavedSteamSession();
    Q_INVOKABLE void cancelSteamLogin();
    Q_INVOKABLE void cancelPendingSteamWork();
    Q_INVOKABLE void logoutSteam();
    Q_INVOKABLE void copySteamLoginLog();
    Q_INVOKABLE void revealWorkshopDownload(const QString& id);
    Q_INVOKABLE void pauseWallpapers();
    Q_INVOKABLE void muteWallpapers();
    Q_INVOKABLE void previewFps(int fps);
    Q_INVOKABLE bool applySettings(const QVariantMap& values);

public slots:
    void setPlaylistScreen(int screen);
    void setSelectedVolume(double volume);
    void setSelectedSpeed(double speed);
    void setSelectedFillMode(const QString& mode);

signals:
    void wallpapersChanged();
    void selectedWallpaperChanged();
    void playlistChanged();
    void workshopItemsChanged();
    void discoverChanged();
    void selectedWorkshopItemChanged();
    void installedWallpaperSelected();
    void workshopStateChanged();
    void firstLaunchChanged();
    void statusMessageChanged();
    void settingsChanged();
    void selectedRuntimeChanged();
    void playlistsSavedChanged();
    void displaysChanged();
    void steamChanged();

private:
    Wallpaper wallpaper(const QString& id) const;
    QVariantMap wallpaperMap(const Wallpaper& wallpaper) const;
    std::optional<WorkshopItem> workshopItem(const QString& id) const;
    QVariantMap workshopItemMap(const WorkshopItem& item) const;
    QVariantMap playlistMap(const Playlist& playlist) const;
    QVariantMap propertyMap(const QString& key, const ProjectProperty& property) const;
    void setStatusMessage(const QString& message);

    GlobalSettingsService m_settings;
    FavoritesManager m_favorites;
    WallpaperLibrary m_library;
    SteamCMDManager m_steamCMD;
    SteamWebAPI m_steamAPI;
    WorkshopViewModel m_workshop;
    RendererController m_renderer;
    WallpaperRuntimeStore m_runtimeStore;
    PlaylistManager m_playlist;
    TrustedWallpaperService m_trusted;
    SteamSetupViewModel m_steamSetup;
    PlaybackController m_playback;
    QVector<Wallpaper> m_allWallpapers;
    QString m_selectedWallpaperId;
    int m_playlistScreen = 0;
    bool m_firstLaunch = true;
    QString m_statusMessage;
};

} // namespace Mirage
