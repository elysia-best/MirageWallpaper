import QtQuick
import QtQuick.Layouts
import FluentUI
import "../../../GlobalComponents"

MirageDialogWindow {
    property var playlists: []
    title: "打开播放列表"
    positiveText: "完成"
    contentDelegate: ColumnLayout {
        FluText {
            Layout.alignment: Qt.AlignHCenter
            visible: parent.parent.playlists.length === 0
            text: "您尚未创建任何播放列表。"
        }
        Repeater {
            model: parent.parent.playlists
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                FluText {
                    Layout.fillWidth: true
                    text: modelData.name
                }
                FluButton {
                    text: "读取"
                }
                FluIconButton {
                    text: "删除"
                    iconSource: FluentIcons.Delete
                }
            }
        }
    }
}
