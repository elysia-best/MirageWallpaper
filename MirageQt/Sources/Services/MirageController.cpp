#include "Services/MirageController.h"

#include <QDir>
#include <QDesktopServices>
#include <QFileInfo>
#include <QClipboard>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QSet>
#include <QUrl>

#include <algorithm>

namespace Mirage {
namespace {




WorkshopSortOrder workshopSortOrderFor(const QString& key) {
    if (key == QStringLiteral("recent")) return WorkshopSortOrder::MostRecent;
    if (key == QStringLiteral("subscribed")) return WorkshopSortOrder::MostSubscribed;
    if (key == QStringLiteral("rated")) return WorkshopSortOrder::TopRated;
    if (key == QStringLiteral("upvoted")) return WorkshopSortOrder::MostUpvoted;
    if (key == QStringLiteral("playtime")) return WorkshopSortOrder::PlaytimeTrend;
    if (key == QStringLiteral("total-playtime")) return WorkshopSortOrder::TotalPlaytime;
    if (key == QStringLiteral("average-playtime")) return WorkshopSortOrder::AveragePlaytimeTrend;
    if (key == QStringLiteral("lifetime-average")) return WorkshopSortOrder::LifetimeAveragePlaytime;
    if (key == QStringLiteral("sessions")) return WorkshopSortOrder::SessionsTrend;
    if (key == QStringLiteral("lifetime-sessions")) return WorkshopSortOrder::LifetimeSessions;
    if (key == QStringLiteral("updated")) return WorkshopSortOrder::LastUpdated;
    return WorkshopSortOrder::Trending;
}

WorkshopTypeFilter workshopTypeFilterFor(const QString& key) {
    if (key == QStringLiteral("scene")) return WorkshopTypeFilter::Scene;
    if (key == QStringLiteral("web")) return WorkshopTypeFilter::Web;
    if (key == QStringLiteral("video")) return WorkshopTypeFilter::Video;
    if (key == QStringLiteral("preset")) return WorkshopTypeFilter::Preset;
    return WorkshopTypeFilter::All;
}

std::optional<WorkshopAgeRating> workshopAgeRatingFor(const QString& key) {
    if (key == QStringLiteral("Everyone")) return WorkshopAgeRating::Everyone;
    if (key == QStringLiteral("Questionable")) return WorkshopAgeRating::Questionable;
    if (key == QStringLiteral("Mature")) return WorkshopAgeRating::Mature;
    return std::nullopt;
}


} // namespace

MirageController::MirageController(QObject* parent)
    : QObject(parent)
    , m_settings(this)
    , m_favorites(this)
    , m_library(&m_settings, this)
    , m_steamCMD(this)
    , m_steamAPI(&m_settings, this)
    , m_workshop(&m_steamAPI, &m_steamCMD, &m_library, this)
    , m_renderer(&m_settings, this)
    , m_runtimeStore(this)
    , m_playlist(&m_library, &m_renderer, this)
    , m_trusted(this)
    , m_steamSetup(&m_steamCMD, this)
    , m_playback(&m_settings, &m_renderer, &m_runtimeStore, &m_playlist, this) {
    m_firstLaunch = QSettings().value(QStringLiteral("IsFirstLaunch"), true).toBool();
    m_renderer.setWallpaperTrustChecker([this](const Wallpaper& item) {
        return m_trusted.isTrusted(item.id());
    });
    connect(&m_library, &WallpaperLibrary::libraryChanged, this, &MirageController::reloadWallpapers);
    connect(&m_favorites, &FavoritesManager::changed, this, [this] {
        emit wallpapersChanged();
        emit selectedWallpaperChanged();
    });
    connect(&m_playlist, &PlaylistManager::currentChanged, this, [this](int screen) {
        if (screen == m_playlistScreen) emit playlistChanged();
    });
    connect(&m_playlist, &PlaylistManager::savedChanged, this, &MirageController::playlistsSavedChanged);
    connect(&m_renderer, &RendererController::rendererMessage, this, &MirageController::setStatusMessage);
    connect(&m_renderer, &RendererController::rendererStateChanged,
            this, &MirageController::displaysChanged);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen*) {
        emit displaysChanged();
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen*) {
        emit displaysChanged();
    });
    connect(&m_settings, &GlobalSettingsService::settingsChanged, this, [this](const GlobalSettings&) {
        emit settingsChanged();
    });
    connect(&m_runtimeStore, &WallpaperRuntimeStore::runtimeChanged, this,
            [this](const QString& id, const WallpaperRuntimeState&) {
                if (id == m_selectedWallpaperId) emit selectedRuntimeChanged();
            });
    connect(&m_workshop, &WorkshopViewModel::browseChanged, this, [this] {
        emit workshopItemsChanged();
        emit workshopStateChanged();
    });
    connect(&m_workshop, &WorkshopViewModel::discoverChanged, this, &MirageController::discoverChanged);
    connect(&m_workshop, &WorkshopViewModel::filtersChanged, this, &MirageController::workshopStateChanged);
    connect(&m_workshop, &WorkshopViewModel::selectedItemChanged, this,
            [this](const WorkshopItem&, bool) { emit selectedWorkshopItemChanged(); });
    connect(&m_workshop, &WorkshopViewModel::downloadQueueChanged, this, [this] {
        emit workshopItemsChanged();
        emit discoverChanged();
        emit selectedWorkshopItemChanged();
        emit workshopStateChanged();
    });
    connect(&m_workshop, &WorkshopViewModel::installedStateChanged, this, [this] {
        emit workshopItemsChanged();
        emit discoverChanged();
        emit selectedWorkshopItemChanged();
    });
    connect(&m_workshop, &WorkshopViewModel::steamSetupChanged, this, &MirageController::workshopStateChanged);
    connect(&m_steamSetup, &SteamSetupViewModel::steamChanged, this, &MirageController::steamChanged);
    connect(&m_steamCMD, &SteamCMDManager::authenticationChanged, this,
            [this](bool, const QString&) { emit workshopStateChanged(); });
    connect(&m_workshop, &WorkshopViewModel::installedWallpaperRequested, this, [this](const Wallpaper& item) {
        selectWallpaper(item.id());
        emit installedWallpaperSelected();
        setStatusMessage(QStringLiteral("该壁纸已在本地库中"));
    });
    connect(&m_workshop, &WorkshopViewModel::presetDependencyRequested, this,
            [this](const QString&, const QString& title, const QString&, const WorkshopItem&) {
                setStatusMessage(QStringLiteral("预设“%1”需要下载基础壁纸").arg(title));
            });
    connect(&m_workshop, &WorkshopViewModel::workshopItemDownloaded, this, [this](const QString&) {
        reloadWallpapers();
        setStatusMessage(QStringLiteral("创意工坊下载完成"));
    });

    if (m_steamCMD.sessionReusable()) m_steamCMD.refreshSession();
    reloadWallpapers();
    m_playback.restoreStartupPlayback();
    m_playlist.startRotators();
}

