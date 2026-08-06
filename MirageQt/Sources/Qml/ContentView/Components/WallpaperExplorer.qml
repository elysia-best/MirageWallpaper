import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    id: root
    required property var host
    spacing: 6

    GridView {
        id: wallpaperGrid
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: root.host.pagedWallpapers
        cellWidth: Math.max(root.host.explorerIconSize,
                            Math.floor(width / Math.max(1, Math.floor(width / root.host.explorerIconSize))))
        cellHeight: cellWidth

        delegate: Item {
            required property var modelData
            width: wallpaperGrid.cellWidth
            height: wallpaperGrid.cellHeight

            FluFrame {
                anchors.fill: parent
                anchors.margins: 7
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 0
                    Image {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        source: modelData.preview
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: false
                    }
                    FluText {
                        id: titleText
                        Layout.fillWidth: true
                        Layout.preferredHeight: titleText.implicitHeight + 12
                        text: modelData.title
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                FluFocusRectangle {
                    anchors.fill: parent
                    visible: modelData.id === mirage.selectedWallpaperId
                    radius: 4
                }
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        mirage.selectWallpaper(modelData.id);
                        root.host.openWallpaperContextMenu(modelData);
                        return;
                    }
                    mirage.selectWallpaper(modelData.id);
                }
                onDoubleClicked: function(mouse) {
                    if (mouse.button === Qt.LeftButton) {
                        root.host.runWithWallpaperTrust(modelData, function() {
                            mirage.applyWallpaper(modelData.id, false);
                        });
                    }
                }
            }
        }
        FluText {
            anchors.centerIn: parent
            visible: wallpaperGrid.count === 0
            text: "没有找到匹配的壁纸。"
        }
    }

    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        visible: root.host.wallpaperPageCount > 1
        spacing: 6
        FluIconButton {
            text: "上一页"
            iconSource: FluentIcons.ChevronLeft
            enabled: root.host.wallpaperCurrentPage > 1
            onClicked: root.host.setWallpaperPage(root.host.wallpaperCurrentPage - 1)
        }
        Repeater {
            model: root.host.wallpaperPageItems()
            delegate: Item {
                required property int modelData
                Layout.preferredWidth: modelData === 0 ? 20 : pageButton.implicitWidth
                Layout.preferredHeight: Math.max(pageButton.implicitHeight, pageEllipsis.implicitHeight)
                FluText {
                    id: pageEllipsis
                    anchors.centerIn: parent
                    visible: parent.modelData === 0
                    text: "…"
                }
                FluButton {
                    id: pageButton
                    anchors.centerIn: parent
                    visible: parent.modelData !== 0
                    text: parent.modelData
                    enabled: parent.modelData !== root.host.wallpaperCurrentPage
                    onClicked: root.host.setWallpaperPage(parent.modelData)
                }
            }
        }
        FluIconButton {
            text: "下一页"
            iconSource: FluentIcons.ChevronRight
            enabled: root.host.wallpaperCurrentPage < root.host.wallpaperPageCount
            onClicked: root.host.setWallpaperPage(root.host.wallpaperCurrentPage + 1)
        }
        FluTextBox {
            Layout.preferredWidth: 54
            text: String(root.host.wallpaperCurrentPage)
            placeholderText: "页码"
            horizontalAlignment: Text.AlignHCenter
            onCommit: root.host.setWallpaperPage(text)
        }
        FluText { text: "/ " + root.host.wallpaperPageCount }
    }
}
