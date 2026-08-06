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

QVariantMap settingsMap(const GlobalSettings& settings) {
    return {
        {QStringLiteral("otherApplicationFocused"), settings.otherApplicationFocused},
        {QStringLiteral("otherApplicationFullscreen"), settings.otherApplicationFullscreen},
        {QStringLiteral("otherApplicationPlayingAudio"), settings.otherApplicationPlayingAudio},
        {QStringLiteral("displayAsleep"), settings.displayAsleep},
        {QStringLiteral("laptopOnBattery"), settings.laptopOnBattery},
        {QStringLiteral("antiAliasing"), settings.antiAliasing},
        {QStringLiteral("postProcessing"), settings.postProcessing},
        {QStringLiteral("textureResolution"), settings.textureResolution},
        {QStringLiteral("reflections"), settings.reflections},
        {QStringLiteral("fps"), settings.fps},
        {QStringLiteral("wallpaperLoadSource"), settings.wallpaperLoadSource},
        {QStringLiteral("autoStart"), settings.autoStart},
        {QStringLiteral("safeMode"), settings.safeMode},
        {QStringLiteral("language"), settings.language},
        {QStringLiteral("appearance"), settings.appearance},
        {QStringLiteral("audioOutput"), settings.audioOutput},
        {QStringLiteral("reloadWhenChangingOutputDevice"), settings.reloadWhenChangingOutputDevice},
        {QStringLiteral("masterVolume"), settings.masterVolume},
        {QStringLiteral("globalMuted"), settings.globalMuted},
        {QStringLiteral("enableSpectrum"), settings.enableSpectrum},
        {QStringLiteral("videoFramework"), settings.videoFramework},
        {QStringLiteral("processPriority"), settings.processPriority},
        {QStringLiteral("pauseOnVRAMExhausted"), settings.pauseOnVRAMExhausted},
        {QStringLiteral("restartAfterCrashing"), settings.restartAfterCrashing},
        {QStringLiteral("logLevel"), settings.logLevel},
        {QStringLiteral("verboseLog"), settings.verboseLog},
        {QStringLiteral("autoRefresh"), settings.autoRefresh},
        {QStringLiteral("steamAPIEndpoint"), settings.steamAPIEndpoint},
        {QStringLiteral("steamAPIKey"), settings.steamAPIKey},
        {QStringLiteral("customWorkshopDirectory"), settings.customWorkshopDirectory},
        {QStringLiteral("customImportedDirectory"), settings.customImportedDirectory},
    };
}

void updateSettingsFromMap(GlobalSettings& settings, const QVariantMap& values) {
    const auto stringValue = [&values](const QString& key, const QString& fallback) {
        return values.contains(key) ? values.value(key).toString() : fallback;
    };
    const auto boolValue = [&values](const QString& key, bool fallback) {
        return values.contains(key) ? values.value(key).toBool() : fallback;
    };
    const auto intValue = [&values](const QString& key, int fallback) {
        return values.contains(key) ? values.value(key).toInt() : fallback;
    };
    const auto doubleValue = [&values](const QString& key, double fallback) {
        return values.contains(key) ? values.value(key).toDouble() : fallback;
    };
    settings.otherApplicationFocused = stringValue(QStringLiteral("otherApplicationFocused"), settings.otherApplicationFocused);
    settings.otherApplicationFullscreen = stringValue(QStringLiteral("otherApplicationFullscreen"), settings.otherApplicationFullscreen);
    settings.otherApplicationPlayingAudio = stringValue(QStringLiteral("otherApplicationPlayingAudio"), settings.otherApplicationPlayingAudio);
    settings.displayAsleep = stringValue(QStringLiteral("displayAsleep"), settings.displayAsleep);
    settings.laptopOnBattery = stringValue(QStringLiteral("laptopOnBattery"), settings.laptopOnBattery);
    settings.antiAliasing = stringValue(QStringLiteral("antiAliasing"), settings.antiAliasing);
    settings.postProcessing = stringValue(QStringLiteral("postProcessing"), settings.postProcessing);
    settings.textureResolution = stringValue(QStringLiteral("textureResolution"), settings.textureResolution);
    settings.reflections = boolValue(QStringLiteral("reflections"), settings.reflections);
    settings.fps = intValue(QStringLiteral("fps"), settings.fps);
    settings.wallpaperLoadSource = stringValue(QStringLiteral("wallpaperLoadSource"), settings.wallpaperLoadSource);
    settings.autoStart = boolValue(QStringLiteral("autoStart"), settings.autoStart);
    settings.safeMode = boolValue(QStringLiteral("safeMode"), settings.safeMode);
    settings.language = stringValue(QStringLiteral("language"), settings.language);
    settings.appearance = stringValue(QStringLiteral("appearance"), settings.appearance);
    settings.audioOutput = boolValue(QStringLiteral("audioOutput"), settings.audioOutput);
    settings.reloadWhenChangingOutputDevice = boolValue(QStringLiteral("reloadWhenChangingOutputDevice"), settings.reloadWhenChangingOutputDevice);
    settings.masterVolume = doubleValue(QStringLiteral("masterVolume"), settings.masterVolume);
    settings.globalMuted = boolValue(QStringLiteral("globalMuted"), settings.globalMuted);
    settings.enableSpectrum = boolValue(QStringLiteral("enableSpectrum"), settings.enableSpectrum);
    settings.videoFramework = stringValue(QStringLiteral("videoFramework"), settings.videoFramework);
    settings.processPriority = stringValue(QStringLiteral("processPriority"), settings.processPriority);
    settings.pauseOnVRAMExhausted = boolValue(QStringLiteral("pauseOnVRAMExhausted"), settings.pauseOnVRAMExhausted);
    settings.restartAfterCrashing = boolValue(QStringLiteral("restartAfterCrashing"), settings.restartAfterCrashing);
    settings.logLevel = stringValue(QStringLiteral("logLevel"), settings.logLevel);
    settings.verboseLog = boolValue(QStringLiteral("verboseLog"), settings.verboseLog);
    settings.autoRefresh = boolValue(QStringLiteral("autoRefresh"), settings.autoRefresh);
    settings.steamAPIEndpoint = stringValue(QStringLiteral("steamAPIEndpoint"), settings.steamAPIEndpoint);
    settings.steamAPIKey = stringValue(QStringLiteral("steamAPIKey"), settings.steamAPIKey);
    settings.customWorkshopDirectory = stringValue(QStringLiteral("customWorkshopDirectory"), settings.customWorkshopDirectory);
    settings.customImportedDirectory = stringValue(QStringLiteral("customImportedDirectory"), settings.customImportedDirectory);
}

