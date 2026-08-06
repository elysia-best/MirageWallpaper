import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    property var sections: []
    property bool loading: false
    property int trendDays: 7
    signal refreshRequested
    RowLayout {
        Layout.fillWidth: true
        FluText {
            text: "趋势范围"
        }
        FluComboBox {
            model: ["今日", "本周", "本月", "三个月", "半年", "一年"]
        }
        Item {
            Layout.fillWidth: true
        }
        FluIconButton {
            text: "刷新发现"
            iconSource: FluentIcons.Refresh
            onClicked: refreshRequested()
        }
    }
    FluProgressRing {
        Layout.alignment: Qt.AlignHCenter
        visible: parent.loading
        indeterminate: true
    }
    FluText {
        Layout.alignment: Qt.AlignHCenter
        visible: !parent.loading && parent.sections.length === 0
        text: "暂无发现内容"
    }
    Repeater {
        model: parent.sections
        delegate: DiscoverSectionView {
            required property var modelData
            title: modelData.title
            items: modelData.items
        }
    }
}