MirageController::~MirageController() {
    m_renderer.stopAll();
}

QVariantList MirageController::wallpapers() const {
    QVariantList result;
    result.reserve(m_allWallpapers.size());
    for (const Wallpaper& item : m_allWallpapers) result.append(wallpaperMap(item));
    return result;
}

QVariantMap MirageController::selectedWallpaper() const {
    return wallpaperMap(wallpaper(m_selectedWallpaperId));
}

QString MirageController::selectedWallpaperId() const {
    return m_selectedWallpaperId;
}

QVariantList MirageController::playlistItems() const {
    QVariantList result;
    for (const Wallpaper& item : m_playlist.resolvedItems(m_playlistScreen)) result.append(wallpaperMap(item));
    return result;
}

QVariantList MirageController::workshopItems() const {
    QVariantList result;
    result.reserve(m_workshop.items().size());
    for (const WorkshopItem& item : m_workshop.items()) result.append(workshopItemMap(item));
    return result;
}


QVariantMap MirageController::selectedWorkshopItem() const {
    return m_workshop.selectedItem() ? workshopItemMap(*m_workshop.selectedItem()) : QVariantMap{};
}

bool MirageController::workshopLoading() const { return m_workshop.isLoading(); }
bool MirageController::discoverLoading() const { return m_workshop.isDiscoverLoading(); }
QString MirageController::workshopError() const { return m_workshop.error(); }
int MirageController::workshopPage() const { return m_workshop.currentPage(); }
int MirageController::workshopPageCount() const { return m_workshop.totalPages(); }
int MirageController::activeDownloadCount() const { return m_workshop.activeDownloadCount(); }
QVariantList MirageController::downloadQueue() const {
    QVariantList result;
    for (const WorkshopDownloadTask& task : m_workshop.downloadQueue()) {
        const DownloadState& state = task.state;
        result.append(QVariantMap{
            {QStringLiteral("id"), task.workshopItem.publishedFileId},
            {QStringLiteral("title"), task.workshopItem.title},
            {QStringLiteral("preview"), task.workshopItem.previewImageUrl},
            {QStringLiteral("typeLabel"), task.workshopItem.displayTypeName()},
            {QStringLiteral("purpose"), task.purpose == DownloadPurpose::PresetDependency
                ? QStringLiteral("presetDependency") : QStringLiteral("wallpaper")},
            {QStringLiteral("state"), downloadStateKey(state.kind)},
            {QStringLiteral("progress"), state.percent},
            {QStringLiteral("message"), state.message},
            {QStringLiteral("size"), task.workshopItem.fileSize},
            {QStringLiteral("sizeLabel"), task.workshopItem.formattedFileSize()},
        });
    }
    return result;
}

