import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    property var tasks: []
    FluText {
        text: "下载管理"
        font: FluTextStyle.Subtitle
    }
    FluText {
        Layout.alignment: Qt.AlignHCenter
        visible: parent.tasks.length === 0
        text: "暂无下载任务"
    }
    Repeater {
        model: parent.tasks
        delegate: RowLayout {
            required property var modelData
            Layout.fillWidth: true
            FluText {
                Layout.fillWidth: true
                text: modelData.title
            }
            FluProgressBar {
                Layout.preferredWidth: 120
                value: modelData.progress || 0
            }
        }
    }
}
