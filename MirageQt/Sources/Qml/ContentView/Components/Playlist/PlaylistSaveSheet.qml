import QtQuick
import QtQuick.Layouts
import FluentUI
import "../../../GlobalComponents"

MirageDialogWindow {
    property string playlistName: ""
    title: "保存播放列表"
    positiveText: "保存"
    negativeText: "取消"
    contentDelegate: ColumnLayout {
        FluText {
            text: "名称"
            font: FluTextStyle.BodyStrong
        }
        FluTextBox {
            Layout.fillWidth: true
            text: parent.parent.playlistName
            onTextChanged: parent.parent.playlistName = text
        }
        FluText {
            Layout.fillWidth: true
            text: "如果已存在相同名称的播放列表，则它将会被覆盖。"
            wrapMode: Text.WordWrap
        }
    }
}
