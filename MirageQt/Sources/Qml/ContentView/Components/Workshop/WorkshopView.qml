import QtQuick
import QtQuick.Layouts
import FluentUI
import "../../../MirageBridge.js" as MirageBridge

Item {
    id: root
    required property var host

    property var items: mirage.workshopItems
    property bool loading: mirage.workshopLoading
    property string errorText: mirage.workshopError
    property int page: mirage.workshopPage
    property int pageCount: mirage.workshopPageCount
    property bool steamReady: mirage.steamReady
    property string steamSummary: mirage.steamSetupSummary
    property int activeDownloads: Number(value("activeDownloadCount", 0))
    property var downloadQueue: value("downloadQueue", [])
    property bool steamLoggedIn: Boolean(value("steamLoggedIn", false))
    property string steamUsername: String(value("steamUsername", ""))

    function value(name, fallback) {
        return MirageBridge.value(mirage, name, fallback);
    }

    function invoke(name) {
        return MirageBridge.invoke(mirage, name, Array.prototype.slice.call(arguments, 1));
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            FluFilledButton {
                text: qsTr("筛选")
                onClicked: root.host.filtersVisible = !root.host.filtersVisible
            }
            FluTextBox {
                Layout.preferredWidth: 200
                Layout.minimumWidth: 140
                Layout.maximumWidth: 240
                placeholderText: qsTr("搜索作品、作者或作品 ID…")
                iconSource: FluentIcons.Search
                text: root.host.workshopSearchText
                onTextChanged: {
                    root.host.workshopSearchText = text;
                    root.invoke("setWorkshopSearchText", text);
                }
                onCommit: root.invoke("submitWorkshopSearch")
            }
            Item { Layout.fillWidth: true }
            FluIconButton {
                iconSource: FluentIcons.Refresh
                text: qsTr("刷新创意工坊")
                contentDescription: qsTr("刷新创意工坊")
                disabled: root.loading
                onClicked: root.invoke("submitWorkshopSearch")
            }
            FluComboBox {
                Layout.preferredWidth: 130
                Layout.minimumWidth: 110
                model: root.host.workshopSortOptions.map(function(option) { return option.label; })
                currentIndex: root.host.workshopSortIndex()
                onActivated: {
                    root.host.workshopSortKey = root.host.workshopSortOptions[currentIndex].key;
                    root.invoke("setWorkshopSortOrder", root.host.workshopSortKey);
                }
            }
            FluIconButton {
                id: downloadButton
                iconSource: FluentIcons.Download
                text: qsTr("下载管理")
                contentDescription: qsTr("下载管理")
                onClicked: {
                    if (downloadPopover.opened)
                        downloadPopover.close();
                    else
                        downloadPopover.openFor(downloadButton);
                }
                FluBadge {
                    position: "topRight"
                    count: root.activeDownloads
                    visible: root.activeDownloads > 0
                }
            }
            RowLayout {
                visible: root.steamLoggedIn
                spacing: 4
                FluIcon {
                    iconSource: FluentIcons.ContactSolid
                    iconSize: 15
                    iconColor: Qt.rgba(16 / 255, 124 / 255, 16 / 255, 1)
                }
                FluText {
                    text: root.steamUsername
                    elide: Text.ElideRight
                    Layout.maximumWidth: 80
                    color: FluTheme.fontSecondaryColor
                    font: FluTextStyle.Caption
                }
                FluIconButton {
                    iconSource: FluentIcons.SignOut
                    text: qsTr("退出 Steam")
                    contentDescription: qsTr("退出 Steam")
                    onClicked: root.invoke("logoutSteam")
                }
            }
            FluFilledButton {
                visible: !root.steamLoggedIn
                text: qsTr("设置 Steam")
                onClicked: root.host.openSteamSetup()
            }
        }

        FluFrame {
            id: steamSetupBanner
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            visible: !root.steamReady
            radius: 8
            color: FluTheme.dark
                ? Qt.rgba(0.08, 0.32, 0.55, 0.18)
                : Qt.rgba(0.0, 0.47, 0.84, 0.08)
            border.color: FluTheme.dark
                ? Qt.rgba(0.20, 0.55, 0.85, 0.32)
                : Qt.rgba(0.0, 0.47, 0.84, 0.20)
            RowLayout {
                id: steamSetupBannerContent
                anchors.fill: parent
                anchors.margins: 12
                spacing: 12
                FluIcon {
                    iconSource: FluentIcons.CloudDownload
                    iconSize: 21
                    iconColor: FluTheme.primaryColor
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    FluText {
                        text: qsTr("连接 Steam 以下载壁纸")
                        font: FluTextStyle.BodyStrong
                    }
                    FluText {
                        Layout.fillWidth: true
                        text: root.steamSummary.length > 0 ? root.steamSummary : qsTr("需要先安装 SteamCMD")
                        wrapMode: Text.WordWrap
                        color: FluTheme.fontSecondaryColor
                    }
                }
                FluFilledButton {
                    text: qsTr("设置 Steam")
                    onClicked: root.host.openSteamSetup()
                }
            }
        }

        FluProgressRing {
            Layout.alignment: Qt.AlignHCenter
            visible: root.loading && root.items.length === 0
            indeterminate: true
        }
        FluText {
            Layout.alignment: Qt.AlignHCenter
            visible: !root.loading && root.items.length === 0
            text: root.errorText.length > 0 ? root.errorText : qsTr("没有找到壁纸")
            color: root.errorText.length > 0
                ? Qt.rgba(196 / 255, 43 / 255, 28 / 255, 1)
                : FluTheme.fontSecondaryColor
        }
        FluButton {
            Layout.alignment: Qt.AlignHCenter
            visible: !root.loading && root.errorText.length > 0
            text: qsTr("重试")
            onClicked: root.invoke("submitWorkshopSearch")
        }

        GridView {
            id: workshopGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.items.length > 0
            clip: true
            model: root.items
            cellWidth: Math.max(178, Math.floor(width / Math.max(1, Math.floor(width / 178))))
            cellHeight: 258
            delegate: WorkshopItemCard {
                required property var modelData
                host: root.host
                itemData: modelData
                width: Math.max(164, workshopGrid.cellWidth - 14)
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            visible: root.pageCount > 1
            FluIconButton {
                iconSource: FluentIcons.ChevronLeft
                text: qsTr("上一页")
                contentDescription: qsTr("上一页")
                disabled: root.page <= 1
                onClicked: root.invoke("loadPreviousWorkshopPage")
            }
            FluText {
                text: root.page + " / " + root.pageCount
            }
            FluIconButton {
                iconSource: FluentIcons.ChevronRight
                text: qsTr("下一页")
                contentDescription: qsTr("下一页")
                disabled: root.page >= root.pageCount
                onClicked: root.invoke("loadNextWorkshopPage")
            }
        }
    }

    DownloadPopover {
        id: downloadPopover
        tasks: root.downloadQueue
        fallbackTasks: []
    }
}