bool MirageController::steamReady() const { return m_workshop.steamSetupState() == SteamSetupState::Ready; }
QString MirageController::steamSetupSummary() const { return m_workshop.steamSetupSummary(); }
QString MirageController::steamCMDPath() const { return m_steamCMD.steamCMDPath(); }
QString MirageController::steamUsername() const { return m_steamCMD.savedUsername(); }
bool MirageController::steamLoggedIn() const { return m_steamCMD.isLoggedIn(); }
QString MirageController::steamInstallState() const { return m_steamSetup.installState(); }
double MirageController::steamInstallProgress() const { return m_steamSetup.installProgress(); }
QString MirageController::steamInstallMessage() const { return m_steamSetup.installMessage(); }
QString MirageController::steamLoginState() const { return m_steamSetup.loginState(); }
QString MirageController::steamLoginMessage() const { return m_steamSetup.loginMessage(); }
QStringList MirageController::steamLoginLog() const { return m_steamSetup.loginLog(); }
QString MirageController::steamGuardType() const { return m_steamSetup.guardType(); }
bool MirageController::steamSessionReusable() const { return m_steamSetup.sessionReusable(); }

bool MirageController::firstLaunch() const {
    return m_firstLaunch;
}

QString MirageController::statusMessage() const {
    return m_statusMessage;
}


QVariantList MirageController::selectedProperties() const {
    const Wallpaper item = wallpaper(m_selectedWallpaperId);
    if (!item.isValid()) return {};

    const QHash<QString, ProjectProperty> properties = m_runtimeStore.effectiveProperties(item);
    QVector<QString> keys;
    keys.reserve(properties.size());
    for (auto it = properties.constBegin(); it != properties.constEnd(); ++it) {
        if (!it.value().presetOnly) keys.append(it.key());
    }
    std::sort(keys.begin(), keys.end(), [&properties](const QString& left, const QString& right) {
        const ProjectProperty leftProperty = properties.value(left);
        const ProjectProperty rightProperty = properties.value(right);
        const int leftOrder = leftProperty.order < 0 ? leftProperty.index : leftProperty.order;
        const int rightOrder = rightProperty.order < 0 ? rightProperty.index : rightProperty.order;
        if (leftOrder != rightOrder) return leftOrder < rightOrder;
        return left < right;
    });

    QVariantList result;
    result.reserve(keys.size());
    for (const QString& key : keys) result.append(propertyMap(key, properties.value(key)));
    return result;
}

int MirageController::playlistScreen() const {
    return m_playlistScreen;
}

int MirageController::screenCount() const {
    return qMax(1, QGuiApplication::screens().size());
}

QVariantList MirageController::displays() const {
    return m_playback.displays();
}

