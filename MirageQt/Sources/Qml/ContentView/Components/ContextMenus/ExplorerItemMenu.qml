import QtQuick
import FluentUI

FluMenu {
    id: menu
    property var wallpaper: ({})
    title: "壁纸"
    signal unavailableFeatureRequested(string feature)
    FluMenuItem {
        text: "设为屏保（TODO）"
        iconSource: FluentIcons.SettingsDisplaySound
        onTriggered: unavailableFeatureRequested("screen saver")
    }
    FluMenuItem {
        text: "在文件管理器中显示"
        iconSource: FluentIcons.FolderOpen
        onTriggered: Qt.openUrlExternally(menu.wallpaper.location)
    }
}
