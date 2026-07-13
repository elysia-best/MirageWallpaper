//
//  Mirage Wallpaper
//
//  Copyright © 2026 王孝慈. All rights reserved.
//

import SwiftUI
import Combine

class WorkshopViewModel: ObservableObject {
    // MARK: - Browse State

    @Published var items: [WorkshopItem] = []
    @Published var searchText: String = ""
    @Published var selectedTags: Set<String> = []
    @Published var sortOrder: WorkshopSortOrder = .trending
    @Published var typeFilter: WorkshopTypeFilter = .all
    @Published var currentPage: Int = 1
    @Published var totalItems: Int = 0
    @Published var isLoading: Bool = false
    @Published var error: String?

    @Published var selectedItem: WorkshopItem?
    @Published var showCustomization: Bool = false

    let itemsPerPage = 30

    // MARK: - Discover State

    @Published var trendingItems: [WorkshopItem] = []
    @Published var mostRecentItems: [WorkshopItem] = []
    @Published var mostSubscribedItems: [WorkshopItem] = []
    @Published var topRatedItems: [WorkshopItem] = []
    @Published var animeItems: [WorkshopItem] = []
    @Published var natureItems: [WorkshopItem] = []
    @Published var abstractItems: [WorkshopItem] = []
    @Published var landscapeItems: [WorkshopItem] = []
    @Published var isDiscoverLoading: Bool = false
    @Published var bannerItems: [WorkshopItem] = []

    // MARK: - Download State

    @Published var downloadQueue: [DownloadTask] = []
    @Published var downloadHistory: [DownloadTask] = []

    // MARK: - Sync State
    // MARK: - Steam service state

    @Published var steamSetupState: SteamSetupState = .notConfigured
    @Published var steamServiceStatus = SteamServiceStatus()
    @Published var logoutResultMessage: String?
    @Published var isLoggingOut = false

    var totalPages: Int {
        max(1, Int(ceil(Double(totalItems) / Double(itemsPerPage))))
    }

    var activeDownloadCount: Int {
        downloadQueue.filter {
            if case .downloading = $0.state { return true }
            if case .starting = $0.state { return true }
            if case .validating = $0.state { return true }
            return false
        }.count
    }

    private var searchDebounce: AnyCancellable?
    private var serviceStateCancellables = Set<AnyCancellable>()

    init() {
        searchDebounce = $searchText
            .debounce(for: .milliseconds(500), scheduler: RunLoop.main)
            .removeDuplicates()
            .sink { [weak self] _ in
                self?.currentPage = 1
                self?.search()
            }

        SteamCMDManager.shared.$isLoggedIn
            .receive(on: RunLoop.main)
            .sink { [weak self] _ in self?.refreshSetupState() }
            .store(in: &serviceStateCancellables)

        SteamCMDManager.shared.$authenticationState
            .receive(on: RunLoop.main)
            .sink { [weak self] state in
                self?.steamServiceStatus.authentication = state
                self?.refreshSetupState()
            }
            .store(in: &serviceStateCancellables)
    }

    // MARK: - Setup Check

    func checkSteamSetup() {
        let cmdManager = SteamCMDManager.shared
        steamServiceStatus.steamCMD = .checking
        DispatchQueue.global(qos: .utility).async { [weak self] in
            let path = cmdManager.detectSteamCMD()
            DispatchQueue.main.async {
                guard let self else { return }
                if let path {
                    self.steamServiceStatus.steamCMD = .available(path.path)
                } else {
                    self.steamServiceStatus.steamCMD = .unavailable("未安装 SteamCMD")
                }
                self.steamServiceStatus.authentication = cmdManager.authenticationState
                self.refreshSetupState()
            }
        }
    }

    private func refreshSetupState() {
        let cmdManager = SteamCMDManager.shared
        if cmdManager.steamCMDPath == nil {
            steamSetupState = .steamCMDMissing
            steamServiceStatus.workshopDownload = .needsAction("需要先安装 SteamCMD")
        } else if !cmdManager.isLoggedIn {
            steamSetupState = .needsLogin
            if cmdManager.savedUsername.isEmpty {
                steamServiceStatus.authentication = .needsAction("需要登录 Steam")
            }
            steamServiceStatus.workshopDownload = .needsAction("需要有效的 Steam 会话")
        } else {
            steamSetupState = .ready
            steamServiceStatus.authentication = .available("会话已验证")
            if case .unknown = steamServiceStatus.workshopDownload {
                steamServiceStatus.workshopDownload = .needsAction("尚未开始下载")
            }
        }
    }

