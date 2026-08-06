import QtQuick
import FluentUI

FluMenuBar {
    FluMenu {
        title: "文件"
        FluMenuItem {
            text: "导入壁纸…"
        }
        FluMenuItem {
            text: "关闭窗口"
        }
    }
    FluMenu {
        title: "查看"
        FluMenuItem {
            text: "显示筛选结果"
        }
        FluMenuItem {
            text: "进入全屏"
        }
    }
    FluMenu {
        title: "帮助"
        FluMenuItem {
            text: "Mirage 项目主页"
            onTriggered: Qt.openUrlExternally("https://github.com/laobamac/MirageWallpaper")
        }
        FluMenuItem {
            text: "更新（TODO：Linux 更新服务）"
            enabled: false
        }
    }
}
