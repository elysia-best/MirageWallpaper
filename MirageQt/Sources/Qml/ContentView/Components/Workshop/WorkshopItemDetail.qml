import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    id: root
    required property var host
    spacing: 16

    Image {
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: Math.min(280, root.width)
        Layout.preferredHeight: Layout.preferredWidth
        visible: mirage.selectedWorkshopItem.id !== undefined
        source: mirage.selectedWorkshopItem.preview || ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
    }
    FluText {
        Layout.fillWidth: true
        text: mirage.selectedWorkshopItem.title || "点击壁纸查看详情"
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        font: FluTextStyle.Subtitle
    }
    FluText {
        Layout.fillWidth: true
        text: mirage.selectedWorkshopItem.typeLabel || ""
        horizontalAlignment: Text.AlignHCenter
    }
    FluText {
        Layout.fillWidth: true
        text: mirage.selectedWorkshopItem.description || "暂无描述"
        wrapMode: Text.WordWrap
        maximumLineCount: 8
        elide: Text.ElideRight
    }
    FluButton {
        Layout.fillWidth: true
        visible: mirage.selectedWorkshopItem.downloadState === "failed"
        text: "重试下载"
        onClicked: mirage.retryWorkshopDownload(mirage.selectedWorkshopItem.id)
    }
    FluFilledButton {
        Layout.fillWidth: true
        visible: !mirage.selectedWorkshopItem.downloaded && !mirage.selectedWorkshopItem.downloadActive && mirage.selectedWorkshopItem.id !== undefined
        text: mirage.selectedWorkshopItem.needsDependency ? "需要基础壁纸" : "下载壁纸"
        onClicked: mirage.downloadWorkshopItem(mirage.selectedWorkshopItem.id)
    }
    FluButton {
        Layout.fillWidth: true
        visible: !!mirage.selectedWorkshopItem.downloadActive
        text: "取消下载"
        onClicked: mirage.cancelWorkshopDownload(mirage.selectedWorkshopItem.id)
    }
    FluButton {
        Layout.fillWidth: true
        visible: mirage.selectedWorkshopItem.id !== undefined
        text: "在 Steam 中查看（TODO：Linux 外部 Steam 集成）"
        onClicked: root.host.showLinuxNotice()
    }
}