QVariantList MirageController::savedPlaylists() const {
    QVariantList result;
    const QVector<Playlist> playlists = m_playlist.saved();
    result.reserve(playlists.size());
    for (const Playlist& playlist : playlists) result.append(playlistMap(playlist));
    return result;
}


double MirageController::selectedVolume() const {
    return m_playback.selectedVolume();
}

double MirageController::selectedSpeed() const {
    return m_playback.selectedSpeed();
}

QString MirageController::selectedFillMode() const {
    return m_playback.selectedFillMode();
}

void MirageController::reloadWallpapers() {
    m_allWallpapers = m_library.loadAll();
    if (wallpaper(m_selectedWallpaperId).isValid()) {
        emit wallpapersChanged();
        emit selectedWallpaperChanged();
        return;
    }
    m_selectedWallpaperId = m_allWallpapers.isEmpty() ? QString() : m_allWallpapers.first().id();
    emit wallpapersChanged();
    emit selectedWallpaperChanged();
    emit selectedRuntimeChanged();
}

void MirageController::selectWallpaper(const QString& id) {
    if (m_selectedWallpaperId == id || !wallpaper(id).isValid()) return;
    m_selectedWallpaperId = id;
    emit selectedWallpaperChanged();
    emit selectedRuntimeChanged();
}

bool MirageController::isWallpaperTrusted(const QString& id) const {
    const Wallpaper item = wallpaper(id);
    if (!item.isValid() || item.kind() != WallpaperKind::Web) return false;
    return m_trusted.isTrusted(id);
}

void MirageController::trustWallpaper(const QString& id, bool persist) {
    const Wallpaper item = wallpaper(id);
    if (!item.isValid() || item.kind() != WallpaperKind::Web) return;
    m_trusted.trust(id, persist);
}

void MirageController::applySelected(bool allScreens) {
    m_playback.apply(wallpaper(m_selectedWallpaperId), allScreens);
}

void MirageController::applyWallpaper(const QString& id, bool allScreens) {
    selectWallpaper(id);
    m_playback.apply(wallpaper(id), allScreens);
}

void MirageController::toggleSelectedFavorite() {
    if (!m_selectedWallpaperId.isEmpty()) m_favorites.toggle(m_selectedWallpaperId);
}

void MirageController::updateSelectedMetadata(const QString& title, const QVariantList& tags) {
    const Wallpaper item = wallpaper(m_selectedWallpaperId);
    if (!item.isValid()) return;

    QStringList normalizedTags;
    QSet<QString> seen;
    for (const QVariant& value : tags) {
        const QString tag = value.toString().trimmed();
        const QString key = tag.toCaseFolded();
        if (tag.isEmpty() || seen.contains(key)) continue;
        seen.insert(key);
        normalizedTags.append(tag);
    }
    normalizedTags.sort(Qt::CaseInsensitive);

    QString error;
    if (!m_library.updateMetadata(item, title, normalizedTags, true, true, &error)) {
        setStatusMessage(error);
        return;
    }
    setStatusMessage(QStringLiteral("壁纸信息已更新"));
}

void MirageController::deleteSelectedWallpaper() {
    const Wallpaper item = wallpaper(m_selectedWallpaperId);
    if (!item.isValid()) return;

    QString error;
    if (!m_library.removeImportedWallpaper(item, &error)) {
        setStatusMessage(error);
        return;
    }
    m_favorites.setFavorite(item.id(), false);
    m_trusted.clear(item.id());
    setStatusMessage(QStringLiteral("已删除导入壁纸"));
}

void MirageController::importWallpaperPath(const QString& path) {
    if (path.isEmpty()) return;

    QString error;
    const QString source = path.endsWith(QStringLiteral("/project.json"))
        ? QFileInfo(path).absolutePath()
        : path;
    const QString imported = m_library.importAny(source, &error);
    if (imported.isEmpty()) {
        setStatusMessage(error);
        return;
    }
    reloadWallpapers();
    setStatusMessage(QStringLiteral("已导入 %1").arg(imported));
}

void MirageController::stopWallpapers() {
    m_playback.stopWallpapers();
}

