import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    id: detail
    property var itemData: ({})
    signal download
    signal cancel
    Image {
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: Math.min(280, detail.width)
        Layout.preferredHeight: Layout.preferredWidth
        source: detail.itemData.preview || ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
    }
    FluText {
        Layout.fillWidth: true
        text: detail.itemData.title || "点击壁纸查看详情"
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        font: FluTextStyle.Subtitle
    }
    FluText {
        Layout.fillWidth: true
        text: detail.itemData.description || "暂无描述"
        wrapMode: Text.WordWrap
        maximumLineCount: 8
        elide: Text.ElideRight
    }
    FluFilledButton {
        Layout.fillWidth: true
        visible: !detail.itemData.downloaded && !detail.itemData.downloadActive
        text: "下载壁纸"
        onClicked: detail.download()
    }
    FluButton {
        Layout.fillWidth: true
        visible: !!detail.itemData.downloadActive
        text: "取消下载"
        onClicked: detail.cancel()
    }
}
