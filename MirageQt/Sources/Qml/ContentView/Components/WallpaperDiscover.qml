import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    spacing: 12
    FluIcon {
        Layout.alignment: Qt.AlignHCenter
        iconSource: FluentIcons.Settings
        iconSize: 48
    }
    FluText {
        Layout.alignment: Qt.AlignHCenter
        text: "功能开发中"
    }
}
