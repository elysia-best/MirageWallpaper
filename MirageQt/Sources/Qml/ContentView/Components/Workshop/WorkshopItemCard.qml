import QtQuick
import QtQuick.Layouts
import FluentUI
import "../../../GlobalComponents"
import "../../../MirageBridge.js" as MirageBridge

// 创意工坊卡片：标题、统计、类别和下载状态全部叠在正方形封面上。共享
// WallpaperItemCard 负责裁剪与渐变，避免此处的领域信息破坏 macOS 卡片比例。
WallpaperItemCard {
    id: root

    // 紧凑模式用于发现区的横向条，仍保持相同的正方形卡片协议。
    property bool compact: false
    property string state: String(field("downloadState", ""))
    property bool downloaded: Boolean(field("downloaded", false))
    property bool active: Boolean(field("downloadActive", false))
    property double progress: Number(field("downloadProgress", -1))
    property var selectedItem: mirage.selectedWorkshopItem
    property bool selected: String(field("id", "")) === String(root.selectedItem.id || "")
    implicitWidth: compact ? 164 : 194
    implicitHeight: implicitWidth
    width: implicitWidth
    height: implicitHeight

    // 下载角标使用 FluentUI 图标与文字，位于右上角且不占用封面信息区。
    // 下载中仍显示实时状态，完成时严格使用参考图的绿色“已下载”胶囊。
    overlay: Component {
        // Loader 的显式 fill 尺寸会传给它的根对象；因此根对象必须是无绘制
        // 的 Item。若直接以 FluFrame 为根，Frame 会被拉成整个封面，导致下载
        // 状态的底色错误覆盖图片中央。
        Item {
            FluFrame {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 8
                implicitWidth: statusRow.implicitWidth + 14
                implicitHeight: 24
                radius: 12
                visible: root.downloaded || root.state.length > 0
                color: root.state === "failed"
                    ? Qt.rgba(196 / 255, 43 / 255, 28 / 255, 0.92)
                    : root.active
                        ? FluTheme.primaryColor
                        : root.downloaded
                            ? Qt.rgba(16 / 255, 124 / 255, 16 / 255, 0.92)
                            : Qt.rgba(196 / 255, 121 / 255, 0, 0.92)
                RowLayout {
                    id: statusRow
                    anchors.centerIn: parent
                    spacing: 3
                    FluIcon {
                        iconSource: root.downloaded ? FluentIcons.CheckMark : FluentIcons.Download
                        iconSize: 12
                        iconColor: "white"
                    }
                    FluText {
                        text: root.statusLabel()
                        color: "white"
                        font: FluTextStyle.Caption
                    }
                }
            }
        }
    }

    // 信息区通过基类 default property 固定在渐变之上；文字均为白色，适配
    // 明亮和深色封面。统计字段来自 workshopItemMap 的既定协议。
    FluText {
        Layout.fillWidth: true
        text: String(root.field("title", qsTr("未命名作品")))
        elide: Text.ElideRight
        font: FluTextStyle.BodyStrong
        color: "white"
    }
    // 使用固定高度的 Item 分离左右两组布局：统计区永远从左开始，类型胶囊
    // 直接锚定右边界，不参与统计文本的宽度争抢，因此不会被挤出卡片。
    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 18
        RowLayout {
            id: statsRow
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            FluIcon {
                iconSource: FluentIcons.Download
                iconSize: 11
                iconColor: Qt.rgba(1, 1, 1, 0.84)
            }
            FluText {
                text: String(root.field("subscriptions", ""))
                elide: Text.ElideRight
                color: Qt.rgba(1, 1, 1, 0.92)
                font.pixelSize: 10
            }
            FluIcon {
                iconSource: FluentIcons.RedEye
                iconSize: 11
                iconColor: Qt.rgba(1, 1, 1, 0.84)
            }
            FluText {
                text: String(root.field("views", ""))
                elide: Text.ElideRight
                color: Qt.rgba(1, 1, 1, 0.92)
                font.pixelSize: 10
            }
        }
        FluFrame {
            id: typeBadge
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(Math.max(typeText.implicitWidth + 10, 34), 54)
            height: 18
            radius: 9
            color: Qt.rgba(126 / 255, 87 / 255, 194 / 255, 0.94)
            FluText {
                id: typeText
                anchors.centerIn: parent
                width: parent.width - 10
                text: String(root.field("typeLabel", ""))
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                color: "white"
                font.pixelSize: 10
            }
        }
    }
    FluProgressBar {
        Layout.fillWidth: true
        Layout.preferredHeight: 3
        visible: root.active
        indeterminate: root.progress < 0
        from: 0
        to: 1
        value: root.progressValue()
    }

    // 左键选中、双击下载；右键没有创意工坊菜单，因此保持无副作用。
    onClicked: function(mouse) {
        if (mouse.button !== Qt.LeftButton)
            return;
        root.invoke("selectWorkshopItem", String(root.field("id", "")));
    }
    onDoubleClicked: function(mouse) {
        if (mouse.button !== Qt.LeftButton)
            return;
        root.invoke("downloadWorkshopItem", String(root.field("id", "")));
    }

    function progressValue() {
        return MirageBridge.progressValue(root.progress);
    }

    function statusLabel() {
        if (root.state === "queued") return qsTr("排队中");
        if (root.state === "starting") return qsTr("启动中");
        if (root.state === "downloading") {
            return root.progress < 0 ? qsTr("连接中") : Math.round(root.progressValue() * 100) + "%";
        }
        if (root.state === "validating") return qsTr("验证中");
        if (root.state === "completed" || root.downloaded) return qsTr("已下载");
        if (root.state === "failed") return qsTr("失败");
        if (root.state === "cancelled") return qsTr("已取消");
        return qsTr("创意工坊");
    }
}
