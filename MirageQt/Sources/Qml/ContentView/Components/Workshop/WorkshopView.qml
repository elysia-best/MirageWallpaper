import QtQuick
import QtQuick.Layouts
import FluentUI

Item {
    id: root
    required property var host

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            FluFilledButton {
                text: "筛选"
                onClicked: root.host.filtersVisible = !root.host.filtersVisible
            }
            FluTextBox {
                Layout.preferredWidth: 260
                placeholderText: "搜索作品、作者或作品 ID…"
                iconSource: FluentIcons.Search
                text: root.host.workshopSearchText
                onTextChanged: {
                    root.host.workshopSearchText = text;
                    mirage.setWorkshopSearchText(text);
                }
                onCommit: mirage.submitWorkshopSearch()
            }
            Item { Layout.fillWidth: true }
            FluIconButton {
                text: "刷新创意工坊"
                iconSource: FluentIcons.Refresh
                enabled: !mirage.workshopLoading
                onClicked: mirage.submitWorkshopSearch()
            }
            FluComboBox {
                Layout.preferredWidth: 150
                model: root.host.workshopSortOptions.map(function(option) {
                    return option.label;
                })
                currentIndex: root.host.workshopSortIndex()
                onActivated: {
                    root.host.workshopSortKey = root.host.workshopSortOptions[currentIndex].key;
                    mirage.setWorkshopSortOrder(root.host.workshopSortKey);
                }
            }
        }

        FluFrame {
            Layout.fillWidth: true
            visible: !mirage.steamReady
            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                FluIcon {
                    iconSource: FluentIcons.Cloud
                    iconSize: 20
                }
                FluText {
                    Layout.fillWidth: true
                    text: "连接 Steam 以下载壁纸。" + mirage.steamSetupSummary
                    wrapMode: Text.WordWrap
                }
                FluButton {
                    text: "设置 Steam"
                    onClicked: root.host.openSteamSetup()
                }
            }
        }

        FluProgressRing {
            Layout.alignment: Qt.AlignHCenter
            visible: mirage.workshopLoading && mirage.workshopItems.length === 0
            indeterminate: true
        }
        FluText {
            Layout.alignment: Qt.AlignHCenter
            visible: !mirage.workshopLoading && mirage.workshopItems.length === 0
            text: mirage.workshopError.length > 0 ? mirage.workshopError : "没有找到壁纸"
        }
        GridView {
            id: workshopGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: mirage.workshopItems.length > 0
            clip: true
            model: mirage.workshopItems
            // A regular card needs 164px plus the cell gutter. Using its real
            // minimum keeps two columns visible in the narrow content area.
            cellWidth: Math.max(178, Math.floor(width / Math.max(1, Math.floor(width / 178))))
            cellHeight: 248
            delegate: WorkshopItemCard {
                required property var modelData
                host: root.host
                itemData: modelData
                width: Math.max(164, workshopGrid.cellWidth - 14)
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            visible: mirage.workshopPageCount > 1
            FluIconButton {
                text: "上一页"
                iconSource: FluentIcons.ChevronLeft
                enabled: mirage.workshopPage > 1
                onClicked: mirage.loadPreviousWorkshopPage()
            }
            FluText { text: mirage.workshopPage + " / " + mirage.workshopPageCount }
            FluIconButton {
                text: "下一页"
                iconSource: FluentIcons.ChevronRight
                enabled: mirage.workshopPage < mirage.workshopPageCount
                onClicked: mirage.loadNextWorkshopPage()
            }
        }
    }
}
