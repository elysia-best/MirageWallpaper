import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    id: preview
    property var wallpaper: ({})
    property var properties: []
    signal apply(bool allScreens)
    signal stop
    signal favorite
    Layout.fillWidth: true
    Image {
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: Math.min(280, preview.width)
        Layout.preferredHeight: Layout.preferredWidth
        source: preview.wallpaper.preview || ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
    }
    FluText {
        Layout.fillWidth: true
        text: preview.wallpaper.title || "请选择一个壁纸"
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        font: FluTextStyle.Subtitle
    }
    FluText {
        Layout.fillWidth: true
        text: preview.wallpaper.author || ""
        horizontalAlignment: Text.AlignHCenter
    }
    FluFilledButton {
        Layout.fillWidth: true
        text: "应用"
        enabled: !!preview.wallpaper.id
        onClicked: preview.apply(false)
    }
    FluButton {
        Layout.fillWidth: true
        text: "应用到所有显示器"
        enabled: !!preview.wallpaper.id
        onClicked: preview.apply(true)
    }
    FluButton {
        Layout.fillWidth: true
        text: "停止壁纸"
        onClicked: preview.stop()
    }
}
