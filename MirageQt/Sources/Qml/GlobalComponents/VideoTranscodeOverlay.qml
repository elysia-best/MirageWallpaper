import QtQuick
import QtQuick.Layouts
import FluentUI

FluFrame {
    visible: false
    property string title: "正在转换视频格式"
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        FluText {
            text: parent.parent.title
            font: FluTextStyle.BodyStrong
        }
        FluText {
            text: "TODO：Linux 渲染器尚未暴露视频转码进度。"
            wrapMode: Text.WordWrap
        }
    }
}
