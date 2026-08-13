import QtQuick
import QtQuick.Layouts
import FluentUI
import "../../GlobalComponents"

// 共享浏览控件：分页条 + 视图菜单（对齐 macOS SharedBrowseControls.swift 的
// PageNavigator + WallpaperGridViewMenu）。showsPageSize 为 false 时只显示
// 分页（订阅/浏览网格的分页条场景）；为 true 时额外显示图标尺寸/每页数量
// 菜单（toolbar 场景）。
RowLayout {
    id: controls
    property int currentPage: 1
    property int pageCount: 1
    property int explorerIconSize: 170
    property int wallpapersPerPage: 25
    property bool showsPageSize: false
    signal selected(int page)
    signal iconSizeChanged(int size)
    signal pageSizeChanged(int count)
    spacing: 6

    WallpaperGridViewMenu {
        visible: controls.showsPageSize
        explorerIconSize: controls.explorerIconSize
        wallpapersPerPage: controls.wallpapersPerPage
        showsPageSize: true
        onIconSizeChanged: size => controls.iconSizeChanged(size)
        onPageSizeChanged: count => controls.pageSizeChanged(count)
    }

    FluIconButton {
        text: "上一页"
        iconSource: FluentIcons.ChevronLeft
        enabled: controls.currentPage > 1
        onClicked: controls.selected(controls.currentPage - 1)
    }
    FluText {
        text: controls.currentPage + " / " + controls.pageCount
        font: FluTextStyle.BodyStrong
    }
    FluIconButton {
        text: "下一页"
        iconSource: FluentIcons.ChevronRight
        enabled: controls.currentPage < controls.pageCount
        onClicked: controls.selected(controls.currentPage + 1)
    }
}
