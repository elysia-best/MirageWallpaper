import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    id: bar
    required property var host
    Layout.fillWidth: true
    Layout.preferredHeight: host.currentTab === 0 ? (host.playlistExpanded ? 174 : 44) : 0
    visible: host.currentTab === 0
    clip: true
    spacing: 6

    RowLayout {
        Layout.fillWidth: true
        FluIconButton {
            text: bar.host.playlistExpanded ? "收起播放列表" : "展开播放列表"
            iconSource: bar.host.playlistExpanded ? FluentIcons.ChevronUp : FluentIcons.ChevronDown
            onClicked: bar.host.playlistExpanded = !bar.host.playlistExpanded
        }
        FluText {
            text: "播放列表" + (mirage.playlistItems.length ? " (" + mirage.playlistItems.length + ")" : "")
            font: FluTextStyle.BodyStrong
        }
        FluComboBox {
            Layout.preferredWidth: 114
            model: bar.host.screenLabels()
            currentIndex: mirage.playlistScreen
            onActivated: mirage.playlistScreen = currentIndex
        }
        Item {
            Layout.fillWidth: true
        }
        FluIconButton {
            text: "打开播放列表"
            iconSource: FluentIcons.OpenFile
            display: Button.IconOnly
            onClicked: bar.host.openPlaylistOpenSheet()
        }
        FluIconButton {
            text: "保存播放列表"
            iconSource: FluentIcons.Save
            display: Button.IconOnly
            onClicked: bar.host.openPlaylistSaveSheet()
        }
        FluIconButton {
            text: "播放列表设置"
            iconSource: FluentIcons.Settings
            display: Button.IconOnly
            onClicked: bar.host.openPlaylistSettingsSheet()
        }
        FluIconButton {
            text: "添加所选壁纸"
            iconSource: FluentIcons.Add
            display: Button.IconOnly
            enabled: mirage.selectedWallpaperId.length > 0
            onClicked: mirage.addSelectedToPlaylist()
        }
        FluIconButton {
            text: "清空播放列表"
            iconSource: FluentIcons.Clear
            display: Button.IconOnly
            enabled: mirage.playlistItems.length > 0
            onClicked: mirage.clearPlaylist()
        }
        FluDropDownButton {
            text: "导入壁纸"
            FluMenuItem {
                text: "导入视频文件…"
                onTriggered: bar.host.openImportPanel()
            }
            FluMenuItem {
                text: "导入壁纸文件夹…"
                onTriggered: bar.host.openImportFolderPanel()
            }
        }
    }

    ListView {
        id: playlistList
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: bar.host.playlistExpanded
        orientation: ListView.Horizontal
        spacing: 8
        clip: true
        model: mirage.playlistItems
        delegate: FluFrame {
            required property var modelData
            required property int index
            width: 112
            height: parent.height
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 4
                Image {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    source: modelData.preview
                    fillMode: Image.PreserveAspectCrop
                }
                FluText {
                    Layout.fillWidth: true
                    text: modelData.title
                    elide: Text.ElideRight
                }
            }
            FluIconButton {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 2
                z: 1
                text: "从播放列表移除"
                iconSource: FluentIcons.Delete
                onClicked: mirage.removePlaylistItem(modelData.id)
            }
            MouseArea {
                anchors.fill: parent
                anchors.rightMargin: 30
                drag.target: parent
                drag.axis: Drag.XAxis
                onDoubleClicked: bar.host.runWithWallpaperTrust(modelData, function () {
                    mirage.playPlaylistItem(modelData.id);
                })
                onReleased: {
                    var target = Math.round((parent.x + parent.width / 2) / (parent.width + playlistList.spacing));
                    target = Math.max(0, Math.min(playlistList.count - 1, target));
                    if (target !== index) {
                        mirage.movePlaylistItem(index, target > index ? target + 1 : target);
                    }
                }
            }
        }
    }
}