void MirageController::applySelectedToScreen(int screen) {
    m_playback.applySelectedToScreen(screen);
}

void MirageController::stopScreen(int screen) {
    m_playback.stopScreen(screen);
}

void MirageController::addSelectedToPlaylist() {
    const Wallpaper item = wallpaper(m_selectedWallpaperId);
    if (!item.isValid()) return;
    m_playlist.add(item, m_playlistScreen);
    emit playlistChanged();
    setStatusMessage(QStringLiteral("已加入播放列表"));
}

void MirageController::playPlaylistItem(const QString& id) {
    const Wallpaper item = m_playlist.resolveWallpaper(id);
    if (!item.isValid()) return;
    m_playback.playPlaylistItem(item);
}

void MirageController::removePlaylistItem(const QString& id) {
    m_playlist.remove(id, m_playlistScreen);
}

void MirageController::clearPlaylist() {
    m_playlist.clear(m_playlistScreen);
}

void MirageController::trimPlaylistItems(int limit) {
    m_playlist.trimItems(qMax(0, limit), m_playlistScreen);
}

void MirageController::movePlaylistItem(int source, int destination) {
    m_playlist.move(source, destination, m_playlistScreen);
}

void MirageController::savePlaylist(const QString& name) {
    if (m_playlist.saveAs(name, m_playlistScreen).name.isEmpty()) return;
    setStatusMessage(QStringLiteral("播放列表已保存"));
}

void MirageController::loadSavedPlaylist(const QString& id) {
    for (const Playlist& playlist : m_playlist.saved()) {
        if (playlist.id.toString(QUuid::WithoutBraces) != id) continue;
        m_playlist.loadSaved(playlist, m_playlistScreen);
        setStatusMessage(QStringLiteral("已载入播放列表"));
        return;
    }
}

void MirageController::deleteSavedPlaylist(const QString& id) {
    m_playlist.deleteSaved(QUuid(id));
}

void MirageController::updatePlaylistSettings(const QVariantMap& values) {
    m_playlist.updateSettings(m_playlistScreen, [&values](PlaylistSettings& settings) {
        updatePlaylistSettingsFromMap(settings, values);
    });
}

void MirageController::setSelectedProperty(const QString& key, const QVariant& value) {
    m_playback.setSelectedProperty(key, value);
}

void MirageController::resetSelectedProperties() {
    m_playback.resetSelectedProperties();
}

void MirageController::completeFirstLaunch(bool hideUntilNextUpdate) {
    m_firstLaunch = false;
    QSettings().setValue(QStringLiteral("IsFirstLaunch"), !hideUntilNextUpdate);
    emit firstLaunchChanged();
}

void MirageController::loadDiscover() {
    m_workshop.loadDiscover();
}

void MirageController::refreshDiscover() {
    m_workshop.refreshDiscover();
}

void MirageController::setDiscoverTrendDays(int days) {
    m_workshop.setDiscoverTrendDays(days);
}

void MirageController::setWorkshopSearchText(const QString& text) {
    m_workshop.setSearchText(text);
}

void MirageController::submitWorkshopSearch() {
    m_workshop.submitSearch();
}

void MirageController::setWorkshopSortOrder(const QString& key) {
    m_workshop.setSortOrder(workshopSortOrderFor(key));
}

void MirageController::setWorkshopTypeFilter(const QString& key) {
    m_workshop.setTypeFilter(workshopTypeFilterFor(key));
}

void MirageController::setWorkshopAgeRatingEnabled(const QString& key, bool enabled) {
    const std::optional<WorkshopAgeRating> rating = workshopAgeRatingFor(key);
    if (rating) m_workshop.setAgeRatingEnabled(*rating, enabled);
}

void MirageController::toggleWorkshopTag(const QString& tag) {
    m_workshop.toggleTag(tag);
}

void MirageController::clearWorkshopFilters() {
    m_workshop.clearFilters();
}

void MirageController::loadPreviousWorkshopPage() {
    m_workshop.loadPreviousPage();
}

void MirageController::loadNextWorkshopPage() {
    m_workshop.loadNextPage();
}

