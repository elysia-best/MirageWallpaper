import QtQuick
import QtQuick.Layouts
import FluentUI

RowLayout {
    id: bar

    property int currentIndex: 0
    property int downloadCount: 0
    signal selected(int index)
    signal mobileRequested
    signal displayRequested
    signal settingsRequested

    RowLayout {
        spacing: 4
        Repeater {
            model: [qsTr("已安装"), qsTr("发现"), qsTr("创意工坊")]
            delegate: FluToggleButton {
                id: tabButton
                required property string modelData
                required property int index
                text: modelData
                checked: bar.currentIndex === index
                clickListener: function() { bar.selected(index); }
                FluBadge {
                    position: "topRight"
                    count: bar.downloadCount
                    visible: index === 2 && bar.downloadCount > 0
                }
            }
        }
    }

    Item { Layout.fillWidth: true }
    FluIconButton {
        iconSource: FluentIcons.MobileTablet
        text: qsTr("移动端")
        contentDescription: qsTr("移动端")
        onClicked: bar.mobileRequested()
    }
    FluIconButton {
        iconSource: FluentIcons.SettingsDisplaySound
        text: qsTr("显示器设置")
        contentDescription: qsTr("显示器设置")
        onClicked: bar.displayRequested()
    }
    FluIconButton {
        iconSource: FluentIcons.Settings
        text: qsTr("设置")
        contentDescription: qsTr("设置")
        onClicked: bar.settingsRequested()
    }
}
