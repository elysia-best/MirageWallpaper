import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    id: settings
    property int selection: 0
    property var values: ({})
    signal accepted(var values)
    signal cancelled
    RowLayout {
        Layout.fillWidth: true
        Repeater {
            model: ["性能", "通用", "插件", "屏保", "关于"]
            delegate: FluToggleButton {
                required property string modelData
                required property int index
                text: modelData
                checked: settings.selection === index
                clickListener: function () {
                    settings.selection = index;
                }
            }
        }
    }
    FluDivider {
        Layout.fillWidth: true
    }
    StackLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        currentIndex: settings.selection
        PerformancePage {
            values: settings.values
        }
        GeneralPage {
            values: settings.values
        }
        PluginsPage {}
        ScreenSaverPage {}
        AboutUsView {}
    }
    RowLayout {
        Layout.fillWidth: true
        Item {
            Layout.fillWidth: true
        }
        FluButton {
            text: "取消"
            onClicked: settings.cancelled()
        }
        FluFilledButton {
            text: "好"
            onClicked: settings.accepted(settings.values)
        }
    }
}
