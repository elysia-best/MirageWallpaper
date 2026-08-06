import QtQuick
import FluentUI

FluMenu {
    title: "视图"
    FluMenuItem {
        text: "在文件管理器中打开全部"
        iconSource: FluentIcons.FolderOpen
        onTriggered: Qt.openUrlExternally("file:///home")
    }
}