void MirageController::selectWorkshopItem(const QString& id) {
    const std::optional<WorkshopItem> item = workshopItem(id);
    if (item) m_workshop.selectWorkshopItem(*item);
}

void MirageController::downloadWorkshopItem(const QString& id) {
    const std::optional<WorkshopItem> item = workshopItem(id);
    if (item) m_workshop.downloadItem(*item);
}

void MirageController::requestWorkshopPresetDependency(const QString& id) {
    m_workshop.requestPresetDependency(id);
}

void MirageController::cancelWorkshopDownload(const QString& id) {
    m_workshop.cancelDownload(id);
}

void MirageController::retryWorkshopDownload(const QString& id) {
    m_workshop.retryDownload(id);
}

void MirageController::clearCompletedDownloads() {
    m_workshop.clearCompletedDownloads();
}

void MirageController::detectSteamCMD() {
    m_steamSetup.detect();
}

void MirageController::installSteamCMD() {
    m_steamSetup.install();
}

void MirageController::cancelSteamCMDInstallation() {
    m_steamSetup.cancelInstallation();
}

void MirageController::loginSteam(const QString& username, const QString& password) {
    m_steamSetup.login(username, password);
}

void MirageController::submitSteamGuardCode(const QString& code) {
    m_steamSetup.submitGuardCode(code);
}

void MirageController::confirmSteamMobileLogin() {
    m_steamSetup.confirmMobileLogin();
}

void MirageController::useSavedSteamSession() {
    m_steamSetup.useSavedSession();
}

void MirageController::cancelSteamLogin() {
    m_steamSetup.cancelLogin();
}

void MirageController::cancelPendingSteamWork() {
    m_steamSetup.cancelPendingWork();
}

void MirageController::logoutSteam() {
    m_steamSetup.logout();
}

void MirageController::copySteamLoginLog() {
    m_steamSetup.copyLoginLog();
}

void MirageController::revealWorkshopDownload(const QString& id) {
    const QStringList directories = m_library.workshopItemDirectories(id);
    for (const QString& directory : directories) {
        if (QFileInfo::exists(directory + QStringLiteral("/project.json"))) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
            return;
        }
    }
}

void MirageController::pauseWallpapers() {
    m_playback.pauseWallpapers();
}

void MirageController::muteWallpapers() {
    m_playback.muteWallpapers();
}

bool MirageController::applySettings(const QVariantMap& values) {
    return m_playback.applySettings(values);
}

void MirageController::previewFps(int fps) {
    m_playback.previewFps(fps);
}

void MirageController::setPlaylistScreen(int screen) {
    const int selected = qBound(0, screen, screenCount() - 1);
    if (selected == m_playlistScreen) return;
    m_playlistScreen = selected;
    m_playlist.ensureScreen(m_playlistScreen);
    emit playlistChanged();
}

void MirageController::setSelectedVolume(double volume) {
    m_playback.setSelectedVolume(volume);
}

void MirageController::setSelectedSpeed(double speed) {
    m_playback.setSelectedSpeed(speed);
}

void MirageController::setSelectedFillMode(const QString& mode) {
    m_playback.setSelectedFillMode(mode);
}

Wallpaper MirageController::wallpaper(const QString& id) const {
    for (const Wallpaper& item : m_allWallpapers) {
        if (item.id() == id) return item;
    }
    return {};
}

std::optional<WorkshopItem> MirageController::workshopItem(const QString& id) const {
    const auto find = [&id](const QVector<WorkshopItem>& items) -> std::optional<WorkshopItem> {
        for (const WorkshopItem& item : items) {
            if (item.publishedFileId == id) return item;
        }
        return std::nullopt;
    };
    if (const std::optional<WorkshopItem> item = find(m_workshop.items())) return item;
    for (const DiscoverSectionDefinition& definition : kDiscoverSections) {
        if (const std::optional<WorkshopItem> item = find(m_workshop.discoverItems(definition.collection))) return item;
    }
    return std::nullopt;
}





void MirageController::setStatusMessage(const QString& message) {
    if (message.isEmpty()) return;
    m_statusMessage = message;
    emit statusMessageChanged();
}

} // namespace Mirage
