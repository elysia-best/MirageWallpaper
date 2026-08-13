import QtQuick
import QtQuick.Layouts
import FluentUI
import "../MirageBridge.js" as MirageBridge

// 壁纸卡片公共基类：统一 4 个分区（已安装/发现/创意工坊/已订阅）的壁纸
// item 显示规则（用户定稿，与 macOS 有差异）：
//   1. 列表中的卡片一律不播放 GIF（isAnimating: false），GIF 只在右侧
//      详情页真实选中时播放（见 WorkshopItemDetail）；
//   2. 鼠标悬停卡片时整体微放大（对齐 macOS ExplorerItem 的
//      scaleEffect(1.03)），并提升 z 避免被相邻 item 遮挡；
//   3. 选中态统一为 FluFrame 主题色浅底 + FluFocusRectangle 焦点框。
// WorkshopItemCard / InstalledWallpaperCard 分别继承本组件，各自提供
// 内容（default property 进入图片区下方的内容区）与图片覆盖层（overlay，
// 如创意工坊卡片的类型/状态徽章），并连接各自的点击/双击回调
// （selectWorkshopItem vs selectWallpaper 等）。
Item {
    id: root

    required property var host
    required property var itemData
    // 选中态：由子类绑定各自数据源的选中判定（如
    // id === mirage.selectedWorkshopItem.id 或 id === mirage.selectedWallpaperId）。
    property bool selected: false
    // 悬停放大倍率（对齐 macOS ExplorerItem 的 1.03；子类可覆盖）。
    property double hoverScale: 1.03
    // 图片区高度：>=0 用固定高度（创意工坊卡片 146/124），<0 表示
    // 填满剩余空间（已安装卡片方形网格）。图片区统一走 WorkshopImage
    // 并固定 isAnimating: false，规则 1 的落点。
    property int imagePreferredHeight: -1
    // 内容区子项间距（创意工坊 5、已安装 0），默认 5。
    property int contentSpacing: 5
    // 图片区覆盖层：子类提供 Component（如创意工坊的类型/下载状态徽章），
    // 由基类 Loader 铺满图片区。QML 的 default property 只能有一个，
    // 内容区已占用，覆盖层需经具名属性注入。
    property Component overlay
    // 内容区：子类在根对象下直接写的子对象进入此处（图片区下方，
    // 按 ColumnLayout 纵向自然高度排列）。
    default property alias contentData: contentHost.data

    signal clicked(var mouse)
    signal doubleClicked(var mouse)

    function field(name, fallback) {
        return MirageBridge.field(root.itemData, name, fallback);
    }

    function invoke(name) {
        return MirageBridge.invoke(mirage, name, Array.prototype.slice.call(arguments, 1));
    }

    // 悬停反馈：scale 微放大 + z 提升（避免被 grid/list 相邻 item 盖住）。
    // easeOut 150ms 对齐 macOS 的 .easeOut(duration: 0.15)。
    scale: mouseArea.containsMouse ? root.hoverScale : 1.0
    z: mouseArea.containsMouse ? 10 : 0
    Behavior on scale {
        NumberAnimation {
            duration: 150
            easing.type: Easing.OutCubic
        }
    }

    FluFrame {
        id: card
        anchors.fill: parent
        color: root.selected
            ? FluTools.withOpacity(FluTheme.primaryColor, 0.12)
            : FluTheme.frameColor

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 7
            spacing: 5

            // 图片区：占位/加载/失败齐备的壁纸图；列表内永不播放 GIF。
            Item {
                id: imageItem
                Layout.fillWidth: true
                // preferredHeight 为 -1 表示"未设置"（Layout 回退到
                // implicitHeight），与 fillHeight 搭配让图片吃满剩余空间；
                // 不能用 undefined（preferredHeight 是 double，赋 undefined
                // 会触发 "Unable to assign [undefined] to double" 警告）。
                Layout.preferredHeight: root.imagePreferredHeight >= 0
                    ? root.imagePreferredHeight : -1
                Layout.fillHeight: root.imagePreferredHeight < 0

                WorkshopImage {
                    anchors.fill: parent
                    imageUrl: root.field("preview", "")
                    contentMode: Image.PreserveAspectCrop
                    // 规则 1：列表中的卡片不播放 GIF 动画（AnimatedImage
                    // playing: false 显示首帧），避免整屏 GIF 同时解码播放。
                    isAnimating: false
                }

                // 子类覆盖层（徽章等）铺满图片区。
                Loader {
                    anchors.fill: parent
                    sourceComponent: root.overlay
                }
            }

            // 内容区：子类内容（标题/订阅数/进度/状态等）自然高度排列在
            // 图片下方；图片 fillHeight 时（已安装）剩余空间被图片吃掉，
            // 文本自然落在底部。
            ColumnLayout {
                id: contentHost
                Layout.fillWidth: true
                spacing: root.contentSpacing
            }
        }
        FluFocusRectangle {
            anchors.fill: parent
            visible: root.selected
            radius: 4
        }
    }

    // 统一命中区：左右键都接收，按钮信息随信号传给子类（已安装需右键菜单）。
    // containsMouse 同时驱动悬停放大。
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: root.clicked(mouse)
        onDoubleClicked: root.doubleClicked(mouse)
    }
}