    // MARK: - Search

    func search() {
        guard !isLoading else { return }
        isLoading = true
        error = nil
        steamServiceStatus.browsingAPI = .checking

        Task { @MainActor in
            do {
                let result = try await SteamWebAPI.shared.queryFiles(
                    searchText: searchText,
                    tags: Array(selectedTags),
                    sortOrder: sortOrder,
                    typeFilter: typeFilter,
                    page: currentPage,
                    perPage: itemsPerPage
                )

                // 创意工坊在线数据不含 approved/mobile/audio/customizable 标志，
                // "仅显示"分区仅为与「已安装」保持 UI 一致，不收窄结果，避免空列表。
                self.items = result.items
                self.totalItems = result.total
                self.isLoading = false
                self.steamServiceStatus.browsingAPI = .available("Steam Web API 可用")
            } catch {
                self.error = error.localizedDescription
                self.isLoading = false
                self.steamServiceStatus.browsingAPI = .unavailable(error.localizedDescription)
            }
        }
    }

    func loadNextPage() {
        guard currentPage < totalPages else { return }
        currentPage += 1
        search()
    }

    func loadPreviousPage() {
        guard currentPage > 1 else { return }
        currentPage -= 1
        search()
    }

    func goToPage(_ page: Int) {
        let clamped = max(1, min(page, totalPages))
        guard clamped != currentPage else { return }
        currentPage = clamped
        search()
    }

    func applyTagFilter(_ tag: String) {
        if selectedTags.contains(tag) {
            selectedTags.remove(tag)
        } else {
            selectedTags.insert(tag)
        }
        currentPage = 1
        search()
    }

    func clearFilters() {
        selectedTags.removeAll()
        searchText = ""
        typeFilter = .all
        sortOrder = .trending
        currentPage = 1
        search()
    }

    // MARK: - Discover

    func loadDiscover() {
        guard !isDiscoverLoading else { return }
        isDiscoverLoading = true

        Task { @MainActor in
            async let trending = SteamWebAPI.shared.fetchTrending(count: 15)
            async let recent = SteamWebAPI.shared.fetchMostRecent(count: 10)
            async let subscribed = SteamWebAPI.shared.fetchMostSubscribed(count: 10)
            async let rated = SteamWebAPI.shared.fetchTopRated(count: 10)
            async let anime = SteamWebAPI.shared.fetchByTag("Anime", count: 10)
            async let nature = SteamWebAPI.shared.fetchByTag("Nature", count: 10)
            async let abstract = SteamWebAPI.shared.fetchByTag("Abstract", count: 10)
            async let landscape = SteamWebAPI.shared.fetchByTag("Landscape", count: 10)

            do {
                self.trendingItems = try await trending
                self.mostRecentItems = try await recent
                self.mostSubscribedItems = try await subscribed
                self.topRatedItems = try await rated
                self.animeItems = try await anime
                self.natureItems = try await nature
                self.abstractItems = try await abstract
                self.landscapeItems = try await landscape

                self.bannerItems = Array(self.trendingItems.prefix(5))
            } catch {
                NSLog("[Mirage] 加载发现页失败: \(error.localizedDescription)")
            }
            self.isDiscoverLoading = false
        }
    }

    // MARK: - Download

    func downloadItem(_ item: WorkshopItem) {
        guard !downloadQueue.contains(where: { $0.id == item.publishedFileId }) else { return }

        let task = DownloadTask(
            workshopItem: item,
            state: .queued,
            startedAt: nil,
            completedAt: nil
        )
        downloadQueue.append(task)
        processDownloadQueue()
    }

    func cancelDownload(_ item: WorkshopItem) {
        SteamCMDManager.shared.cancelDownload(workshopId: item.publishedFileId)
        downloadQueue.removeAll { $0.id == item.publishedFileId }
    }

    func retryDownload(_ task: DownloadTask) {
        downloadQueue.removeAll { $0.id == task.id }
        downloadItem(task.workshopItem)
    }

