import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import FluentUI
import "ContentViewLogic.js" as ContentViewLogic
import "OptionData.js" as OptionData
import "Components"
import "Components/Discover"
import "Components/Workshop"
import "Components/Playlist"
import "Components/ContextMenus"
import "Components/Alerts"
import "ViewModels"
import "../SettingsView"
import "../SteamSetup"
import "../GlobalComponents"
import "../MenuBars"

FluWindow {
    id: window
    title: "Mirage"
    // Matches MainWindowController's initial and minimum content size.
    width: 1000
    height: 640
    minimumWidth: 1000
    minimumHeight: 640
    // The status bar restores this root window after it is closed. FluentUI's
    // default destroys the window on close, leaving the tray with no target.
    autoDestroy: false
    fitsAppBarWindows: true
    appBar: FluAppBar {
        id: appBar
        // 与 FluAppBar 默认高度一致，窗口按钮组贴顶，标签栏 30px 正好填满。
        height: 30
        titleVisible: false
        icon: ""
        showDark: true
        showStayTop: true
        TopTabBar {
            id: appBarTabs
            height: 30
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.right: appBar.layoutStandardbuttons.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            currentIndex: window.currentTab
            onSelected: index => window.currentTab = index
            onMobileRequested: linuxNotice.open()
            onDisplayRequested: displaySettingsSheet.open()
            onSettingsRequested: settingsSheet.open()
        }
        Component.onCompleted: {
            appBarTabs.registerHitTest(window);
        }
    }

    ContentViewModel {
        id: contentViewModel
    }
    property alias currentTab: contentViewModel.currentTab
    property alias filtersVisible: contentViewModel.filtersVisible
    property alias playlistExpanded: contentViewModel.playlistExpanded
    property alias searchText: contentViewModel.searchText
    property alias approvedOnly: contentViewModel.approvedOnly
    property alias favoritesOnly: contentViewModel.favoritesOnly
    property alias mobileOnly: contentViewModel.mobileOnly
    property alias audioOnly: contentViewModel.audioOnly
    property alias customizableOnly: contentViewModel.customizableOnly
    property alias typeFilters: contentViewModel.typeFilters
    property alias ratingFilters: contentViewModel.ratingFilters
    property alias sourceFilters: contentViewModel.sourceFilters
    property alias tagFilters: contentViewModel.tagFilters
    property alias enabledTypes: contentViewModel.enabledTypes
    property alias enabledRatings: contentViewModel.enabledRatings
    property alias enabledSources: contentViewModel.enabledSources
    property alias enabledTags: contentViewModel.enabledTags
    property alias sortMode: contentViewModel.sortMode
    property alias sortDescending: contentViewModel.sortDescending
    property alias installedSortOptions: contentViewModel.installedSortOptions
    property alias rawWallpapers: contentViewModel.rawWallpapers
    property alias filteredWallpapers: contentViewModel.filteredWallpapers
    property alias explorerIconSize: contentViewModel.explorerIconSize
    property alias wallpapersPerPage: contentViewModel.wallpapersPerPage
    property alias wallpaperCurrentPage: contentViewModel.wallpaperCurrentPage
    property alias wallpaperPageCount: contentViewModel.wallpaperPageCount
    property alias pagedWallpapers: contentViewModel.pagedWallpapers
    property alias workshopSearchText: contentViewModel.workshopSearchText
    property alias workshopSortKey: contentViewModel.workshopSortKey
    property alias workshopType: contentViewModel.workshopType
    property alias workshopRatings: contentViewModel.workshopRatings
    property alias workshopSelectedTags: contentViewModel.workshopSelectedTags
    property alias discoverTrendDays: contentViewModel.discoverTrendDays
    property alias settingsPage: contentViewModel.settingsPage
    property alias settingsDraft: contentViewModel.settingsDraft
    property alias settingsDirty: contentViewModel.settingsDirty
    property alias playlistSettingsDraft: contentViewModel.playlistSettingsDraft
    property alias playlistSaveName: contentViewModel.playlistSaveName
    property alias metadataTitle: contentViewModel.metadataTitle
    property alias metadataTags: contentViewModel.metadataTags
    property alias pendingTrustAction: contentViewModel.pendingTrustAction
    property alias pendingTrustWallpaperId: contentViewModel.pendingTrustWallpaperId

    function openImportPanel() {
        importFileDialog.open();
    }

    function openImportFolderPanel() {
        importFolderDialog.open();
    }

    function openPlaylistOpenSheet() {
        playlistOpenSheet.open();
    }

    function openPlaylistSaveSheet() {
        playlistSaveSheet.open();
    }

    function openPlaylistSettingsSheet() {
        playlistSettingsSheet.open();
    }

    function openPlaylistDeleteConfirmation(playlistId, playlistName) {
        playlistDeleteConfirmation.playlistId = playlistId;
        playlistDeleteConfirmation.playlistName = playlistName;
        playlistDeleteConfirmation.open();
    }

    function openSettingsPanel() {
        settingsSheet.open();
    }

    function openSteamSetup() {
        steamSetupController.open();
    }

    function showLinuxNotice() {
        linuxNotice.open();
    }

    function openWallpaperContextMenu(wallpaper) {
        wallpaperContextMenu.wallpaper = wallpaper;
        wallpaperContextMenu.popup();
    }

    function openPropertyPicker(propertyKey, directory) {
        if (directory) {
            propertyFolderDialog.propertyKey = propertyKey;
            propertyFolderDialog.open();
        } else {
            propertyFileDialog.propertyKey = propertyKey;
            propertyFileDialog.open();
        }
    }
    property var playlistOrderOptions: OptionData.playlistOrderOptions
    property var playlistTimingOptions: OptionData.playlistTimingOptions
    property var playlistTransitionOptions: OptionData.playlistTransitionOptions
    property var playbackOptions: OptionData.playbackOptions
    property var playbackModes: OptionData.playbackModes
    property var weekdayLabels: OptionData.weekdayLabels
    property var workshopSortOptions: OptionData.workshopSortOptions
    property var workshopTypeFilters: OptionData.workshopTypeFilters
    property var workshopTagFilters: OptionData.workshopTagFilters

    function isEnabled(filters, key) {
        return ContentViewLogic.isEnabled(filters, key);
    }

    function setEnabled(propertyName, key, enabled) {
        window[propertyName] = ContentViewLogic.setEnabled(window[propertyName], key, enabled);
    }

    function resetFilters() {
        approvedOnly = false;
        favoritesOnly = false;
        mobileOnly = false;
        audioOnly = false;
        customizableOnly = false;
        enabledTypes = typeFilters.map(function (filter) {
            return filter.key;
        });
        enabledRatings = ratingFilters.map(function (filter) {
            return filter.key;
        });
        enabledSources = sourceFilters.map(function (filter) {
            return filter.key;
        });
        enabledTags = tagFilters.map(function (filter) {
            return filter.key;
        });
    }

    function propertyLabel(propertyData) {
        return ContentViewLogic.propertyLabel(propertyData);
    }

    function propertyOptionItems(options) {
        return ContentViewLogic.propertyOptionItems(options, mirage.selectedProperties);
    }

    function resetDetailScroll() {
        Qt.callLater(function () {
            if (detailScroll.contentItem)
                detailScroll.contentItem.contentY = 0;
        });
    }

    function workshopSortIndex() {
        return ContentViewLogic.findOptionIndex(workshopSortOptions, workshopSortKey);
    }

    function setWorkshopRating(key, enabled) {
        setEnabled("workshopRatings", key, enabled);
        mirage.setWorkshopAgeRatingEnabled(key, enabled);
    }

    function toggleWorkshopTag(key) {
        setEnabled("workshopSelectedTags", key, !isEnabled(workshopSelectedTags, key));
        mirage.toggleWorkshopTag(key);
    }

    function selectAllWorkshopTags() {
        workshopTagFilters.forEach(function(filter) {
            if (!isEnabled(workshopSelectedTags, filter.key))
                toggleWorkshopTag(filter.key);
        });
    }

    function clearWorkshopTags() {
        workshopSelectedTags.slice().forEach(function(tag) {
            toggleWorkshopTag(tag);
        });
    }

    function filterDiscoverItems(items) {
        return ContentViewLogic.filterDiscoverItems(items, {
            workshopType: workshopType,
            workshopRatings: workshopRatings,
            workshopSelectedTags: workshopSelectedTags
        });
    }

    function resetWorkshopFilters() {
        workshopSearchText = "";
        workshopSortKey = "trending";
        workshopType = "all";
        workshopRatings = ["Everyone"];
        workshopSelectedTags = [];
        mirage.clearWorkshopFilters();
    }

    function resetSettingsDraft() {
        var next = {};
        for (var name in mirage.settings)
            next[name] = mirage.settings[name];
        settingsDraft = next;
        settingsDirty = false;
    }

    function cancelSettingsDraft() {
        mirage.previewFps(mirage.settings.fps);
        resetSettingsDraft();
    }

    function setSetting(key, value) {
        if (settingsDraft[key] === value)
            return;
        var next = {};
        for (var name in settingsDraft)
            next[name] = settingsDraft[name];
        next[key] = value;
        settingsDraft = next;
        settingsDirty = true;
        if (key === "fps")
            mirage.previewFps(value);
    }

    function playlistOptionIndex(options, value) {
        return ContentViewLogic.findOptionIndex(options, value);
    }

    function resetPlaylistSettingsDraft() {
        var next = {};
        var source = mirage.playlistSettings || {};
        for (var key in source) {
            next[key] = Array.isArray(source[key]) ? source[key].slice() : source[key];
        }
        playlistSettingsDraft = next;
    }

    function setPlaylistSetting(key, value) {
        var next = {};
        for (var name in playlistSettingsDraft) {
            next[name] = Array.isArray(playlistSettingsDraft[name]) ? playlistSettingsDraft[name].slice() : playlistSettingsDraft[name];
        }
        next[key] = value;
        playlistSettingsDraft = next;
    }

    function screenLabels() {
        return ContentViewLogic.screenLabels(mirage.screenCount);
    }

    function propertyConditionVisible(condition) {
        return ContentViewLogic.propertyConditionVisible(condition, mirage.selectedProperties);
    }

    function propertyOptionIndex(options, value) {
        return ContentViewLogic.findOptionIndex(options, value, "value");
    }

    function propertyBoolValue(value) {
        return ContentViewLogic.propertyBoolValue(value);
    }

    function propertyColor(value) {
        return ContentViewLogic.propertyColor(value);
    }

    function propertyColorValue(color) {
        return ContentViewLogic.propertyColorValue(color);
    }

    function propertyPathLabel(value) {
        return ContentViewLogic.propertyPathLabel(value);
    }

    function needsWallpaperTrust(wallpaper) {
        return wallpaper && wallpaper.type === "web" && !mirage.isWallpaperTrusted(wallpaper.id);
    }

    function runWithWallpaperTrust(wallpaper, action) {
        if (!wallpaper || !wallpaper.id)
            return;
        if (!needsWallpaperTrust(wallpaper)) {
            action();
            return;
        }
        pendingTrustWallpaperId = wallpaper.id;
        pendingTrustAction = action;
        wallpaperTrustSheet.open();
    }

    function confirmWallpaperTrust() {
        var action = pendingTrustAction;
        var wallpaperId = pendingTrustWallpaperId;
        pendingTrustAction = null;
        pendingTrustWallpaperId = "";
        mirage.trustWallpaper(wallpaperId, wallpaperTrustSheet.rememberWallpaper);
        if (action)
            action();
    }

    function cancelWallpaperTrust() {
        pendingTrustAction = null;
        pendingTrustWallpaperId = "";
    }

    function addWallpaperToPlaylist(id, screen) {
        var previousScreen = mirage.playlistScreen;
        mirage.selectWallpaper(id);
        mirage.playlistScreen = screen;
        mirage.addSelectedToPlaylist();
        mirage.playlistScreen = previousScreen;
    }

    function setWallpaperPage(page) {
        wallpaperCurrentPage = Math.max(1, Math.min(wallpaperPageCount, Math.floor(Number(page) || 1)));
        wallpaperGrid.positionViewAtBeginning();
    }

    function wallpaperPageItems() {
        var page = wallpaperCurrentPage;
        var count = wallpaperPageCount;
        if (count <= 7) {
            var all = [];
            for (var index = 1; index <= count; ++index)
                all.push(index);
            return all;
        }
        if (page <= 4)
            return [1, 2, 3, 4, 5, 0, count];
        if (page >= count - 3)
            return [1, 0, count - 4, count - 3, count - 2, count - 1, count];
        return [1, 0, page - 1, page, page + 1, 0, count];
    }

    onFilteredWallpapersChanged: wallpaperCurrentPage = 1
    onWallpapersPerPageChanged: wallpaperCurrentPage = 1
    onWallpaperPageCountChanged: {
        if (wallpaperCurrentPage > wallpaperPageCount)
            wallpaperCurrentPage = wallpaperPageCount;
    }

    onCurrentTabChanged: {
        if (currentTab === 1 && !mirage.discoverLoading && mirage.discoverSections.length === 0) {
            mirage.loadDiscover();
        } else if (currentTab === 2 && !mirage.workshopLoading && mirage.workshopItems.length === 0) {
            mirage.submitWorkshopSearch();
        }
        if (currentTab !== 1) {
            explorerTopBar.searchBox.text = currentTab === 0 ? searchText : workshopSearchText;
        }
        resetDetailScroll();
    }

    Connections {
        target: mirage
        function onSettingsChanged() {
            if (!settingsDirty)
                window.resetSettingsDraft();
        }
        function onInstalledWallpaperSelected() {
            window.currentTab = 0;
        }
        function onSelectedWallpaperChanged() {
            window.resetDetailScroll();
        }
        function onSelectedWorkshopItemChanged() {
            window.resetDetailScroll();
        }
    }

    ExplorerItemMenu {
        id: wallpaperContextMenu
        host: window
        onUnavailableFeatureRequested: linuxNotice.open()
        onDeleteRequested: wallpaperDeleteConfirmation.open()
    }

    FluSplitLayout {
        id: mainSplit
        anchors.fill: parent
        orientation: Qt.Horizontal

        Item {
            SplitView.minimumWidth: 620
            SplitView.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                anchors.bottomMargin: 16
                anchors.topMargin: appBar.height

                ProjectFeedbackBanner {
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                }

                ExplorerTopBar {
                    id: explorerTopBar
                    Layout.fillWidth: true
                    visible: window.currentTab === 0
                    host: window
                    currentTab: window.currentTab
                    searchText: window.currentTab === 0 ? window.searchText : window.workshopSearchText
                    explorerIconSize: window.explorerIconSize
                    wallpapersPerPage: window.wallpapersPerPage
                    sortMode: window.sortMode
                    sortDescending: window.sortDescending
                    onFilterRequested: window.filtersVisible = !window.filtersVisible
                    onRefreshRequested: {
                        if (window.currentTab === 0)
                            mirage.reloadWallpapers();
                        else
                            mirage.submitWorkshopSearch();
                    }
                    onIconSizeChanged: size => window.explorerIconSize = size
                    onPageSizeChanged: count => window.wallpapersPerPage = count
                    onSortChanged: key => {
                        if (key === "direction") {
                            window.sortDescending = !window.sortDescending;
                        } else {
                            var sortIndex = ContentViewLogic.findOptionIndex(window.installedSortOptions, key, "label");
                            window.sortMode = window.installedSortOptions[sortIndex].key;
                        }
                    }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: window.currentTab

                    RowLayout {
                        spacing: 10

                        // FluPage（FluScrollablePage 基类）onCompleted 会强制
                        // visible=true，永久破坏外部 visible 绑定，导致侧栏常驻
                        // 无法收起（同 SettingsView.qml 的页面可见性问题）。
                        // 故用普通 Item 承载显隐，FluScrollablePage 内部
                        // anchors.fill 即可，Item 的 visible 绑定不受影响。
                        Item {
                            Layout.fillHeight: true
                            Layout.preferredWidth: 225
                            visible: window.filtersVisible

                            FilterResults {
                                anchors.fill: parent
                                host: window
                            }
                        }

                        WallpaperExplorer {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            host: window
                        }
                    }

                    RowLayout {
                        spacing: 10
                        // 同 tab 0：FluPage 基类 onCompleted 强制 visible=true，
                        // 需用 Item 承载显隐，否则侧栏无法收起。
                        Item {
                            Layout.fillHeight: true
                            Layout.preferredWidth: 225
                            visible: window.filtersVisible

                            WorkshopFilterSidebar {
                                anchors.fill: parent
                                host: window
                            }
                        }
                        DiscoverView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            host: window
                        }
                    }

                    RowLayout {
                        spacing: 10
                        // 同 tab 0：FluPage 基类 onCompleted 强制 visible=true，
                        // 需用 Item 承载显隐，否则侧栏无法收起。
                        Item {
                            Layout.fillHeight: true
                            Layout.preferredWidth: 225
                            visible: window.filtersVisible

                            WorkshopFilterSidebar {
                                anchors.fill: parent
                                host: window
                            }
                        }
                        WorkshopView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            host: window
                        }
                    }
                }

                ExplorerBottomBar {
                    host: window
                }
            }
        }

        FluScrollablePage {
            id: detailScroll
            SplitView.minimumWidth: 288
            SplitView.preferredWidth: 320
            SplitView.maximumWidth: 340
            SplitView.fillHeight: true

            clip: true

            // 用户修复：FluScrollablePage 内容列高跟随父级
            columnHeight: parent.height

            // The frameless window intentionally lets content fit behind the
            // app bar. Keep the detail preview below that hit-test region so
            // window controls never paint over wallpaper information.
            // 包装层用普通 Item：ColumnLayout 的 fillWidth 在内容
            // 切换（WallpaperPreview↔WorkshopItemDetail）时会被内容
            // 隐式宽覆盖导致变窄，Item 隐式宽为 0，宽度稳定。
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: appBar.height + 16
                Layout.bottomMargin: 16
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                WallpaperPreview {
                    id: wallpaperPreview
                    anchors.fill: parent
                    visible: window.currentTab === 0
                    host: window
                    onMetadataEditRequested: wallpaperMetadataSheet.open()
                    onDeleteRequested: wallpaperDeleteConfirmation.open()
                }

                WorkshopItemDetail {
                    id: workshopDetail
                    anchors.fill: parent
                    visible: window.currentTab !== 0
                    host: window
                }
            }
        }
    }

    FirstLaunchView {
        id: firstLaunchSheet
        host: window
    }

    SettingsView {
        id: settingsSheet
        host: window
    }
    WallpaperMetadataSheet {
        id: wallpaperMetadataSheet
        host: window
    }

    DeleteWallpaperDialog {
        id: wallpaperDeleteConfirmation
    }

    UnsafeWallpaper {
        id: wallpaperTrustSheet
        host: window
    }

    DisplaySettings {
        id: displaySettingsSheet
        host: window
    }

    PlaylistOpenSheet {
        id: playlistOpenSheet
        host: window
    }
    PlaylistDeleteConfirmation {
        id: playlistDeleteConfirmation
    }

    PlaylistSaveSheet {
        id: playlistSaveSheet
        host: window
    }
    PlaylistSettingsSheet {
        id: playlistSettingsSheet
        host: window
    }
    FileDialog {
        id: propertyFileDialog
        property string propertyKey: ""
        title: "选择文件"
        fileMode: FileDialog.OpenFile
        onAccepted: mirage.setSelectedProperty(propertyKey, ContentViewLogic.selectedFilePath(selectedFile))
    }

    FolderDialog {
        id: propertyFolderDialog
        property string propertyKey: ""
        title: "选择目录"
        onAccepted: mirage.setSelectedProperty(propertyKey, ContentViewLogic.selectedFilePath(selectedFolder))
    }

    FileDialog {
        id: importFileDialog
        title: "导入壁纸视频"
        fileMode: FileDialog.OpenFile
        nameFilters: ["视频 (*.mp4 *.mov *.m4v *.webm *.mkv)", "所有文件 (*)"]
        onAccepted: mirage.importWallpaperPath(ContentViewLogic.selectedFilePath(selectedFile))
    }

    FolderDialog {
        id: importFolderDialog
        title: "导入壁纸文件夹"
        onAccepted: mirage.importWallpaperPath(ContentViewLogic.selectedFilePath(selectedFolder))
    }

    SteamSetupView {
        id: steamSetupWindow
        autoVisible: false
    }

    SteamSetupWindowController {
        id: steamSetupController
        window: steamSetupWindow
    }

    LinuxNotice {
        id: linuxNotice
    }

    Connections {
        target: mirage
        function onStatusMessageChanged() {
            if (mirage.statusMessage.length > 0)
                window.showSuccess(mirage.statusMessage);
        }
    }
}
