import QtQuick
import QtQuick.Layouts
import FluentUI

// 已安装壁纸详情面板：对应 macOS Components/WallpaperPreview.swift。
// 展示选中壁纸的预览、元数据、播放控制与属性编辑，操作经 host（窗口）
// 与 mirage 完成；编辑元数据/删除壁纸以信号通知窗口打开对话框。
ColumnLayout {
    id: root
    required property var host
    signal metadataEditRequested
    signal deleteRequested

    spacing: 16

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

    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(280, root.width)
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
            onClicked: root.metadataEditRequested()
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
        host: root.host
        properties: mirage.selectedProperties
    }

    DetailSectionHeader {
        title: "壁纸"
    }
    FluFilledButton {
        Layout.fillWidth: true
        text: "应用"
        enabled: mirage.selectedWallpaperId.length > 0
        onClicked: root.host.runWithWallpaperTrust(mirage.selectedWallpaper, function () {
            mirage.applySelected(false);
        })
    }
    FluButton {
        Layout.fillWidth: true
        text: "应用到所有显示器"
        enabled: mirage.selectedWallpaperId.length > 0
        onClicked: root.host.runWithWallpaperTrust(mirage.selectedWallpaper, function () {
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
        onClicked: root.deleteRequested()
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
