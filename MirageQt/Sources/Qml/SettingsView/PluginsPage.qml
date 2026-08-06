import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    spacing: 12
    FluText {
        text: "插件"
        font: FluTextStyle.Title
    }
    FluText {
        Layout.fillWidth: true
        text: "Linux 版本当前没有可管理的 Mirage 插件。"
        wrapMode: Text.WordWrap
    }
    FluFrame {
        Layout.fillWidth: true
        Layout.preferredHeight: 72
        FluText {
            anchors.centerIn: parent
            text: "插件管理（TODO）"
        }
    }
}