QVariantMap playlistSettingsMap(const PlaylistSettings& settings) {
    QVariantList anchors;
    for (int hour : settings.daytimeAnchors) anchors.append(hour);
    QVariantList weekOrder;
    for (int day : settings.dayOfWeekOrder) weekOrder.append(day);
    return {
        {QStringLiteral("order"), playlistOrderKey(settings.order)},
        {QStringLiteral("timing"), playlistTimingKey(settings.timing)},
        {QStringLiteral("timerHours"), settings.timerHours},
        {QStringLiteral("timerMinutes"), settings.timerMinutes},
        {QStringLiteral("updateOnPause"), settings.updateOnPause},
        {QStringLiteral("transition"), playlistTransitionKey(settings.transition)},
        {QStringLiteral("transitionSeconds"), settings.transitionSeconds},
        {QStringLiteral("alwaysBeginFirst"), settings.alwaysBeginFirst},
        {QStringLiteral("introOnStartup"), settings.introOnStartup},
        {QStringLiteral("videoSequence"), settings.videoSequence},
        {QStringLiteral("daytimeAnchors"), anchors},
        {QStringLiteral("dayOfWeekOrder"), weekOrder},
    };
}

void updatePlaylistSettingsFromMap(PlaylistSettings& settings, const QVariantMap& values) {
    if (values.contains(QStringLiteral("order"))) {
        settings.order = playlistOrderFromKey(values.value(QStringLiteral("order")).toString());
    }
    if (values.contains(QStringLiteral("timing"))) {
        settings.timing = playlistTimingFromKey(values.value(QStringLiteral("timing")).toString());
    }
    if (values.contains(QStringLiteral("timerHours"))) {
        settings.timerHours = values.value(QStringLiteral("timerHours")).toInt();
    }
    if (values.contains(QStringLiteral("timerMinutes"))) {
        settings.timerMinutes = values.value(QStringLiteral("timerMinutes")).toInt();
    }
    if (values.contains(QStringLiteral("updateOnPause"))) {
        settings.updateOnPause = values.value(QStringLiteral("updateOnPause")).toBool();
    }
    if (values.contains(QStringLiteral("transition"))) {
        settings.transition = playlistTransitionFromKey(values.value(QStringLiteral("transition")).toString());
    }
    if (values.contains(QStringLiteral("transitionSeconds"))) {
        settings.transitionSeconds = values.value(QStringLiteral("transitionSeconds")).toDouble();
    }
    if (values.contains(QStringLiteral("alwaysBeginFirst"))) {
        settings.alwaysBeginFirst = values.value(QStringLiteral("alwaysBeginFirst")).toBool();
    }
    if (values.contains(QStringLiteral("introOnStartup"))) {
        settings.introOnStartup = values.value(QStringLiteral("introOnStartup")).toBool();
    }
    if (values.contains(QStringLiteral("videoSequence"))) {
        settings.videoSequence = values.value(QStringLiteral("videoSequence")).toBool();
    }
    if (values.contains(QStringLiteral("daytimeAnchors"))) {
        QVector<int> anchors;
        for (const QVariant& value : values.value(QStringLiteral("daytimeAnchors")).toList()) {
            anchors.push_back(value.toInt());
        }
        settings.daytimeAnchors = anchors;
    }
    if (values.contains(QStringLiteral("dayOfWeekOrder"))) {
        QVector<int> weekOrder;
        for (const QVariant& value : values.value(QStringLiteral("dayOfWeekOrder")).toList()) {
            const int day = value.toInt();
            if (day >= 0 && day <= 6 && !weekOrder.contains(day)) weekOrder.push_back(day);
        }
        if (!weekOrder.isEmpty()) settings.dayOfWeekOrder = weekOrder;
    }
}

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