    func clearCompletedDownloads() {
        downloadQueue.removeAll {
            if case .completed = $0.state { return true }
            if case .failed = $0.state { return true }
            return false
        }
    }

    func downloadState(for workshopId: String) -> DownloadState? {
        downloadQueue.first(where: { $0.id == workshopId })?.state
    }

    private func processDownloadQueue() {
        let maxConcurrent = 1
        let currentActive = downloadQueue.filter {
            if case .downloading = $0.state { return true }
            if case .starting = $0.state { return true }
            if case .validating = $0.state { return true }
            return false
        }.count

        guard currentActive < maxConcurrent else { return }

        guard let nextIndex = downloadQueue.firstIndex(where: {
            if case .queued = $0.state { return true }
            return false
        }) else { return }

        let workshopId = downloadQueue[nextIndex].workshopItem.publishedFileId
        downloadQueue[nextIndex].state = .starting
        downloadQueue[nextIndex].startedAt = Date()

        SteamCMDManager.shared.downloadItem(
            workshopId: workshopId,
            expectedFileSize: self.downloadQueue[nextIndex].workshopItem.fileSize
        ) { [weak self] state in
            guard let self else { return }
            guard let idx = self.downloadQueue.firstIndex(where: { $0.id == workshopId }) else { return }

            self.downloadQueue[idx].state = state

            if case .completed = state {
                self.steamServiceStatus.workshopDownload = .available("最近一次下载已验证")
                self.downloadQueue[idx].completedAt = Date()
                self.processDownloadQueue()
                NotificationCenter.default.post(name: .workshopItemDownloaded, object: workshopId)
                self.applyDownloadedWallpaper(workshopId: workshopId)
            } else if case .failed = state {
                self.steamServiceStatus.workshopDownload = .unavailable("最近一次下载失败")
                self.processDownloadQueue()
            } else if case .starting = state {
                self.steamServiceStatus.workshopDownload = .checking
            }
        }
    }

    // MARK: - Navigate to Workshop with filter

    func navigateToWorkshopWithTag(_ tag: String) {
        selectedTags = [tag]
        searchText = ""
        typeFilter = .all
        sortOrder = .trending
        showCustomization = false
        currentPage = 1
        search()
    }

    func navigateToWorkshopWithSort(_ sort: WorkshopSortOrder) {
        selectedTags.removeAll()
        searchText = ""
        typeFilter = .all
        sortOrder = sort
        showCustomization = false
        currentPage = 1
        search()
    }

    func logout() {
        guard !isLoggingOut else { return }
        isLoggingOut = true
        steamServiceStatus.authentication = .checking
        SteamCMDManager.shared.logout { [weak self] result in
            guard let self else { return }
            self.isLoggingOut = false
            switch result {
            case .success:
                self.steamServiceStatus.authentication = .needsAction("已退出登录")
                self.logoutResultMessage = "已退出 Steam，并清除了 Mirage 本机保存的 SteamCMD 会话。"
            case .failure(let error):
                self.steamServiceStatus.authentication = .needsAction(error.localizedDescription)
                self.logoutResultMessage = error.localizedDescription
            }
            self.refreshSetupState()
        }
    }

    // MARK: - Auto Apply

    private func applyDownloadedWallpaper(workshopId: String) {
        DispatchQueue.main.asyncAfter(deadline: .now() + 1) { [weak self] in
            let lib = WallpaperLibrary.shared
            for dir in [lib.steamWorkshopDirectory, SteamCMDManager.shared.steamCMDContentDirectory] {
                let wallpaperDir = dir.appending(path: workshopId)
                let projectFile = wallpaperDir.appending(path: "project.json")
                if FileManager.default.fileExists(atPath: projectFile.path) {
                    let wallpaper = WEWallpaper.load(from: wallpaperDir)
                    if wallpaper.isValid {
                        AppDelegate.shared.wallpaperViewModel.nextCurrentWallpaper = wallpaper
                        // 留在创意工坊标签，右侧改显当前壁纸的自定义面板
                        self?.showCustomization = true
                        self?.selectedItem = nil
                        return
                    }
                }
            }
        }
    }
}

// MARK: - Notification Names

extension Notification.Name {
    static let workshopItemDownloaded = Notification.Name("workshopItemDownloaded")
    static let favoritesChanged = Notification.Name("favoritesChanged")
}
