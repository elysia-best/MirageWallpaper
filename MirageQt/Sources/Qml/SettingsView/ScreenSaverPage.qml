import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    spacing: 14
    FluText {
        text: "屏保"
        font: FluTextStyle.Title
    }
    FluText {
        Layout.fillWidth: true
        text: "屏保组件（TODO：Linux 屏保宿主）"
        font: FluTextStyle.Subtitle
    }
    FluText {
        Layout.fillWidth: true
        text: "Linux Qt 版本暂不提供 macOS 动态屏保安装、配置和系统设置入口。"
        wrapMode: Text.WordWrap
    }
    FluButton {
        text: "打开系统屏保设置（TODO）"
        enabled: false
    }
    FluButton {
        text: "将正在播放的壁纸设为屏保（TODO）"
        enabled: false
    }
}