QString downloadStateKey(DownloadStateKind kind) {
    switch (kind) {
    case DownloadStateKind::Queued: return QStringLiteral("queued");
    case DownloadStateKind::Starting: return QStringLiteral("starting");
    case DownloadStateKind::Downloading: return QStringLiteral("downloading");
    case DownloadStateKind::Validating: return QStringLiteral("validating");
    case DownloadStateKind::Completed: return QStringLiteral("completed");
    case DownloadStateKind::Failed: return QStringLiteral("failed");
    case DownloadStateKind::Cancelled: return QStringLiteral("cancelled");
    }
    return {};
}

QString steamInstallStateKey(SteamCMDInstallState state) {
    switch (state) {
    case SteamCMDInstallState::Detecting: return QStringLiteral("detecting");
    case SteamCMDInstallState::Found: return QStringLiteral("found");
    case SteamCMDInstallState::NotFound: return QStringLiteral("notFound");
    case SteamCMDInstallState::Downloading: return QStringLiteral("downloading");
    case SteamCMDInstallState::Extracting: return QStringLiteral("extracting");
    case SteamCMDInstallState::Initializing: return QStringLiteral("initializing");
    case SteamCMDInstallState::Installed: return QStringLiteral("installed");
    case SteamCMDInstallState::Failed: return QStringLiteral("failed");
    }
    return {};
}

QString steamLoginStateKey(SteamLoginState state) {
    switch (state) {
    case SteamLoginState::Idle: return QStringLiteral("idle");
    case SteamLoginState::LoggingIn: return QStringLiteral("loggingIn");
    case SteamLoginState::WaitingForGuard: return QStringLiteral("waitingForGuard");
    case SteamLoginState::Success: return QStringLiteral("success");
    case SteamLoginState::Failed: return QStringLiteral("failed");
    }
    return {};
}

QString steamGuardTypeKey(SteamGuardType type) {
    switch (type) {
    case SteamGuardType::None: return QStringLiteral("");
    case SteamGuardType::Email: return QStringLiteral("email");
    case SteamGuardType::Mobile: return QStringLiteral("mobile");
    case SteamGuardType::MobileConfirm: return QStringLiteral("mobileConfirm");
    }
    return {};
}

bool isActiveDownload(DownloadStateKind kind) {
    return kind == DownloadStateKind::Starting || kind == DownloadStateKind::Downloading ||
           kind == DownloadStateKind::Validating;
}

struct DiscoverSectionDefinition {
    DiscoverCollection collection;
    const char* title;
};

