import QtQuick
import QtQuick.Window
import FluentUI

// 窗口菜单栏：对应 macOS AppDelegate.setMainMenu()。
// 放在窗口 appBar 左侧；动作经 host（窗口）触发，未实现功能保持 TODO 禁用。
FluMenuBar {
    id: menuBar
    required property var host

    FluMenu {
        title: "文件"
        FluMenuItem {
            text: "导入壁纸…"
            onTriggered: menuBar.host.openImportPanel()
        }
        FluMenuItem {
            text: "关闭窗口"
            onTriggered: menuBar.host.close()
        }
    }
    FluMenu {
        title: "查看"
        FluMenuItem {
            text: "显示筛选结果"
            onTriggered: menuBar.host.filtersVisible = !menuBar.host.filtersVisible
        }
        FluMenuItem {
            text: "进入全屏"
            onTriggered: menuBar.host.visibility = menuBar.host.visibility === Window.FullScreen
                ? Window.Windowed : Window.FullScreen
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
