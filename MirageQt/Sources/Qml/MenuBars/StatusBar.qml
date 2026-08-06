import QtQuick
import QtQuick.Layouts
import FluentUI

RowLayout {
    property var controller
    FluIconButton {
        text: "暂停"
        iconSource: FluentIcons.Pause
        onClicked: controller.pauseWallpapers()
    }
    FluIconButton {
        text: "静音"
        iconSource: FluentIcons.Mute
        onClicked: controller.muteWallpapers()
    }
    FluIconButton {
        text: "停止壁纸"
        iconSource: FluentIcons.Stop
        onClicked: controller.stopWallpapers()
    }
}