constexpr DiscoverSectionDefinition kDiscoverSections[] = {
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
    , m_playlist(&m_library, &m_renderer, this) {
    m_firstLaunch = QSettings().value(QStringLiteral("IsFirstLaunch"), true).toBool();
    m_renderer.setWallpaperTrustChecker([this](const Wallpaper& item) {
        return isWallpaperTrusted(item.id());
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
    connect(&m_steamCMD, &SteamCMDManager::installStateChanged, this,
            [this](SteamCMDInstallState state, double progress, const QString& message) {
                m_steamInstallState = steamInstallStateKey(state);
                m_steamInstallProgress = progress;
                m_steamInstallMessage = message;
                emit steamChanged();
            });
    connect(&m_steamCMD, &SteamCMDManager::loginStateChanged, this,
            [this](SteamLoginState state, const QString& message) {
                m_steamLoginState = steamLoginStateKey(state);
                m_steamLoginMessage = message;
                emit steamChanged();
            });
    connect(&m_steamCMD, &SteamCMDManager::guardTypeChanged, this,
            [this](SteamGuardType) { emit steamChanged(); });
    connect(&m_steamCMD, &SteamCMDManager::diagnosticEvent, this,
            [this](const QString& line) {
                m_steamLoginLog.append(line);
                if (m_steamLoginLog.size() > 500)
                    m_steamLoginLog.remove(0, m_steamLoginLog.size() - 500);
                emit steamChanged();
            });
    connect(&m_steamCMD, &SteamCMDManager::steamCMDPathChanged, this,
            [this](const QString&) { emit steamChanged(); });
    connect(&m_steamCMD, &SteamCMDManager::authenticationChanged, this,
            [this](bool, const QString&) {
                emit steamChanged();
                emit workshopStateChanged();
            });
    connect(&m_steamCMD, &SteamCMDManager::sessionReusableChanged, this,
            [this](bool) { emit steamChanged(); });
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
    restoreStartupPlayback();
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

QVariantList MirageController::discoverSections() const {
    QVariantList result;
    for (const DiscoverSectionDefinition& definition : kDiscoverSections) {
        const QVector<WorkshopItem>& items = m_workshop.discoverItems(definition.collection);
        if (items.isEmpty()) continue;
        QVariantList sectionItems;
        sectionItems.reserve(items.size());
        for (const WorkshopItem& item : items) sectionItems.append(workshopItemMap(item));
        result.append(QVariantMap{
            {QStringLiteral("title"), QString::fromUtf8(definition.title)},
            {QStringLiteral("items"), sectionItems},
        });
    }
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

bool MirageController::hasDownloadHistory() const {
    for (const WorkshopDownloadTask& task : m_workshop.downloadQueue()) {
        if (task.state.kind == DownloadStateKind::Completed ||
            task.state.kind == DownloadStateKind::Failed ||
            task.state.kind == DownloadStateKind::Cancelled) return true;
    }
    return false;
}

bool MirageController::steamReady() const { return m_workshop.steamSetupState() == SteamSetupState::Ready; }
QString MirageController::steamSetupSummary() const { return m_workshop.steamSetupSummary(); }
QString MirageController::steamCMDPath() const { return m_steamCMD.steamCMDPath(); }
QString MirageController::steamUsername() const { return m_steamCMD.savedUsername(); }
bool MirageController::steamLoggedIn() const { return m_steamCMD.isLoggedIn(); }
QString MirageController::steamInstallState() const { return m_steamInstallState; }
double MirageController::steamInstallProgress() const { return m_steamInstallProgress; }
QString MirageController::steamInstallMessage() const { return m_steamInstallMessage; }
QString MirageController::steamLoginState() const { return m_steamLoginState; }
QString MirageController::steamLoginMessage() const { return m_steamLoginMessage; }
QStringList MirageController::steamLoginLog() const { return m_steamLoginLog; }
QString MirageController::steamGuardType() const { return steamGuardTypeKey(m_steamCMD.steamGuardType()); }
bool MirageController::steamSessionReusable() const { return m_steamCMD.sessionReusable(); }
QStringList MirageController::steamDiagnosticEvents() const { return m_steamCMD.diagnosticEvents(); }

bool MirageController::firstLaunch() const {
    return m_firstLaunch;
}

QString MirageController::statusMessage() const {
    return m_statusMessage;
}

QVariantMap MirageController::settings() const {
    return settingsMap(m_settings.settings());
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
    QVariantList result;
    const QList<QScreen*> screens = QGuiApplication::screens();
    result.reserve(qMax(1, screens.size()));

    if (screens.isEmpty()) {
        result.append(QVariantMap{
            {QStringLiteral("index"), 0},
            {QStringLiteral("name"), QStringLiteral("显示器 1")},
            {QStringLiteral("width"), 0},
            {QStringLiteral("height"), 0},
            {QStringLiteral("primary"), true},
            {QStringLiteral("running"), m_renderer.isRunningOnScreen(0)},
            {QStringLiteral("wallpaperTitle"), QString()},
        });
        return result;
    }

    for (int index = 0; index < screens.size(); ++index) {
        const QScreen* screen = screens.at(index);
        const QString wallpaperId = m_renderer.wallpaperIdOnScreen(index);
        const Wallpaper runningWallpaper = wallpaper(wallpaperId);
        const QRect geometry = screen->geometry();
        result.append(QVariantMap{
            {QStringLiteral("index"), index},
            {QStringLiteral("name"), screen->name().isEmpty()
                ? QStringLiteral("显示器 %1").arg(index + 1) : screen->name()},
            {QStringLiteral("width"), geometry.width()},
            {QStringLiteral("height"), geometry.height()},
            {QStringLiteral("primary"), screen == QGuiApplication::primaryScreen()},
            {QStringLiteral("running"), m_renderer.isRunningOnScreen(index)},
            {QStringLiteral("wallpaperTitle"), runningWallpaper.isValid()
                ? runningWallpaper.project.title : QString()},
        });
    }
    return result;
}

QVariantList MirageController::savedPlaylists() const {
    QVariantList result;
    const QVector<Playlist> playlists = m_playlist.saved();
    result.reserve(playlists.size());
    for (const Playlist& playlist : playlists) result.append(playlistMap(playlist));
    return result;
}

QVariantMap MirageController::playlistSettings() const {
    return playlistSettingsMap(m_playlist.current(m_playlistScreen).settings);
}

double MirageController::selectedVolume() const {
    const Wallpaper item = wallpaper(m_selectedWallpaperId);
    return item.isValid() ? m_runtimeStore.loadRuntime(item).volume : 1.0;
}

double MirageController::selectedSpeed() const {
    const Wallpaper item = wallpaper(m_selectedWallpaperId);
    return item.isValid() ? m_runtimeStore.loadRuntime(item).speed : 1.0;
}

QString MirageController::selectedFillMode() const {
    const Wallpaper item = wallpaper(m_selectedWallpaperId);
    if (!item.isValid()) return QStringLiteral("cover");
    return RendererController::fillModeKey(m_runtimeStore.loadRuntime(item).fillMode);
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
    if (m_sessionTrustedWallpapers.contains(id)) return true;
    return QSettings().value(QStringLiteral("TrustedWallpapers")).toStringList().contains(id);
}

void MirageController::trustWallpaper(const QString& id, bool persist) {
    const Wallpaper item = wallpaper(id);
    if (!item.isValid() || item.kind() != WallpaperKind::Web) return;

    m_sessionTrustedWallpapers.insert(id);
    if (!persist) return;

    QSettings settings;
    QStringList trusted = settings.value(QStringLiteral("TrustedWallpapers")).toStringList();
    if (!trusted.contains(id)) {
        trusted.append(id);
        settings.setValue(QStringLiteral("TrustedWallpapers"), trusted);
    }
}

void MirageController::applySelected(bool allScreens) {
    apply(wallpaper(m_selectedWallpaperId), allScreens);
}

void MirageController::applyWallpaper(const QString& id, bool allScreens) {
    selectWallpaper(id);
    apply(wallpaper(id), allScreens);
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
    clearWallpaperTrust(item.id());
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
    m_renderer.stopAll();
    setStatusMessage(QStringLiteral("已停止动态壁纸"));
}

void MirageController::applySelectedToScreen(int screen) {
    const Wallpaper item = wallpaper(m_selectedWallpaperId);
    if (!item.isValid()) return;

    const int target = qBound(0, screen, screenCount() - 1);
    QString error;
    if (m_renderer.render(item, target, renderOptionsFor(item), &error)) {
        m_playlist.setCurrentWallpaper(target, item);
        m_playlist.kickRotator(target);
        emit playlistChanged();
        setStatusMessage(QStringLiteral("已应用到显示器 %1").arg(target + 1));
    } else if (!error.isEmpty()) {
        setStatusMessage(error);
    }
}

void MirageController::stopScreen(int screen) {
    if (screen < 0 || screen >= screenCount()) return;
    m_renderer.stop(screen);
    setStatusMessage(QStringLiteral("已停止显示器 %1 的动态壁纸").arg(screen + 1));
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
    QString error;
    if (m_renderer.render(item, m_playlistScreen, renderOptionsFor(item), &error)) {
        m_playlist.setCurrentWallpaper(m_playlistScreen, item);
        m_playlist.kickRotator(m_playlistScreen);
        selectWallpaper(item.id());
        setStatusMessage(QStringLiteral("已应用壁纸"));
    } else if (!error.isEmpty()) {
        setStatusMessage(error);
    }
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

void MirageController::removeSelectedPlaylistItem(const QString& id) {
    removePlaylistItem(id);
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

void MirageController::resetPlaylistSettings() {
    m_playlist.resetSettings(m_playlistScreen);
}

void MirageController::setSelectedProperty(const QString& key, const QVariant& value) {
    const Wallpaper item = wallpaper(m_selectedWallpaperId);
    if (!item.isValid()) return;
    const ProjectProperty property = m_runtimeStore.setProperty(item, key, value);
    if (property.type.isEmpty()) return;
    if (item.kind() == WallpaperKind::Scene || item.kind() == WallpaperKind::Web) {
        m_renderer.setProperty(key, property);
    }
}

void MirageController::resetSelectedProperties() {
    const Wallpaper item = wallpaper(m_selectedWallpaperId);
    if (!item.isValid()) return;
    m_runtimeStore.resetRuntime(item);
    for (int screen : m_renderer.activeScreens()) {
        if (m_playlist.currentWallpaper(screen).id() != item.id()) continue;
        QString error;
        m_renderer.render(item, screen, renderOptionsFor(item), &error);
    }
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
    m_steamInstallState = QStringLiteral("detecting");
    emit steamChanged();
    m_steamCMD.detectSteamCMD();
}

void MirageController::installSteamCMD() {
    m_steamCMD.installSteamCMD();
}

void MirageController::cancelSteamCMDInstallation() {
    m_steamCMD.cancelInstallation();
}

void MirageController::loginSteam(const QString& username, const QString& password) {
    m_steamLoginLog.clear();
    emit steamChanged();
    m_steamCMD.login(username, password);
}

void MirageController::submitSteamGuardCode(const QString& code) {
    m_steamCMD.submitGuardCode(code);
}

void MirageController::confirmSteamMobileLogin() {
    m_steamCMD.confirmMobileLogin();
}

void MirageController::useSavedSteamSession() {
    m_steamLoginState = QStringLiteral("loggingIn");
    m_steamLoginMessage = QStringLiteral("正在验证已保存的 SteamCMD 会话");
    emit steamChanged();
    m_steamCMD.refreshSession();
}

void MirageController::cancelSteamLogin() {
    m_steamCMD.cancelLogin();
}

void MirageController::cancelPendingSteamWork() {
    m_steamCMD.cancelLogin();
    m_steamCMD.cancelInstallation();
}

void MirageController::logoutSteam() {
    m_steamCMD.logout();
    m_steamLoginState = QStringLiteral("idle");
    m_steamLoginMessage = QStringLiteral("未登录");
    emit steamChanged();
}

void MirageController::copySteamLoginLog() {
    QGuiApplication::clipboard()->setText(m_steamLoginLog.join(QStringLiteral("\n")));
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
    m_renderer.pause();
}

void MirageController::resumeWallpapers() {
    m_renderer.resume();
}

void MirageController::muteWallpapers() {
    m_renderer.setMuted(true);
}

void MirageController::unmuteWallpapers() {
    m_renderer.setMuted(false);
}

void MirageController::reloadCurrentWallpaper() {
    const Wallpaper item = wallpaper(m_selectedWallpaperId);
    if (!item.isValid()) return;
    for (const int screen : m_renderer.activeScreens()) {
        QString error;
        m_renderer.render(item, screen, renderOptionsFor(item), &error);
        if (!error.isEmpty()) setStatusMessage(error);
    }
}

void MirageController::resetTrustedWallpapers() {
    QSettings settings;
    settings.setValue(QStringLiteral("TrustedWallpapers"), QStringList());
    m_sessionTrustedWallpapers.clear();
    setStatusMessage(QStringLiteral("已重置所有已信任壁纸"));
}

bool MirageController::applySettings(const QVariantMap& values) {
    GlobalSettings updated = m_settings.settings();
    updateSettingsFromMap(updated, values);
    QString error;
    if (m_settings.setSettings(updated, &error)) return true;
    setStatusMessage(error.isEmpty() ? QStringLiteral("设置保存失败") : error);
    return false;
}

void MirageController::setPlaylistScreen(int screen) {
    const int selected = qBound(0, screen, screenCount() - 1);
    if (selected == m_playlistScreen) return;
    m_playlistScreen = selected;
    m_playlist.ensureScreen(m_playlistScreen);
    emit playlistChanged();
}

void MirageController::setSelectedVolume(double volume) {
    const Wallpaper item = wallpaper(m_selectedWallpaperId);
    if (!item.isValid()) return;
    m_runtimeStore.setVolume(item, volume);
    const GlobalSettings& settings = m_settings.settings();
    const WallpaperRuntimeState runtime = m_runtimeStore.loadRuntime(item);
    m_renderer.setVolume(runtime.volume * settings.masterVolume);
    m_renderer.setMuted(runtime.muted || settings.globalMuted || runtime.volume <= 0.0);
    emit selectedRuntimeChanged();
}

void MirageController::setSelectedSpeed(double speed) {
    const Wallpaper item = wallpaper(m_selectedWallpaperId);
    if (!item.isValid()) return;
    m_runtimeStore.setSpeed(item, speed);
    m_renderer.setSpeed(speed);
    emit selectedRuntimeChanged();
}

void MirageController::setSelectedFillMode(const QString& mode) {
    const Wallpaper item = wallpaper(m_selectedWallpaperId);
    if (!item.isValid()) return;
    const FillMode fillMode = mode == QStringLiteral("contain")
        ? FillMode::Contain
        : mode == QStringLiteral("stretch") ? FillMode::Stretch : FillMode::Cover;
    m_runtimeStore.setFillMode(item, fillMode);
    m_renderer.setFillMode(fillMode);
    emit selectedRuntimeChanged();
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

QVariantMap MirageController::wallpaperMap(const Wallpaper& item) const {
    if (!item.isValid()) return {};
    QString typeKey = item.isPreset() ? QStringLiteral("preset") : wallpaperKindKey(item.kind());
    if (item.kind() == WallpaperKind::Unsupported
        && item.project.type.compare(QStringLiteral("application"), Qt::CaseInsensitive) == 0) {
        typeKey = QStringLiteral("application");
    }
    const QString typeLabel = typeKey == QStringLiteral("preset")
        ? QStringLiteral("预设")
        : typeKey == QStringLiteral("application") ? QStringLiteral("应用程序") : wallpaperKindName(item.kind());
    const QString importedRoot = QDir::cleanPath(m_library.importedDirectory()) + QDir::separator();
    const QString wallpaperPath = QDir::cleanPath(item.wallpaperDirectory) + QDir::separator();
    const qint64 size = QFileInfo(item.previewPath()).size() + QFileInfo(item.entryPath()).size();
    const QString searchText = QStringList{item.project.title,
                                           item.project.resolvedAuthor(),
                                           item.project.type,
                                           item.project.description,
                                           item.project.workshopId,
                                           QFileInfo(item.wallpaperDirectory).fileName(),
                                           item.project.tags.join(' ')}.join(' ');
    return {
        {QStringLiteral("id"), item.id()},
        {QStringLiteral("title"), item.project.title},
        {QStringLiteral("author"), item.project.resolvedAuthor()},
        {QStringLiteral("type"), typeKey},
        {QStringLiteral("typeLabel"), typeLabel},
        {QStringLiteral("kind"), wallpaperKindKey(item.kind())},
        {QStringLiteral("preview"), QUrl::fromLocalFile(item.previewPath())},
        {QStringLiteral("tags"), item.project.tags},
        {QStringLiteral("description"), item.project.description},
        {QStringLiteral("location"), QUrl::fromLocalFile(item.wallpaperDirectory)},
        {QStringLiteral("favorite"), m_favorites.contains(item.id())},
        {QStringLiteral("customizable"), !item.project.properties.isEmpty()},
        {QStringLiteral("approved"), item.project.approved},
        {QStringLiteral("rating"), item.project.contentRating},
        {QStringLiteral("source"), wallpaperPath.startsWith(importedRoot)
                                        ? QStringLiteral("imported") : QStringLiteral("workshop")},
        {QStringLiteral("size"), size},
        {QStringLiteral("searchText"), searchText},
        {QStringLiteral("preset"), item.isPreset()},
        {QStringLiteral("presetStatus"), item.presetStatusDescription()},
    };
}

QVariantMap MirageController::workshopItemMap(const WorkshopItem& item) const {
    QString downloadState;
    QString downloadMessage;
    double downloadProgress = -1.0;
    bool downloadActive = false;
    if (const std::optional<DownloadState> state = m_workshop.downloadStateFor(item.publishedFileId)) {
        downloadState = downloadStateKey(state->kind);
        downloadMessage = state->message;
        downloadProgress = state->percent;
        downloadActive = isActiveDownload(state->kind);
    }
    return {
        {QStringLiteral("id"), item.publishedFileId},
        {QStringLiteral("title"), item.title},
        {QStringLiteral("description"), item.description},
        {QStringLiteral("preview"), item.previewImageUrl},
        {QStringLiteral("tags"), item.tags},
        {QStringLiteral("type"), item.isPreset() ? QStringLiteral("preset") : item.wallpaperType},
        {QStringLiteral("typeLabel"), item.displayTypeName()},
        {QStringLiteral("rating"), item.ageRating},
        {QStringLiteral("subscriptions"), item.formattedSubscriptions()},
        {QStringLiteral("favorited"), item.formattedFavorited()},
        {QStringLiteral("views"), item.formattedViews()},
        {QStringLiteral("creatorSteamId"), item.creatorSteamId},
        {QStringLiteral("size"), item.fileSize},
        {QStringLiteral("sizeLabel"), item.formattedFileSize()},
        {QStringLiteral("updatedAt"), item.timeUpdated.toString(Qt::ISODate)},
        {QStringLiteral("downloaded"), m_workshop.isItemDownloaded(item.publishedFileId)},
        {QStringLiteral("needsDependency"), m_workshop.presetNeedsDependency(item.publishedFileId)},
        {QStringLiteral("downloadState"), downloadState},
        {QStringLiteral("downloadMessage"), downloadMessage},
        {QStringLiteral("downloadProgress"), downloadProgress},
        {QStringLiteral("downloadActive"), downloadActive},
    };
}

QVariantMap MirageController::playlistMap(const Playlist& playlist) const {
    return {
        {QStringLiteral("id"), playlist.id.toString(QUuid::WithoutBraces)},
        {QStringLiteral("name"), playlist.name},
        {QStringLiteral("itemCount"), playlist.items.size()},
        {QStringLiteral("updatedAt"), playlist.updatedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))},
    };
}

QVariantMap MirageController::propertyMap(const QString& key, const ProjectProperty& property) const {
    QVariantList options;
    options.reserve(property.options.size());
    for (const ProjectPropertyOption& option : property.options) {
        options.append(QVariantMap{
            {QStringLiteral("label"), option.label},
            {QStringLiteral("value"), option.value},
            {QStringLiteral("condition"), option.condition},
        });
    }
    return {
        {QStringLiteral("key"), key},
        {QStringLiteral("text"), property.text},
        {QStringLiteral("type"), property.type.toLower()},
        {QStringLiteral("condition"), property.condition},
        {QStringLiteral("value"), property.value},
        {QStringLiteral("min"), property.min},
        {QStringLiteral("max"), property.max},
        {QStringLiteral("step"), property.step},
        {QStringLiteral("hasMin"), property.hasMin},
        {QStringLiteral("hasMax"), property.hasMax},
        {QStringLiteral("hasStep"), property.hasStep},
        {QStringLiteral("fraction"), property.fraction},
        {QStringLiteral("options"), options},
    };
}

RenderOptions MirageController::renderOptionsFor(const Wallpaper& item) const {
    const GlobalSettings& settings = m_settings.settings();
    const WallpaperRuntimeState runtime = m_runtimeStore.loadRuntime(item);
    RenderOptions options;
    options.fps = settings.fps;
    options.volume = runtime.volume * settings.masterVolume;
    options.muted = runtime.muted || settings.globalMuted || runtime.volume <= 0.0;
    options.speed = runtime.speed;
    options.fillMode = runtime.fillMode;
    options.enableSpectrum = settings.enableSpectrum;
    options.loadFromMemory = settings.wallpaperLoadSource == QStringLiteral("memory");
    options.userProperties = m_runtimeStore.effectiveProperties(item, runtime);
    return options;
}

void MirageController::apply(const Wallpaper& item, bool allScreens) {
    if (!item.isValid()) return;
    selectWallpaper(item.id());

    bool applied = false;
    QString error;
    const RenderOptions options = renderOptionsFor(item);
    const int count = allScreens ? qMax(1, QGuiApplication::screens().size()) : 1;
    for (int screen = 0; screen < count; ++screen) {
        QString screenError;
        if (m_renderer.render(item, screen, options, &screenError)) {
            applied = true;
            m_playlist.setCurrentWallpaper(screen, item);
            m_playlist.kickRotator(screen);
        }
        if (!screenError.isEmpty()) error = screenError;
    }
    if (applied) {
        emit playlistChanged();
        setStatusMessage(allScreens ? QStringLiteral("已应用到所有显示器") : QStringLiteral("已应用壁纸"));
    } else if (!error.isEmpty()) {
        setStatusMessage(error);
    }
}

void MirageController::restoreStartupPlayback() {
    const QHash<int, QString> lastApplied = m_playlist.lastAppliedIDs();
    for (auto it = lastApplied.constBegin(); it != lastApplied.constEnd(); ++it) {
        const Wallpaper item = m_playlist.resolveWallpaper(it.value());
        if (!item.isValid()) continue;
        QString error;
        if (m_renderer.render(item, it.key(), renderOptionsFor(item), &error)) {
            m_playlist.setCurrentWallpaper(it.key(), item);
        }
    }
}

void MirageController::setStatusMessage(const QString& message) {
    if (message.isEmpty()) return;
    m_statusMessage = message;
    emit statusMessageChanged();
}

void MirageController::clearWallpaperTrust(const QString& id) {
    m_sessionTrustedWallpapers.remove(id);

    QSettings settings;
    QStringList trusted = settings.value(QStringLiteral("TrustedWallpapers")).toStringList();
    if (trusted.removeAll(id) > 0) {
        settings.setValue(QStringLiteral("TrustedWallpapers"), trusted);
    }
}

} // namespace Mirage
