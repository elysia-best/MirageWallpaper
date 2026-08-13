import QtQuick
import QtQuick.Controls
import FluentUI

// 视图菜单（对齐 macOS SharedBrowseControls.swift 的 WallpaperGridViewMenu）：
// 图标尺寸（小 140 / 中 170 / 大 200）与每页壁纸数（showsPageSize 时显示
// 每页 10/25/50）；ExplorerTopBar 与订阅视图 toolbar 共用。
FluDropDownButton {
    id: menu

    property int explorerIconSize: 170
    property int wallpapersPerPage: 25
    property bool showsPageSize: false
    signal iconSizeChanged(int size)
    signal pageSizeChanged(int count)

    text: "视图"

    FluMenuItem {
        text: qsTr("小图标")
        checkable: true
        checked: menu.explorerIconSize === 140
        onTriggered: menu.iconSizeChanged(140)
    }
    FluMenuItem {
        text: qsTr("中图标")
        checkable: true
        checked: menu.explorerIconSize === 170
        onTriggered: menu.iconSizeChanged(170)
    }
    FluMenuItem {
        text: qsTr("大图标")
        checkable: true
        checked: menu.explorerIconSize === 200
        onTriggered: menu.iconSizeChanged(200)
    }

    FluMenuSeparator {
        visible: menu.showsPageSize
    }

    FluMenuItem {
        text: qsTr("每页 10 个")
        visible: menu.showsPageSize
        checkable: true
        checked: menu.wallpapersPerPage === 10
        onTriggered: menu.pageSizeChanged(10)
    }
    FluMenuItem {
        text: qsTr("每页 25 个")
        visible: menu.showsPageSize
        checkable: true
        checked: menu.wallpapersPerPage === 25
        onTriggered: menu.pageSizeChanged(25)
    }
    FluMenuItem {
        text: qsTr("每页 50 个")
        visible: menu.showsPageSize
        checkable: true
        checked: menu.wallpapersPerPage === 50
        onTriggered: menu.pageSizeChanged(50)
    }
}
