import QtQuick
import QtQuick.Layouts
import FluentUI
import "../ContentViewLogic.js" as ContentViewLogic

FluScrollablePage {
    id: root
    required property var host

    GridView {
        id: wallpaperGrid
        Layout.fillWidth: true
        Layout.preferredHeight: contentHeight
        interactive: false
        clip: true
        model: root.host.pagedWallpapers
        // 自适应列宽（对齐 macOS GridItem(.adaptive(minimum:explorerIconSize,
        // maximum:2×explorerIconSize), spacing:14)）：列宽随 explorerIconSize
        // 变化（140/170/200 三档在宽窗口下均有差异），间隔统一 14。
        cellWidth: ContentViewLogic.adaptiveGridCellWidth(width, root.host.explorerIconSize, 14)
        cellHeight: cellWidth

        // 页码变化时把网格滚回顶部。原实现放在 ContentView.setWallpaperPage
        // 里直接引用本组件内 id（wallpaperGrid），跨文件作用域不可见，每次
        // 点击分页都抛 ReferenceError；滚动逻辑移到 grid 自身职责内，
        // 由页码属性变化信号驱动。
        Connections {
            target: root.host
            function onWallpaperCurrentPageChanged() {
                wallpaperGrid.positionViewAtBeginning();
            }
        }

        // 已安装卡片：继承公共卡片基类（悬停放大/选中态/列表不播 GIF），
        // 点击/双击/右键回调见 InstalledWallpaperCard.qml。
        // 卡片四周留 7px（cell - 14），与创意工坊/订阅网格的 14px 间隔一致
        // （对齐 macOS GridItem spacing 14）。
        delegate: InstalledWallpaperCard {
            required property var modelData
            host: root.host
            itemData: modelData
            width: wallpaperGrid.cellWidth - 14
            height: wallpaperGrid.cellHeight - 14
        }
    }

    FluText {
        Layout.fillWidth: true
        Layout.topMargin: 24
        visible: wallpaperGrid.count === 0
        text: "没有找到匹配的壁纸。"
        horizontalAlignment: Text.AlignHCenter
        color: FluTheme.fontSecondaryColor
    }

    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 6
        visible: root.host.wallpaperPageCount > 1
        spacing: 6
        FluIconButton {
            text: "上一页"
            iconSource: FluentIcons.ChevronLeft
            enabled: root.host.wallpaperCurrentPage > 1
            onClicked: root.host.setWallpaperPage(root.host.wallpaperCurrentPage - 1)
        }
        Repeater {
            model: root.host.wallpaperPageItems()
            delegate: Item {
                required property int modelData
                Layout.preferredWidth: modelData === 0 ? 20 : pageButton.implicitWidth
                Layout.preferredHeight: Math.max(pageButton.implicitHeight, pageEllipsis.implicitHeight)
                FluText {
                    id: pageEllipsis
                    anchors.centerIn: parent
                    visible: parent.modelData === 0
                    text: "…"
                }
                FluButton {
                    id: pageButton
                    anchors.centerIn: parent
                    visible: parent.modelData !== 0
                    text: parent.modelData
                    enabled: parent.modelData !== root.host.wallpaperCurrentPage
                    onClicked: root.host.setWallpaperPage(parent.modelData)
                }
            }
        }
        FluIconButton {
            text: "下一页"
            iconSource: FluentIcons.ChevronRight
            enabled: root.host.wallpaperCurrentPage < root.host.wallpaperPageCount
            onClicked: root.host.setWallpaperPage(root.host.wallpaperCurrentPage + 1)
        }
        FluTextBox {
            Layout.preferredWidth: 54
            text: String(root.host.wallpaperCurrentPage)
            placeholderText: "页码"
            horizontalAlignment: Text.AlignHCenter
            onCommit: root.host.setWallpaperPage(text)
        }
        FluText { text: "/ " + root.host.wallpaperPageCount }
    }
}
