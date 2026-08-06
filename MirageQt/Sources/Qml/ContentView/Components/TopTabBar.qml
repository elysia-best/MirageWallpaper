import QtQuick
import QtQuick.Layouts
import FluentUI

RowLayout {
    id: bar
    property int currentIndex: 0
    property int downloadCount: 0
    signal selected(int index)
    spacing: 10

    RowLayout {
        spacing: 4
        Repeater {
            model: ["已安装", "发现", "创意工坊"]
            delegate: FluToggleButton {
                required property string modelData
                required property int index
                text: modelData
                checked: bar.currentIndex === index
                clickListener: function () {
                    bar.selected(index);
                }
            }
        }
        FluBadge {
            visible: bar.downloadCount > 0
            count: bar.downloadCount
        }
    }
    Item {
        Layout.fillWidth: true
    }
    FluIconButton {
        text: "移动端"
        iconSource: FluentIcons.MobileTablet
        onClicked: bar.mobileRequested()
    }
    FluIconButton {
        text: "显示器设置"
        iconSource: FluentIcons.SettingsDisplaySound
        onClicked: bar.displayRequested()
    }
    FluIconButton {
        text: "设置"
        iconSource: FluentIcons.Settings
        onClicked: bar.settingsRequested()
    }

    signal mobileRequested
    signal displayRequested
    signal settingsRequested
}
