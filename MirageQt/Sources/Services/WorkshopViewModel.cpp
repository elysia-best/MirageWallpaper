#include "Services/WorkshopViewModel.h"

#include <QtConcurrent/QtConcurrentRun>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSettings>
#include <QTimer>

namespace Mirage {
namespace {

constexpr int kItemsPerPage = 50;

bool isActive(const DownloadState& state) {
    return state.kind == DownloadStateKind::Starting ||
           state.kind == DownloadStateKind::Downloading ||
           state.kind == DownloadStateKind::Validating;
}

InstalledWorkshopState scanInstalledWorkshopState(const QStringList& sources) {
    InstalledWorkshopState state;
    QHash<QString, Project> projects;

    for (const QString& source : sources) {
        const QDir directory(source);
        if (!directory.exists()) continue;
        const QFileInfoList entries = directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        for (const QFileInfo& entry : entries) {
            QFile file(entry.filePath() + "/project.json");
            if (!file.open(QIODevice::ReadOnly)) continue;
            const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
            if (!document.isObject()) continue;

            const Project project = Project::fromJson(document.object());
            const QString id = project.workshopId.isEmpty() ? entry.fileName() : project.workshopId;
            if (id.isEmpty() || projects.contains(id)) continue;
            projects.insert(id, project);
            state.installedIds.insert(id);
        }
    }

    for (auto it = projects.cbegin(); it != projects.cend(); ++it) {
        const Project& project = it.value();
        if (project.isWorkshopPreset() &&
            (project.dependency.isEmpty() || !state.installedIds.contains(project.dependency))) {
            state.presetsNeedingDependency.insert(it.key());
        }
    }
    return state;
}

} // namespace

WorkshopViewModel::WorkshopViewModel(SteamWebAPI* api,
                                     SteamCMDManager* steamCMD,
                                     WallpaperLibrary* library,
                                     QObject* parent)
    : QObject(parent)
    , m_api(api)
    , m_steamCMD(steamCMD)
    , m_library(library) {
    qRegisterMetaType<Mirage::WorkshopDownloadTask>();
    qRegisterMetaType<Mirage::WorkshopSortOrder>();
    qRegisterMetaType<Mirage::WorkshopTypeFilter>();

    m_searchDebounce.setSingleShot(true);
    m_searchDebounce.setInterval(500);
    m_ageRatingMask = QSettings().value(QStringLiteral("Workshop/AgeRatingMask"), 1).toInt() & 0x7;
    connect(&m_searchDebounce, &QTimer::timeout, this, &WorkshopViewModel::search);
    connect(m_api, &SteamWebAPI::queryFinished, this, &WorkshopViewModel::handleQueryFinished);
    connect(m_api, &SteamWebAPI::detailsFinished, this, &WorkshopViewModel::handleDetailsFinished);
    connect(m_steamCMD, &SteamCMDManager::downloadStateChanged, this, &WorkshopViewModel::handleDownloadState);
    connect(m_steamCMD, &SteamCMDManager::authenticationChanged, this, [this] {
        refreshSteamSetupState();
        processDownloadQueue();
    });
    connect(m_steamCMD, &SteamCMDManager::steamCMDPathChanged, this, [this] {
        refreshSteamSetupState();
    });
    connect(m_library, &WallpaperLibrary::libraryChanged, this, &WorkshopViewModel::refreshInstalledState);
    connect(&m_installedStateWatcher, &QFutureWatcher<InstalledWorkshopState>::finished, this, [this] {
        const InstalledWorkshopState state = m_installedStateWatcher.result();
        const bool changed = m_installedWorkshopIds != state.installedIds ||
                             m_presetsNeedingDependency != state.presetsNeedingDependency;
        m_installedWorkshopIds = state.installedIds;
        m_presetsNeedingDependency = state.presetsNeedingDependency;
        if (changed) emit installedStateChanged();
        if (m_installedStateRefreshPending) {
            m_installedStateRefreshPending = false;
            refreshInstalledState();
        }
    });
    refreshSteamSetupState();
    refreshInstalledState();
}

const QVector<WorkshopItem>& WorkshopViewModel::items() const { return m_items; }

const QVector<WorkshopItem>& WorkshopViewModel::discoverItems(DiscoverCollection collection) const {
    static const QVector<WorkshopItem> empty;
    const auto it = m_discoverItems.constFind(collection);
    return it == m_discoverItems.cend() ? empty : it.value();
}

const QVector<WorkshopItem>& WorkshopViewModel::bannerItems() const { return m_bannerItems; }
const QVector<WorkshopDownloadTask>& WorkshopViewModel::downloadQueue() const { return m_downloadQueue; }
const std::optional<WorkshopItem>& WorkshopViewModel::selectedItem() const { return m_selectedItem; }
QString WorkshopViewModel::searchText() const { return m_searchText; }
const QSet<QString>& WorkshopViewModel::selectedTags() const { return m_selectedTags; }
WorkshopSortOrder WorkshopViewModel::sortOrder() const { return m_sortOrder; }
WorkshopTypeFilter WorkshopViewModel::typeFilter() const { return m_typeFilter; }
int WorkshopViewModel::ageRatingMask() const { return m_ageRatingMask; }
int WorkshopViewModel::trendDays() const { return m_trendDays; }
int WorkshopViewModel::discoverTrendDays() const { return m_discoverTrendDays; }
int WorkshopViewModel::currentPage() const { return m_currentPage; }
int WorkshopViewModel::totalPages() const { return qMax(1, (m_totalItems + kItemsPerPage - 1) / kItemsPerPage); }
bool WorkshopViewModel::isLoading() const { return m_isLoading; }
bool WorkshopViewModel::isDiscoverLoading() const { return m_isDiscoverLoading; }
QString WorkshopViewModel::error() const { return m_error; }
SteamSetupState WorkshopViewModel::steamSetupState() const { return m_steamSetupState; }

QString WorkshopViewModel::steamSetupSummary() const {
    switch (m_steamSetupState) {
    case SteamSetupState::SteamCMDMissing: return QStringLiteral("需要先安装 SteamCMD");
    case SteamSetupState::NeedsLogin: return QStringLiteral("需要有效的 Steam 会话");
    case SteamSetupState::Ready: return QStringLiteral("已登录 %1").arg(m_steamCMD->savedUsername());
    }
    return {};
}

int WorkshopViewModel::activeDownloadCount() const {
    int count = 0;
    for (const WorkshopDownloadTask& task : m_downloadQueue) {
        if (isActive(task.state)) ++count;
    }
    return count;
}

std::optional<Wallpaper> WorkshopViewModel::installedItem(const QString& workshopId) const {
    const QStringList directories = m_library->workshopItemDirectories(workshopId);
    std::optional<Wallpaper> first;
    std::optional<Wallpaper> preset;
    for (const QString& directory : directories) {
        const Wallpaper wallpaper = m_library->loadWallpaper(directory);
        if (!first) first = wallpaper;
        if (wallpaper.isValid()) return wallpaper;
        if (wallpaper.isPreset() && !preset) preset = wallpaper;
    }
    return preset ? preset : first;
}

bool WorkshopViewModel::isItemDownloaded(const QString& workshopId) const {
    return m_installedWorkshopIds.contains(workshopId);
}

bool WorkshopViewModel::presetNeedsDependency(const QString& workshopId) const {
    return m_presetsNeedingDependency.contains(workshopId);
}

std::optional<DownloadState> WorkshopViewModel::downloadStateFor(const QString& workshopId) const {
    for (const WorkshopDownloadTask& task : m_downloadQueue) {
        if (task.workshopItem.publishedFileId == workshopId) return task.state;
    }
    return std::nullopt;
}

void WorkshopViewModel::setSearchText(const QString& text) {
    if (m_searchText == text) return;
    m_searchText = text;
    m_currentPage = 1;
    emit filtersChanged();
    m_searchDebounce.start();
}

void WorkshopViewModel::submitSearch() {
    m_searchDebounce.stop();
    m_currentPage = 1;
    search();
}

void WorkshopViewModel::setSortOrder(WorkshopSortOrder order) {
    if (m_sortOrder == order) return;
    m_sortOrder = order;
    m_currentPage = 1;
    emit filtersChanged();
    search();
}

void WorkshopViewModel::setTrendDays(int days) {
    const int bounded = qBound(1, days, 365);
    if (m_trendDays == bounded) return;
    m_trendDays = bounded;
    m_currentPage = 1;
    emit filtersChanged();
    if (workshopSortUsesTrendPeriod(m_sortOrder)) search();
}

void WorkshopViewModel::setDiscoverTrendDays(int days) {
    const int bounded = qBound(1, days, 365);
    if (m_discoverTrendDays == bounded) return;
    m_discoverTrendDays = bounded;
    refreshDiscover();
}

void WorkshopViewModel::setTypeFilter(WorkshopTypeFilter filter) {
    if (m_typeFilter == filter) return;
    m_typeFilter = filter;
    m_currentPage = 1;
    emit filtersChanged();
    search();
}

void WorkshopViewModel::setAgeRatingEnabled(WorkshopAgeRating rating, bool enabled) {
    const int bit = 1 << static_cast<int>(rating);
    const int updated = enabled ? (m_ageRatingMask | bit) : (m_ageRatingMask & ~bit);
    if (updated == m_ageRatingMask) return;
    m_ageRatingMask = updated;
    QSettings().setValue(QStringLiteral("Workshop/AgeRatingMask"), m_ageRatingMask);
    m_currentPage = 1;
    emit filtersChanged();
    search();
}

void WorkshopViewModel::toggleTag(const QString& tag) {
    if (m_selectedTags.contains(tag)) m_selectedTags.remove(tag);
    else m_selectedTags.insert(tag);
    m_currentPage = 1;
    emit filtersChanged();
    search();
}

void WorkshopViewModel::selectAllTags() {
    m_selectedTags.clear();
    for (const WorkshopTag& tag : workshopTags()) m_selectedTags.insert(tag.value);
    m_currentPage = 1;
    emit filtersChanged();
    search();
}

void WorkshopViewModel::clearTags() {
    if (m_selectedTags.isEmpty()) return;
    m_selectedTags.clear();
    m_currentPage = 1;
    emit filtersChanged();
    search();
}

void WorkshopViewModel::clearFilters() {
    m_searchDebounce.stop();
    m_searchText.clear();
    m_selectedTags.clear();
    m_sortOrder = WorkshopSortOrder::Trending;
    m_typeFilter = WorkshopTypeFilter::All;
    m_ageRatingMask = 1;
    m_trendDays = 7;
    QSettings().setValue(QStringLiteral("Workshop/AgeRatingMask"), m_ageRatingMask);
    m_currentPage = 1;
    emit filtersChanged();
    search();
}

void WorkshopViewModel::loadPreviousPage() {
    if (m_currentPage <= 1) return;
    --m_currentPage;
    emit filtersChanged();
    search();
}

void WorkshopViewModel::loadNextPage() {
    if (m_currentPage >= totalPages()) return;
    ++m_currentPage;
    emit filtersChanged();
    search();
}

void WorkshopViewModel::search() {
    WorkshopQuery query;
    query.searchText = m_searchText;
    query.tags = m_selectedTags.values();
    query.sortOrder = m_sortOrder;
    query.typeFilter = m_typeFilter;
    query.trendDays = m_trendDays;
    query.ageRatingMask = m_ageRatingMask;
    query.page = m_currentPage;
    query.perPage = kItemsPerPage;

    if (m_searchRequestId != 0) m_api->cancelQuery(m_searchRequestId);
    m_isLoading = true;
    m_error.clear();
    emit browseChanged();
    m_searchRequestId = m_api->queryFiles(query);
}

void WorkshopViewModel::loadDiscover() {
    if (m_isDiscoverLoading) return;
    m_isDiscoverLoading = true;
    m_discoverRequests.clear();
    m_pendingDiscoverItems.clear();
    emit discoverChanged();

    issueDiscoverRequest(DiscoverCollection::Trending, WorkshopSortOrder::Trending, {}, 15, m_discoverTrendDays);
    issueDiscoverRequest(DiscoverCollection::MostUpvoted, WorkshopSortOrder::MostUpvoted, {}, 10);
    issueDiscoverRequest(DiscoverCollection::MostRecent, WorkshopSortOrder::MostRecent, {}, 10);
    issueDiscoverRequest(DiscoverCollection::MostSubscribed, WorkshopSortOrder::MostSubscribed, {}, 10);
    issueDiscoverRequest(DiscoverCollection::TopRated, WorkshopSortOrder::TopRated, {}, 10);
    issueDiscoverRequest(DiscoverCollection::LastUpdated, WorkshopSortOrder::LastUpdated, {}, 10);
    issueDiscoverRequest(DiscoverCollection::PlaytimeTrend, WorkshopSortOrder::PlaytimeTrend, {}, 10, m_discoverTrendDays);
    issueDiscoverRequest(DiscoverCollection::AveragePlaytimeTrend, WorkshopSortOrder::AveragePlaytimeTrend, {}, 10, m_discoverTrendDays);
    issueDiscoverRequest(DiscoverCollection::SessionsTrend, WorkshopSortOrder::SessionsTrend, {}, 10, m_discoverTrendDays);
    issueDiscoverRequest(DiscoverCollection::TotalPlaytime, WorkshopSortOrder::TotalPlaytime, {}, 10);
    issueDiscoverRequest(DiscoverCollection::LifetimeAveragePlaytime, WorkshopSortOrder::LifetimeAveragePlaytime, {}, 10);
    issueDiscoverRequest(DiscoverCollection::LifetimeSessions, WorkshopSortOrder::LifetimeSessions, {}, 10);
    issueDiscoverRequest(DiscoverCollection::Anime, WorkshopSortOrder::Trending, QStringLiteral("Anime"), 10);
    issueDiscoverRequest(DiscoverCollection::Nature, WorkshopSortOrder::Trending, QStringLiteral("Nature"), 10);
    issueDiscoverRequest(DiscoverCollection::Abstract, WorkshopSortOrder::Trending, QStringLiteral("Abstract"), 10);
    issueDiscoverRequest(DiscoverCollection::Landscape, WorkshopSortOrder::Trending, QStringLiteral("Landscape"), 10);
}

void WorkshopViewModel::reloadOnlineContent() {
    cancelDiscoverRequests();
    m_discoverItems.clear();
    m_pendingDiscoverItems.clear();
    m_bannerItems.clear();
    m_isDiscoverLoading = false;
    emit discoverChanged();
    loadDiscover();
    search();
}

void WorkshopViewModel::refreshDiscover() {
    cancelDiscoverRequests();
    m_pendingDiscoverItems.clear();
    m_isDiscoverLoading = false;
    loadDiscover();
}

void WorkshopViewModel::issueDiscoverRequest(DiscoverCollection collection,
                                             WorkshopSortOrder order,
                                             const QString& tag,
                                             int count,
                                             int trendDays) {
    WorkshopQuery query;
    query.sortOrder = order;
    query.perPage = count;
    query.trendDays = trendDays;
    query.ageRatingMask = m_ageRatingMask;
    if (!tag.isEmpty()) query.tags = {tag};
    const quint64 requestId = m_api->queryFiles(query);
    m_discoverRequests.insert(requestId, collection);
}

void WorkshopViewModel::handleQueryFinished(quint64 requestId, const WorkshopQueryResult& result) {
    if (requestId == m_searchRequestId) {
        m_isLoading = false;
        m_error = result.error;
        if (result.error.isEmpty()) {
            m_items = result.items;
            m_totalItems = result.total;
        } else {
            m_items.clear();
            m_totalItems = 0;
        }
        emit browseChanged();
        return;
    }

    const auto it = m_discoverRequests.find(requestId);
    if (it == m_discoverRequests.end()) return;
    const DiscoverCollection collection = it.value();
    m_discoverRequests.erase(it);
    if (result.error.isEmpty()) m_pendingDiscoverItems[collection] = result.items;
    if (!m_discoverRequests.isEmpty()) return;

    m_discoverItems = std::move(m_pendingDiscoverItems);
    m_bannerItems = m_discoverItems.value(DiscoverCollection::Trending).mid(
        0, qMin(5, m_discoverItems.value(DiscoverCollection::Trending).size()));
    m_isDiscoverLoading = false;
    emit discoverChanged();
}

void WorkshopViewModel::checkSteamSetup() {
    if (m_steamCMD->steamCMDPath().isEmpty()) m_steamCMD->detectSteamCMD();
    refreshSteamSetupState();
}

void WorkshopViewModel::refreshSteamSetupState() {
    if (m_steamCMD->steamCMDPath().isEmpty()) {
        m_steamSetupState = SteamSetupState::SteamCMDMissing;
    } else if (!m_steamCMD->isLoggedIn()) {
        m_steamSetupState = SteamSetupState::NeedsLogin;
    } else {
        m_steamSetupState = SteamSetupState::Ready;
    }
    emit steamSetupChanged();
}

void WorkshopViewModel::logout() {
    m_steamCMD->logout();
    refreshSteamSetupState();
}

void WorkshopViewModel::setSelectedItem(const std::optional<WorkshopItem>& item) {
    m_selectedItem = item;
    emit selectedItemChanged(item.value_or(WorkshopItem()), item.has_value());
}

void WorkshopViewModel::selectWorkshopItem(const WorkshopItem& item) {
    const auto installed = installedItem(item.publishedFileId);
    if (installed && installed->isValid()) {
        setSelectedItem(std::nullopt);
        emit installedWallpaperRequested(*installed);
        return;
    }

    setSelectedItem(item);
    if (installed && installed->isPreset()) requestPresetDependency(item.publishedFileId);
}

void WorkshopViewModel::requestPresetDependency(const QString& workshopId) {
    const auto preset = installedItem(workshopId);
    if (!preset || !preset->isPreset() || preset->presetDependency.isEmpty()) return;

    const QString dependencyId = preset->presetDependency;
    const auto dependency = installedItem(dependencyId);
    if (dependency && dependency->isValid()) {
        const auto refreshed = installedItem(workshopId);
        if (refreshed && refreshed->isValid()) emit installedWallpaperRequested(*refreshed);
        return;
    }

    PendingDependency pending;
    pending.presetId = workshopId;
    pending.presetTitle = preset->project.title;
    pending.dependencyId = dependencyId;
    const quint64 requestId = m_api->getFileDetails({dependencyId});
    m_dependencyRequests.insert(requestId, pending);
}

void WorkshopViewModel::handleDetailsFinished(quint64 requestId,
                                              const QVector<WorkshopItem>& items,
                                              const QString&) {
    const auto it = m_dependencyRequests.find(requestId);
    if (it == m_dependencyRequests.end()) return;
    const PendingDependency pending = it.value();
    m_dependencyRequests.erase(it);

    WorkshopItem dependency = WorkshopItem::dependencyPlaceholder(pending.dependencyId);
    for (const WorkshopItem& item : items) {
        if (item.publishedFileId == pending.dependencyId) {
            dependency = item;
            break;
        }
    }
    emit presetDependencyRequested(pending.presetId,
                                   pending.presetTitle,
                                   pending.dependencyId,
                                   dependency);
}

void WorkshopViewModel::downloadItem(const WorkshopItem& item, DownloadPurpose purpose) {
    for (int i = 0; i < m_downloadQueue.size(); ++i) {
        if (m_downloadQueue.at(i).workshopItem.publishedFileId != item.publishedFileId) continue;
        const DownloadStateKind kind = m_downloadQueue.at(i).state.kind;
        if (kind != DownloadStateKind::Failed && kind != DownloadStateKind::Completed && kind != DownloadStateKind::Cancelled) return;
        m_downloadQueue.removeAt(i);
        break;
    }

    WorkshopDownloadTask task;
    task.workshopItem = item;
    task.state.kind = DownloadStateKind::Queued;
    task.state.message = QStringLiteral("等待 SteamCMD 按顺序下载…");
    task.purpose = purpose;
    m_downloadQueue.push_back(task);
    emit downloadQueueChanged();
    processDownloadQueue();
}

void WorkshopViewModel::processDownloadQueue() {
    if (m_steamSetupState != SteamSetupState::Ready) return;
    for (const WorkshopDownloadTask& task : m_downloadQueue) {
        if (isActive(task.state)) return;
    }

    for (WorkshopDownloadTask& task : m_downloadQueue) {
        if (task.state.kind != DownloadStateKind::Queued) continue;
        task.state.kind = DownloadStateKind::Starting;
        task.state.message = QStringLiteral("正在启动 SteamCMD…");
        task.startedAt = QDateTime::currentDateTime();
        emit downloadQueueChanged();
        m_steamCMD->downloadItem(task.workshopItem.publishedFileId);
        return;
    }
}

void WorkshopViewModel::handleDownloadState(const QString& workshopId, const DownloadState& state) {
    for (int i = 0; i < m_downloadQueue.size(); ++i) {
        WorkshopDownloadTask& task = m_downloadQueue[i];
        if (task.workshopItem.publishedFileId != workshopId) continue;
        if (state.kind == DownloadStateKind::Cancelled) {
            m_downloadQueue.removeAt(i);
            emit downloadQueueChanged();
            processDownloadQueue();
            return;
        }

        task.state = state;
        if (state.kind == DownloadStateKind::Completed) {
            task.completedAt = QDateTime::currentDateTime();
            emit workshopItemDownloaded(workshopId);
            QTimer::singleShot(500, this, [this, workshopId] {
                refreshInstalledState();
                handleCompletedDownload(workshopId);
            });
        }
        emit downloadQueueChanged();
        if (state.kind == DownloadStateKind::Completed || state.kind == DownloadStateKind::Failed) {
            processDownloadQueue();
        }
        return;
    }
}

void WorkshopViewModel::cancelDownload(const QString& workshopId) {
    for (int i = 0; i < m_downloadQueue.size(); ++i) {
        WorkshopDownloadTask& task = m_downloadQueue[i];
        if (task.workshopItem.publishedFileId != workshopId) continue;
        if (task.state.kind == DownloadStateKind::Queued) {
            m_downloadQueue.removeAt(i);
            emit downloadQueueChanged();
            processDownloadQueue();
        } else {
            m_steamCMD->cancelDownload(workshopId);
        }
        return;
    }
}

void WorkshopViewModel::retryDownload(const QString& workshopId) {
    for (WorkshopDownloadTask& task : m_downloadQueue) {
        if (task.workshopItem.publishedFileId != workshopId || task.state.kind != DownloadStateKind::Failed) continue;
        task.state = DownloadState();
        task.state.kind = DownloadStateKind::Queued;
        task.state.message = QStringLiteral("等待 SteamCMD 按顺序下载…");
        emit downloadQueueChanged();
        processDownloadQueue();
        return;
    }
}

void WorkshopViewModel::clearCompletedDownloads() {
    for (int i = m_downloadQueue.size() - 1; i >= 0; --i) {
        const DownloadStateKind kind = m_downloadQueue.at(i).state.kind;
        if (kind == DownloadStateKind::Completed || kind == DownloadStateKind::Failed || kind == DownloadStateKind::Cancelled) {
            m_downloadQueue.removeAt(i);
        }
    }
    emit downloadQueueChanged();
}

void WorkshopViewModel::confirmPresetDependencyDownload(const QString& presetId,
                                                        const QString& dependencyId,
                                                        const WorkshopItem& dependencyItem) {
    const auto preset = installedItem(presetId);
    if (preset && preset->isValid()) {
        emit installedWallpaperRequested(*preset);
        return;
    }

    m_pendingPresetId = presetId;
    m_pendingDependencyId = dependencyId;
    downloadItem(dependencyItem, DownloadPurpose::PresetDependency);
    if (m_steamSetupState != SteamSetupState::Ready) emit steamSetupRequested();
}

void WorkshopViewModel::handleCompletedDownload(const QString& workshopId) {
    if (!m_pendingDependencyId.isEmpty() && workshopId == m_pendingDependencyId) {
        const auto preset = installedItem(m_pendingPresetId);
        if (preset && preset->isValid()) {
            m_pendingPresetId.clear();
            m_pendingDependencyId.clear();
            emit installedWallpaperRequested(*preset);
            return;
        }
    }

    const auto wallpaper = installedItem(workshopId);
    if (!wallpaper) return;
    if (wallpaper->isPreset() && !wallpaper->isValid()) {
        requestPresetDependency(workshopId);
    } else if (wallpaper->isValid()) {
        emit installedWallpaperRequested(*wallpaper);
    }
}

void WorkshopViewModel::refreshInstalledState() {
    if (m_installedStateWatcher.isRunning()) {
        m_installedStateRefreshPending = true;
        return;
    }
    const QStringList sources = m_library->sourceDirectories();
    m_installedStateWatcher.setFuture(QtConcurrent::run(scanInstalledWorkshopState, sources));
}

void WorkshopViewModel::cancelDiscoverRequests() {
    for (auto it = m_discoverRequests.cbegin(); it != m_discoverRequests.cend(); ++it) {
        m_api->cancelQuery(it.key());
    }
    m_discoverRequests.clear();
}

void WorkshopViewModel::navigateToWorkshopWithTag(const QString& tag) {
    m_searchDebounce.stop();
    m_selectedTags = {tag};
    m_searchText.clear();
    m_typeFilter = WorkshopTypeFilter::All;
    m_sortOrder = WorkshopSortOrder::Trending;
    m_currentPage = 1;
    emit filtersChanged();
    emit navigateToWorkshopRequested();
    search();
}

void WorkshopViewModel::navigateToWorkshopWithSort(WorkshopSortOrder order) {
    m_searchDebounce.stop();
    m_selectedTags.clear();
    m_searchText.clear();
    m_typeFilter = WorkshopTypeFilter::All;
    m_sortOrder = order;
    m_currentPage = 1;
    emit filtersChanged();
    emit navigateToWorkshopRequested();
    search();
}

} // namespace Mirage
