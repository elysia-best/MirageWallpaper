import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import FluentUI
import "../PropertyLocalization.js" as PropertyLocalization
import "Components"
import "Components/Discover"
import "Components/Workshop"
import "Components/Playlist"
import "../SettingsView"
import "../SteamSetup"
import "../GlobalComponents"

FluWindow {
    id: window
    title: "Mirage 1.0.0"
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
        height: 32
        showDark: true
    }

    property int currentTab: 0
    property bool filtersVisible: false
    property bool playlistExpanded: false
    property string searchText: ""
    property bool approvedOnly: false
    property bool favoritesOnly: false
    property bool mobileOnly: false
    property bool audioOnly: false
    property bool customizableOnly: false
    property var typeFilters: [
        {
            label: "场景",
            key: "scene"
        },
        {
            label: "视频",
            key: "video"
        },
        {
            label: "网页",
            key: "web"
        },
        {
            label: "应用程序",
            key: "application"
        },
        {
            label: "预设",
            key: "preset"
        }
    ]
    property var ratingFilters: [
        {
            label: "所有人",
            key: "Everyone"
        },
        {
            label: "轻度裸露",
            key: "Questionable"
        },
        {
            label: "成人",
            key: "Mature"
        }
    ]
    property var sourceFilters: [
        {
            label: "创意工坊",
            key: "workshop"
        },
        {
            label: "我的壁纸",
            key: "imported"
        }
    ]
    property var tagFilters: [
        {
            label: "抽象",
            key: "abstract"
        },
        {
            label: "动物",
            key: "animal"
        },
        {
            label: "动漫",
            key: "anime"
        },
        {
            label: "卡通",
            key: "cartoon"
        },
        {
            label: "CGI",
            key: "cgi"
        },
        {
            label: "赛博朋克",
            key: "cyberpunk"
        },
        {
            label: "奇幻",
            key: "fantasy"
        },
        {
            label: "游戏",
            key: "game"
        },
        {
            label: "女孩",
            key: "girls"
        },
        {
            label: "男孩",
            key: "guys"
        },
        {
            label: "风景",
            key: "landscape"
        },
        {
            label: "中世纪",
            key: "medieval"
        },
        {
            label: "表情包",
            key: "memes"
        },
        {
            label: "MMD",
            key: "mmd"
        },
        {
            label: "音乐",
            key: "music"
        },
        {
            label: "自然",
            key: "nature"
        },
        {
            label: "像素艺术",
            key: "pixelart"
        },
        {
            label: "治愈",
            key: "relaxing"
        },
        {
            label: "复古",
            key: "retro"
        },
        {
            label: "科幻",
            key: "scifi"
        },
        {
            label: "运动",
            key: "sports"
        },
        {
            label: "科技",
            key: "technology"
        },
        {
            label: "影视",
            key: "television"
        },
        {
            label: "载具",
            key: "vehicle"
        },
        {
            label: "未分类",
            key: "unspecified"
        }
    ]
    property var enabledTypes: typeFilters.map(function (filter) {
        return filter.key;
    })
    property var enabledRatings: ratingFilters.map(function (filter) {
        return filter.key;
    })
    property var enabledSources: sourceFilters.map(function (filter) {
        return filter.key;
    })
    property var enabledTags: tagFilters.map(function (filter) {
        return filter.key;
    })
    property string sortMode: "name"
    property bool sortDescending: false
    property var rawWallpapers: mirage.wallpapers
    property var filteredWallpapers: filterWallpapers(rawWallpapers)
    property int explorerIconSize: 170
    property int wallpapersPerPage: 25
    property int wallpaperCurrentPage: 1
    property int wallpaperPageCount: Math.max(1, Math.ceil(filteredWallpapers.length / wallpapersPerPage))
    property var pagedWallpapers: filteredWallpapers.slice((wallpaperCurrentPage - 1) * wallpapersPerPage, wallpaperCurrentPage * wallpapersPerPage)
    property string workshopSearchText: ""
    property string workshopSortKey: "trending"
    property string workshopType: "all"
    property var workshopRatings: ["Everyone"]
    property var workshopSelectedTags: []
    property int discoverTrendDays: 7
    property int settingsPage: 0
    property var settingsDraft: mirage.settings
    property bool settingsDirty: false
    property var playlistSettingsDraft: ({})
    property string playlistSaveName: ""
    property string metadataTitle: ""
    property string metadataTags: ""
    property var pendingTrustAction: null
    property string pendingTrustWallpaperId: ""
    property string groupNumber: "2160040437"

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
    property var playlistOrderOptions: [
        {
            label: "有序",
            key: "sorted"
        },
        {
            label: "随机",
            key: "random"
        }
    ]
    property var playlistTimingOptions: [
        {
            label: "计时器",
            key: "timer"
        },
        {
            label: "登录时",
            key: "logon"
        },
        {
            label: "当日时间",
            key: "daytime"
        },
        {
            label: "星期",
            key: "dayOfWeek"
        },
        {
            label: "从不",
            key: "never"
        }
    ]
    property var playlistTransitionOptions: [
        {
            label: "启用全部",
            key: "enabled"
        },
        {
            label: "禁用全部",
            key: "disabled"
        },
        {
            label: "随机",
            key: "random"
        }
    ]
    property var playbackOptions: [
        {
            label: "其他应用获得焦点时",
            key: "otherApplicationFocused"
        },
        {
            label: "其他应用全屏时",
            key: "otherApplicationFullscreen"
        },
        {
            label: "其他应用播放音频时",
            key: "otherApplicationPlayingAudio"
        },
        {
            label: "显示器睡眠时",
            key: "displayAsleep"
        },
        {
            label: "笔记本使用电池时",
            key: "laptopOnBattery"
        }
    ]
    property var playbackModes: [
        {
            label: "保持运行",
            key: "keepRunning"
        },
        {
            label: "静音",
            key: "mute"
        },
        {
            label: "暂停",
            key: "pause"
        },
        {
            label: "停止（释放内存）",
            key: "stop"
        }
    ]
    property var weekdayLabels: ["星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"]
    property var workshopSortOptions: [
        {
            label: "热门趋势",
            key: "trending"
        },
        {
            label: "最新发布",
            key: "recent"
        },
        {
            label: "订阅最多",
            key: "subscribed"
        },
        {
            label: "评分最高",
            key: "rated"
        },
        {
            label: "最多投票",
            key: "upvoted"
        },
        {
            label: "播放时长最多",
            key: "playtime"
        },
        {
            label: "总播放时长最多",
            key: "total-playtime"
        },
        {
            label: "平均播放时长最长",
            key: "average-playtime"
        },
        {
            label: "终身平均播放时长",
            key: "lifetime-average"
        },
        {
            label: "播放次数最多",
            key: "sessions"
        },
        {
            label: "终身播放次数最多",
            key: "lifetime-sessions"
        },
        {
            label: "最近更新",
            key: "updated"
        }
    ]
    property var workshopTypeFilters: [
        {
            label: "全部",
            key: "all"
        },
        {
            label: "场景",
            key: "scene"
        },
        {
            label: "网页",
            key: "web"
        },
        {
            label: "视频",
            key: "video"
        },
        {
            label: "预设",
            key: "preset"
        }
    ]
    property var workshopTagFilters: [
        {
            label: "动漫",
            key: "Anime"
        },
        {
            label: "自然",
            key: "Nature"
        },
        {
            label: "抽象",
            key: "Abstract"
        },
        {
            label: "风景",
            key: "Landscape"
        },
        {
            label: "科幻",
            key: "Sci-Fi"
        },
        {
            label: "卡通",
            key: "Cartoon"
        },
        {
            label: "赛博朋克",
            key: "Cyberpunk"
        },
        {
            label: "奇幻",
            key: "Fantasy"
        },
        {
            label: "女孩",
            key: "Girl"
        },
        {
            label: "游戏",
            key: "Game"
        },
        {
            label: "动物",
            key: "Animal"
        },
        {
            label: "音乐",
            key: "Music"
        },
        {
            label: "车辆",
            key: "Vehicle"
        },
        {
            label: "科技",
            key: "Technology"
        },
        {
            label: "复古",
            key: "Retro"
        },
        {
            label: "城市",
            key: "City"
        },
        {
            label: "太空",
            key: "Space"
        },
        {
            label: "暗黑",
            key: "Dark"
        },
        {
            label: "像素",
            key: "Pixel Art"
        },
        {
            label: "极简",
            key: "Minimalist"
        },
        {
            label: "水下",
            key: "Underwater"
        },
        {
            label: "放松",
            key: "Relaxing"
        },
        {
            label: "中世纪",
            key: "Medieval"
        },
        {
            label: "未分类",
            key: "Unspecified"
        }
    ]

    function isEnabled(filters, key) {
        return filters.indexOf(key) !== -1;
    }

    function setEnabled(propertyName, key, enabled) {
        var next = window[propertyName].slice();
        var index = next.indexOf(key);
        if (enabled && index === -1)
            next.push(key);
        if (!enabled && index !== -1)
            next.splice(index, 1);
        window[propertyName] = next;
    }

    function normalizeTag(tag) {
        return String(tag || "").toLowerCase().replace(/[ -]/g, "");
    }

    function normalizedTags(tags) {
        var result = [];
        for (var index = 0; index < (tags || []).length; ++index) {
            result.push(normalizeTag(tags[index]));
        }
        return result;
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
        return PropertyLocalization.propertyText(propertyData.key, propertyData.text);
    }

    function propertyOptionItems(options) {
        return visiblePropertyOptions(options).map(function (option) {
            return {
                label: PropertyLocalization.displayText(option.label || option.value),
                value: option.value
            };
        });
    }

    function resetDetailScroll() {
        Qt.callLater(function () {
            if (detailScroll.contentItem)
                detailScroll.contentItem.contentY = 0;
        });
    }

    function copyGroupNumber() {
        groupNumberClipboard.selectAll();
        groupNumberClipboard.copy();
    }

    function workshopSortIndex() {
        for (var index = 0; index < workshopSortOptions.length; ++index) {
            if (workshopSortOptions[index].key === workshopSortKey)
                return index;
        }
        return 0;
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
        return items.filter(function(item) {
            if (workshopType !== "all" && String(item.type || "") !== workshopType)
                return false;
            if (workshopRatings.length > 0 && !isEnabled(workshopRatings, String(item.rating || "Everyone")))
                return false;
            if (workshopSelectedTags.length > 0) {
                var itemTags = item.tags || [];
                for (var index = 0; index < workshopSelectedTags.length; ++index) {
                    if (itemTags.indexOf(workshopSelectedTags[index]) === -1)
                        return false;
                }
            }
            return true;
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
        settingsDraft = mirage.settings;
        settingsDirty = false;
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
    }

    function playlistOptionIndex(options, value) {
        for (var index = 0; index < options.length; ++index) {
            if (options[index].key === value)
                return index;
        }
        return 0;
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
        var result = [];
        for (var index = 0; index < mirage.screenCount; ++index) {
            result.push("显示器 " + (index + 1));
        }
        return result;
    }

    function propertyConditionValue(value, type) {
        if (typeof value === "boolean" || typeof value === "number")
            return value;
        var text = String(value === undefined || value === null ? "" : value).trim();
        if (type === "bool") {
            var normalized = text.toLowerCase();
            return normalized === "true" || normalized === "1" || normalized === "yes" || normalized === "on";
        }
        if (/^-?(?:\d+\.?\d*|\.\d+)$/.test(text))
            return Number(text);
        if (text === "true")
            return true;
        if (text === "false")
            return false;
        return text;
    }

    function propertyConditionVisible(condition) {
        if (!condition || String(condition).trim().length === 0)
            return true;
        try {
            var names = [];
            var values = [];
            var properties = mirage.selectedProperties || [];
            for (var index = 0; index < properties.length; ++index) {
                var property = properties[index];
                if (!/^[A-Za-z_$][A-Za-z0-9_$]*$/.test(property.key))
                    continue;
                names.push(property.key);
                values.push({
                    value: propertyConditionValue(property.value, property.type)
                });
            }
            var evaluate = Function.apply(null, names.concat(["return !!(" + condition + ");"]));
            return evaluate.apply(null, values);
        } catch (error) {
            return true;
        }
    }

    function visiblePropertyOptions(options) {
        var result = [];
        for (var index = 0; index < (options || []).length; ++index) {
            if (propertyConditionVisible(options[index].condition))
                result.push(options[index]);
        }
        return result;
    }

    function propertyOptionIndex(options, value) {
        for (var index = 0; index < options.length; ++index) {
            if (String(options[index].value) === String(value))
                return index;
        }
        return 0;
    }

    function propertyBoolValue(value) {
        if (typeof value === "boolean")
            return value;
        var normalized = String(value || "").toLowerCase();
        return normalized === "true" || normalized === "1" || normalized === "yes" || normalized === "on";
    }

    function propertyColor(value) {
        var components = String(value || "").trim().split(/\s+/);
        if (components.length >= 3 && !isNaN(Number(components[0])) && !isNaN(Number(components[1])) && !isNaN(Number(components[2]))) {
            return Qt.rgba(Number(components[0]), Number(components[1]), Number(components[2]), 1);
        }
        return value || "white";
    }

    function propertyColorValue(color) {
        return Number(color.r).toFixed(3) + " " + Number(color.g).toFixed(3) + " " + Number(color.b).toFixed(3);
    }

    function propertyPathLabel(value) {
        var path = String(value || "");
        if (path.length === 0)
            return "未选择";
        var separator = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"));
        return separator >= 0 ? path.substring(separator + 1) : path;
    }

    function selectedFilePath(url) {
        return decodeURIComponent(String(url).replace(/^file:\/\//, ""));
    }

    function metadataTagList(text) {
        var result = [];
        var seen = {};
        var values = String(text || "").split(/[;,\n]/);
        for (var index = 0; index < values.length; ++index) {
            var tag = values[index].trim();
            var normalized = tag.toLowerCase();
            if (tag.length === 0 || seen[normalized])
                continue;
            seen[normalized] = true;
            result.push(tag);
        }
        result.sort(function (left, right) {
            return left.localeCompare(right);
        });
        return result;
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

    function filterWallpapers(source) {
        var query = searchText.trim().toLowerCase();
        var result = source ? source.slice() : [];
        result = result.filter(function (wallpaper) {
            var tags = normalizedTags(wallpaper.tags);
            if (approvedOnly && !wallpaper.approved)
                return false;
            if (favoritesOnly && !wallpaper.favorite)
                return false;
            if (customizableOnly && !wallpaper.customizable)
                return false;
            if (mobileOnly && tags.indexOf("mobile") === -1)
                return false;
            if (audioOnly && tags.indexOf("audioresponsive") === -1 && tags.indexOf("audio") === -1)
                return false;
            if (!isEnabled(enabledTypes, wallpaper.type))
                return false;
            if (!isEnabled(enabledRatings, wallpaper.rating))
                return false;
            if (!isEnabled(enabledSources, wallpaper.source))
                return false;

            if (enabledTags.length < tagFilters.length) {
                if (tags.length === 0) {
                    if (!isEnabled(enabledTags, "unspecified"))
                        return false;
                } else {
                    var tagMatches = false;
                    for (var index = 0; index < tags.length; ++index) {
                        if (isEnabled(enabledTags, tags[index])) {
                            tagMatches = true;
                            break;
                        }
                    }
                    if (!tagMatches)
                        return false;
                }
            }
            return query.length === 0 || String(wallpaper.searchText || "").toLowerCase().indexOf(query) !== -1;
        });
        result.sort(function (left, right) {
            var comparison = 0;
            if (sortMode === "rating") {
                comparison = String(left.rating || "").localeCompare(String(right.rating || ""));
            } else if (sortMode === "size") {
                comparison = Number(left.size || 0) - Number(right.size || 0);
            }
            if (comparison === 0) {
                comparison = String(left.title || "").localeCompare(String(right.title || ""));
            }
            return sortDescending ? -comparison : comparison;
        });
        return result;
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

    Component.onCompleted: {
        FluTheme.animationEnabled = true;
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
                settingsDraft = mirage.settings;
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

    FluMenu {
        id: wallpaperContextMenu
        property var wallpaper: ({})
        width: Math.min(240, Math.max(220, window.width - 32))

        FluMenuItem {
            text: "设为屏保（TODO）"
            iconSource: FluentIcons.SettingsDisplaySound
            onTriggered: linuxNotice.open()
        }
        FluMenuSeparator {}
        FluMenu {
            title: "加入播放列表"
            Repeater {
                model: mirage.screenCount
                delegate: FluMenuItem {
                    required property int index
                    text: "显示器 " + (index + 1)
                    iconSource: FluentIcons.Add
                    onTriggered: window.addWallpaperToPlaylist(wallpaperContextMenu.wallpaper.id, index)
                }
            }
        }
        FluMenuItem {
            text: wallpaperContextMenu.wallpaper.favorite ? "取消收藏" : "加入收藏"
            iconSource: wallpaperContextMenu.wallpaper.favorite ? FluentIcons.HeartFill : FluentIcons.Heart
            onTriggered: {
                mirage.selectWallpaper(wallpaperContextMenu.wallpaper.id);
                mirage.toggleSelectedFavorite();
            }
        }
        FluMenuSeparator {}
        FluMenuItem {
            text: "在文件管理器中显示"
            iconSource: FluentIcons.FolderOpen
            onTriggered: Qt.openUrlExternally(wallpaperContextMenu.wallpaper.location)
        }
        FluMenuItem {
            text: "在 Steam 中查看（TODO）"
            iconSource: FluentIcons.OpenFile
            visible: wallpaperContextMenu.wallpaper.source === "workshop"
            onTriggered: linuxNotice.open()
        }
        FluMenuSeparator {}
        FluMenuItem {
            text: "删除导入壁纸"
            iconSource: FluentIcons.Delete
            visible: wallpaperContextMenu.wallpaper.source === "imported"
            onTriggered: {
                mirage.selectWallpaper(wallpaperContextMenu.wallpaper.id);
                wallpaperDeleteConfirmation.open();
            }
        }
        FluMenuItem {
            text: "取消订阅（TODO）"
            iconSource: FluentIcons.Delete
            visible: wallpaperContextMenu.wallpaper.source === "workshop"
            onTriggered: linuxNotice.open()
        }
    }

    TextEdit {
        id: groupNumberClipboard
        visible: false
        text: window.groupNumber
        selectByMouse: false
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
                anchors.margins: 16
                spacing: 6

                TopTabBar {
                    Layout.fillWidth: true
                    currentIndex: window.currentTab
                    downloadCount: mirage.activeDownloadCount
                    onSelected: index => window.currentTab = index
                    onMobileRequested: linuxNotice.open()
                    onDisplayRequested: displaySettingsSheet.open()
                    onSettingsRequested: settingsSheet.open()
                }

                ProjectFeedbackBanner {
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    onCopyRequested: window.copyGroupNumber()
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
                        } else if (window.currentTab === 0) {
                            window.sortMode = ["名称", "评分", "文件大小"].indexOf(key) === 1 ? "rating" : ["名称", "评分", "文件大小"].indexOf(key) === 2 ? "size" : "name";
                        } else {
                            window.workshopSortKey = window.workshopSortOptions[window.workshopSortIndex()].key;
                            mirage.setWorkshopSortOrder(window.workshopSortKey);
                        }
                    }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: window.currentTab

                    RowLayout {
                        spacing: 10

                        ScrollView {
                            Layout.fillHeight: true
                            Layout.preferredWidth: 225
                            visible: window.filtersVisible
                            clip: true

                            ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 12

                                    FluText {
                                        text: "筛选"
                                        font: FluTextStyle.Subtitle
                                    }
                                    FluFilledButton {
                                        Layout.fillWidth: true
                                        text: "重置筛选"
                                        onClicked: window.resetFilters()
                                    }
                                    FluText {
                                        text: "仅显示"
                                        font: FluTextStyle.BodyStrong
                                    }
                                    FluToggleSwitch {
                                        text: "广受好评"
                                        checked: window.approvedOnly
                                        clickListener: function () {
                                            window.approvedOnly = !window.approvedOnly;
                                        }
                                    }
                                    FluToggleSwitch {
                                        text: "我的收藏"
                                        checked: window.favoritesOnly
                                        clickListener: function () {
                                            window.favoritesOnly = !window.favoritesOnly;
                                        }
                                    }
                                    FluToggleSwitch {
                                        text: "移动端兼容"
                                        checked: window.mobileOnly
                                        clickListener: function () {
                                            window.mobileOnly = !window.mobileOnly;
                                        }
                                    }
                                    FluToggleSwitch {
                                        text: "音频响应"
                                        checked: window.audioOnly
                                        clickListener: function () {
                                            window.audioOnly = !window.audioOnly;
                                        }
                                    }
                                    FluToggleSwitch {
                                        text: "可自定义"
                                        checked: window.customizableOnly
                                        clickListener: function () {
                                            window.customizableOnly = !window.customizableOnly;
                                        }
                                    }
                                    FluDivider {
                                        Layout.fillWidth: true
                                    }
                                    FluText {
                                        text: "类型"
                                        font: FluTextStyle.BodyStrong
                                    }
                                    Repeater {
                                        model: window.typeFilters
                                        delegate: FluToggleSwitch {
                                            required property var modelData
                                            text: modelData.label
                                            checked: window.isEnabled(window.enabledTypes, modelData.key)
                                            clickListener: function () {
                                                window.setEnabled("enabledTypes", modelData.key, !checked);
                                            }
                                        }
                                    }
                                    FluDivider {
                                        Layout.fillWidth: true
                                    }
                                    FluText {
                                        text: "分级"
                                        font: FluTextStyle.BodyStrong
                                    }
                                    Repeater {
                                        model: window.ratingFilters
                                        delegate: FluToggleSwitch {
                                            required property var modelData
                                            text: modelData.label
                                            checked: window.isEnabled(window.enabledRatings, modelData.key)
                                            clickListener: function () {
                                                window.setEnabled("enabledRatings", modelData.key, !checked);
                                            }
                                        }
                                    }
                                    FluDivider {
                                        Layout.fillWidth: true
                                    }
                                    FluText {
                                        text: "来源"
                                        font: FluTextStyle.BodyStrong
                                    }
                                    Repeater {
                                        model: window.sourceFilters
                                        delegate: FluToggleSwitch {
                                            required property var modelData
                                            text: modelData.label
                                            checked: window.isEnabled(window.enabledSources, modelData.key)
                                            clickListener: function () {
                                                window.setEnabled("enabledSources", modelData.key, !checked);
                                            }
                                        }
                                    }
                                    FluDivider {
                                        Layout.fillWidth: true
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        FluText {
                                            text: "标签"
                                            font: FluTextStyle.BodyStrong
                                        }
                                        Item {
                                            Layout.fillWidth: true
                                        }
                                        FluTextButton {
                                            text: "全选"
                                            onClicked: {
                                                window.enabledTags = window.tagFilters.map(function (filter) {
                                                    return filter.key;
                                                });
                                            }
                                        }
                                        FluTextButton {
                                            text: "清空"
                                            onClicked: window.enabledTags = []
                                        }
                                    }
                                    Repeater {
                                        model: window.tagFilters
                                        delegate: FluToggleSwitch {
                                            required property var modelData
                                            text: modelData.label
                                            checked: window.isEnabled(window.enabledTags, modelData.key)
                                            clickListener: function () {
                                                window.setEnabled("enabledTags", modelData.key, !checked);
                                            }
                                        }
                                    }
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
                        ScrollView {
                            Layout.fillHeight: true
                            Layout.preferredWidth: 225
                            visible: window.filtersVisible
                            clip: true
                            WorkshopFilterSidebar {
                                width: parent.width
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
                        ScrollView {
                            Layout.fillHeight: true
                            Layout.preferredWidth: 225
                            visible: window.filtersVisible
                            clip: true
                            WorkshopFilterSidebar {
                                width: parent.width
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

        ScrollView {
            id: detailScroll
            SplitView.minimumWidth: 288
            SplitView.preferredWidth: 320
            SplitView.maximumWidth: 340
            SplitView.fillHeight: true
            clip: true
            leftPadding: 16
            // The frameless window intentionally lets content fit behind the
            // app bar. Keep the detail preview below that hit-test region so
            // window controls never paint over wallpaper information.
            topPadding: appBar.height + 16
            rightPadding: 16
            bottomPadding: 16
            contentWidth: Math.max(0, width - leftPadding - rightPadding)

            Item {
                id: detailContent
                width: detailScroll.contentWidth
                implicitHeight: window.currentTab === 0 ? installedDetail.implicitHeight : workshopDetail.implicitHeight
                height: implicitHeight

                ColumnLayout {
                    id: installedDetail
                    width: parent.width
                    visible: window.currentTab === 0
                    spacing: 16

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(280, installedDetail.width)
                        Image {
                            anchors.centerIn: parent
                            width: Math.min(280, parent.width)
                            height: width
                            source: mirage.selectedWallpaper.preview || ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            cache: false
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        FluText {
                            Layout.fillWidth: true
                            text: mirage.selectedWallpaper.title || "请选择一个壁纸"
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            font: FluTextStyle.Subtitle
                        }
                        FluIconButton {
                            text: "编辑壁纸信息"
                            iconSource: FluentIcons.Edit
                            visible: mirage.selectedWallpaperId.length > 0
                            onClicked: wallpaperMetadataSheet.open()
                        }
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 6
                        FluIcon {
                            iconSource: FluentIcons.Contact
                            iconSize: 20
                            iconColor: FluTheme.fontSecondaryColor
                        }
                        FluText {
                            text: mirage.selectedWallpaperId.length > 0 ? (mirage.selectedWallpaper.author || "佚名作者") : ""
                            elide: Text.ElideRight
                        }
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 6
                        FluText {
                            text: mirage.selectedWallpaper.typeLabel || ""
                        }
                        FluIconButton {
                            text: mirage.selectedWallpaper.favorite ? "取消收藏" : "收藏"
                            iconSource: mirage.selectedWallpaper.favorite ? FluentIcons.HeartFill : FluentIcons.Heart
                            enabled: mirage.selectedWallpaperId.length > 0
                            onClicked: mirage.toggleSelectedFavorite()
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        visible: (mirage.selectedWallpaper.tags || []).length > 0
                        spacing: 6
                        Repeater {
                            model: mirage.selectedWallpaper.tags || []
                            delegate: FluFrame {
                                required property string modelData
                                width: tagText.implicitWidth + 16
                                height: tagText.implicitHeight + 8
                                FluText {
                                    id: tagText
                                    anchors.centerIn: parent
                                    text: modelData
                                }
                            }
                        }
                    }

                    DetailSectionHeader {
                        title: "播放控制"
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        FluText {
                            text: "音量"
                        }
                        FluSlider {
                            Layout.fillWidth: true
                            from: 0
                            to: 1
                            value: mirage.selectedVolume
                            onMoved: mirage.selectedVolume = value
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: mirage.selectedWallpaper.kind === "scene"
                        FluText {
                            text: "速度"
                        }
                        FluSlider {
                            Layout.fillWidth: true
                            from: 0
                            to: 2
                            stepSize: 0.1
                            value: mirage.selectedSpeed
                            onMoved: mirage.selectedSpeed = value
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: mirage.selectedWallpaper.kind === "video"
                        FluText {
                            text: "填充模式"
                        }
                        FluComboBox {
                            Layout.fillWidth: true
                            model: ["cover", "contain", "stretch"]
                            currentIndex: Math.max(0, model.indexOf(mirage.selectedFillMode))
                            onActivated: mirage.selectedFillMode = currentText
                        }
                    }

                    DetailSectionHeader {
                        title: "壁纸属性"
                        visible: mirage.selectedWallpaperId.length > 0
                    }
                    FluText {
                        Layout.fillWidth: true
                        visible: mirage.selectedWallpaperId.length > 0 && mirage.selectedProperties.length === 0
                        text: "此壁纸没有可调节的属性。"
                        wrapMode: Text.WordWrap
                    }
                    PropertyEditor {
                        Layout.fillWidth: true
                        host: window
                        properties: mirage.selectedProperties
                    }

                    DetailSectionHeader {
                        title: "壁纸"
                    }
                    FluFilledButton {
                        Layout.fillWidth: true
                        text: "应用"
                        enabled: mirage.selectedWallpaperId.length > 0
                        onClicked: window.runWithWallpaperTrust(mirage.selectedWallpaper, function () {
                            mirage.applySelected(false);
                        })
                    }
                    FluButton {
                        Layout.fillWidth: true
                        text: "应用到所有显示器"
                        enabled: mirage.selectedWallpaperId.length > 0
                        onClicked: window.runWithWallpaperTrust(mirage.selectedWallpaper, function () {
                            mirage.applySelected(true);
                        })
                    }
                    FluButton {
                        Layout.fillWidth: true
                        text: "停止壁纸"
                        onClicked: mirage.stopWallpapers()
                    }
                    FluButton {
                        Layout.fillWidth: true
                        visible: mirage.selectedWallpaper.source === "imported"
                        text: "删除导入壁纸"
                        onClicked: wallpaperDeleteConfirmation.open()
                    }

                    DetailSectionHeader {
                        title: "预设"
                    }
                    FluButton {
                        Layout.fillWidth: true
                        text: "重置为默认"
                        enabled: mirage.selectedProperties.length > 0
                        onClicked: mirage.resetSelectedProperties()
                    }
                }

                WorkshopItemDetail {
                    id: workshopDetail
                    width: parent.width
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
    MirageDialogWindow {
        id: wallpaperMetadataSheet
        title: "编辑壁纸信息"
        width: 460
        height: 300
        negativeText: "取消"
        positiveText: "保存"
        buttonFlags: FluContentDialogType.NegativeButton | FluContentDialogType.PositiveButton
        onOpened: {
            window.metadataTitle = mirage.selectedWallpaper.title || "";
            window.metadataTags = (mirage.selectedWallpaper.tags || []).join(", ");
        }
        onPositiveClicked: mirage.updateSelectedMetadata(window.metadataTitle, window.metadataTagList(window.metadataTags))
        contentDelegate: Component {
            ColumnLayout {
                width: parent.width
                spacing: 8
                FluText {
                    text: "名称"
                }
                FluTextBox {
                    Layout.fillWidth: true
                    text: window.metadataTitle
                    placeholderText: "壁纸名称"
                    onTextChanged: window.metadataTitle = text
                }
                FluText {
                    text: "标签"
                }
                FluTextBox {
                    Layout.fillWidth: true
                    text: window.metadataTags
                    placeholderText: "用逗号、分号或换行分隔标签"
                    onTextChanged: window.metadataTags = text
                }
            }
        }
    }

    MirageDialogWindow {
        id: wallpaperDeleteConfirmation
        width: 420
        height: 220
        title: "删除导入壁纸"
        message: "确定要删除“" + (mirage.selectedWallpaper.title || "") + "”及其所有文件吗？此操作不可恢复。"
        negativeText: "取消"
        positiveText: "删除"
        buttonFlags: FluContentDialogType.NegativeButton | FluContentDialogType.PositiveButton
        onPositiveClicked: mirage.deleteSelectedWallpaper()
    }

    MirageDialogWindow {
        id: wallpaperTrustSheet
        property bool rememberWallpaper: false
        title: "确认运行网页壁纸"
        width: 460
        height: 300
        negativeText: "取消"
        positiveText: "继续运行"
        buttonFlags: FluContentDialogType.NegativeButton | FluContentDialogType.PositiveButton
        onOpened: rememberWallpaper = false
        onNegativeClicked: window.cancelWallpaperTrust()
        onPositiveClicked: window.confirmWallpaperTrust()
        contentDelegate: Component {
            ColumnLayout {
                width: parent.width
                spacing: 10
                FluText {
                    Layout.fillWidth: true
                    text: "网页壁纸可能执行来自第三方的脚本。请仅在信任来源和内容时继续运行。"
                    wrapMode: Text.WordWrap
                }
                FluToggleSwitch {
                    text: "记住对此壁纸的确认"
                    checked: wallpaperTrustSheet.rememberWallpaper
                    clickListener: function () {
                        wallpaperTrustSheet.rememberWallpaper = !wallpaperTrustSheet.rememberWallpaper;
                    }
                }
            }
        }
    }

    MirageDialogWindow {
        id: displaySettingsSheet
        title: "显示器设置"
        width: 520
        height: 450
        neutralText: "完成"
        buttonFlags: FluContentDialogType.NeutralButton
        contentDelegate: Component {
            ColumnLayout {
                width: parent.width
                spacing: 10

                FluText {
                    Layout.fillWidth: true
                    text: "将当前壁纸「" + (mirage.selectedWallpaper.title || "未选择") + "」指派到指定显示器。"
                    wrapMode: Text.WordWrap
                }
                ListView {
                    id: displayList
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    implicitHeight: Math.min(320, Math.max(76, contentHeight))
                    clip: true
                    spacing: 8
                    model: mirage.displays
                    delegate: FluFrame {
                        required property var modelData
                        width: displayList.width
                        height: displayRow.implicitHeight + 16

                        RowLayout {
                            id: displayRow
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 10

                            FluIcon {
                                iconSource: FluentIcons.SettingsDisplaySound
                                iconSize: 24
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                FluText {
                                    Layout.fillWidth: true
                                    text: modelData.name + (modelData.primary ? " · 主显示器" : "")
                                    elide: Text.ElideRight
                                    font: FluTextStyle.BodyStrong
                                }
                                FluText {
                                    Layout.fillWidth: true
                                    text: modelData.width + " × " + modelData.height + (modelData.running ? " · 正在渲染：" + modelData.wallpaperTitle : " · 未渲染壁纸")
                                    elide: Text.ElideRight
                                }
                            }
                            ColumnLayout {
                                spacing: 4
                                FluFilledButton {
                                    text: "应用到此屏"
                                    enabled: mirage.selectedWallpaperId.length > 0
                                    onClicked: window.runWithWallpaperTrust(mirage.selectedWallpaper, function () {
                                        mirage.applySelectedToScreen(modelData.index);
                                    })
                                }
                                FluButton {
                                    text: "停止"
                                    visible: modelData.running
                                    onClicked: mirage.stopScreen(modelData.index)
                                }
                            }
                        }
                    }
                }
                FluButton {
                    Layout.alignment: Qt.AlignHCenter
                    text: "全部停止"
                    enabled: mirage.displays.some(function (display) {
                        return display.running;
                    })
                    onClicked: mirage.stopWallpapers()
                }
            }
        }
    }

    PlaylistOpenSheet {
        id: playlistOpenSheet
        host: window
    }
    MirageDialogWindow {
        id: playlistDeleteConfirmation
        width: 420
        height: 220
        property string playlistId: ""
        property string playlistName: ""
        title: "删除播放列表"
        message: "确定要删除“" + playlistName + "”吗？"
        negativeText: "取消"
        positiveText: "删除"
        buttonFlags: FluContentDialogType.NegativeButton | FluContentDialogType.PositiveButton
        onPositiveClicked: {
            mirage.deleteSavedPlaylist(playlistId);
            playlistId = "";
        }
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
        onAccepted: mirage.setSelectedProperty(propertyKey, window.selectedFilePath(selectedFile))
    }

    FolderDialog {
        id: propertyFolderDialog
        property string propertyKey: ""
        title: "选择目录"
        onAccepted: mirage.setSelectedProperty(propertyKey, window.selectedFilePath(selectedFolder))
    }

    FileDialog {
        id: importFileDialog
        title: "导入壁纸视频"
        fileMode: FileDialog.OpenFile
        nameFilters: ["视频 (*.mp4 *.mov *.m4v *.webm *.mkv)", "所有文件 (*)"]
        onAccepted: mirage.importWallpaperPath(window.selectedFilePath(selectedFile))
    }

    FolderDialog {
        id: importFolderDialog
        title: "导入壁纸文件夹"
        onAccepted: mirage.importWallpaperPath(window.selectedFilePath(selectedFolder))
    }

    SteamSetupView {
        id: steamSetupWindow
        autoVisible: false
    }

    SteamSetupWindowController {
        id: steamSetupController
        window: steamSetupWindow
    }

    MirageDialogWindow {
        id: linuxNotice
        title: "Mirage"
        width: 380
        height: 220
        buttonFlags: 0
        contentDelegate: Component {
            ColumnLayout {
                spacing: 12
                FluText {
                    text: "Mirage"
                    font: FluTextStyle.Subtitle
                }
                FluText {
                    Layout.fillWidth: true
                    text: "TODO: 此功能尚未在 Linux Qt 版本实现。"
                    wrapMode: Text.WordWrap
                }
                FluFilledButton {
                    Layout.alignment: Qt.AlignRight
                    text: "好"
                    onClicked: linuxNotice.close()
                }
            }
        }
    }

    MirageDialogWindow {
        id: statusPopup
        title: "Mirage"
        width: 380
        height: 160
        modality: Qt.NonModal
        buttonFlags: 0
        contentDelegate: Component {
            FluText {
                width: parent.width
                text: mirage.statusMessage
                wrapMode: Text.WordWrap
            }
        }
        Timer {
            interval: 4000
            running: statusPopup.visible
            onTriggered: statusPopup.close()
        }
        Component.onCompleted: {
            if (mirage.statusMessage.length > 0)
                open();
        }
        Connections {
            target: mirage
            function onStatusMessageChanged() {
                if (mirage.statusMessage.length > 0)
                    statusPopup.open();
                else
                    statusPopup.close();
            }
        }
    }

    component DetailSectionHeader: RowLayout {
        required property string title
        Layout.fillWidth: true
        spacing: 6

        FluText {
            text: parent.title
            font: FluTextStyle.BodyStrong
        }
        FluDivider {
            Layout.fillWidth: true
        }
    }

}
