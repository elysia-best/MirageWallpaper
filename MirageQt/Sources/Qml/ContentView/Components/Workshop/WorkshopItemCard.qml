import QtQuick
import QtQuick.Layouts
import FluentUI
import "../../../GlobalComponents"
import "../../../MirageBridge.js" as MirageBridge

// 创意工坊卡片：继承 WallpaperItemCard 公共基类。悬停放大、选中态、
// 列表不播 GIF（基类 WorkshopImage 固定 isAnimating: false）、点击/双击
// 事件均由基类统一；本文件只保留创意工坊特有内容——图片左上类型徽章、
// 右上下载状态徽章（overlay）、标题/订阅数/进度/状态文本（contentData），
// 以及回调（左键选中 → selectWorkshopItem、双击 → downloadWorkshopItem）。
WallpaperItemCard {
    id: root

    // 紧凑模式（发现分区横向条）：小尺寸 + 隐藏订阅数行。
    property bool compact: false
    property string state: String(field("downloadState", ""))
    property bool downloaded: Boolean(field("downloaded", false))
    property bool active: Boolean(field("downloadActive", false))
    property double progress: Number(field("downloadProgress", -1))
    property var selectedItem: mirage.selectedWorkshopItem
    property bool selected: String(field("id", "")) === String(root.selectedItem.id || "")
    implicitWidth: compact ? 164 : 194
    implicitHeight: compact ? 204 : 244
    width: implicitWidth
    height: implicitHeight
    imagePreferredHeight: compact ? 124 : 146
    contentSpacing: 5

    // 图片覆盖层：类型徽章（左上）与下载状态徽章（右上），
    // 由基类 Loader 铺满图片区（anchors 相对图片区定位）。
    overlay: Component {
        Item {
            FluFrame {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: 5
                implicitWidth: typeText.implicitWidth + 12
                implicitHeight: 22
                radius: 3
                color: FluTheme.dark ? Qt.rgba(0, 0, 0, 0.72) : Qt.rgba(1, 1, 1, 0.88)
                FluText {
                    id: typeText
                    anchors.centerIn: parent
                    text: String(root.field("typeLabel", root.field("type", "创意工坊")))
                    font: FluTextStyle.Caption
                    color: FluTheme.fontPrimaryColor
                }
            }
            FluFrame {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 5
                implicitWidth: statusText.implicitWidth + 12
                implicitHeight: 22
                radius: 3
                visible: root.downloaded || root.state.length > 0
                color: root.state === "failed"
                    ? Qt.rgba(196 / 255, 43 / 255, 28 / 255, 0.92)
                    : root.active
                        ? FluTheme.primaryColor
                        : root.downloaded
                            ? Qt.rgba(16 / 255, 124 / 255, 16 / 255, 0.92)
                            : Qt.rgba(196 / 255, 121 / 255, 0, 0.92)
                FluText {
                    id: statusText
                    anchors.centerIn: parent
                    text: root.statusLabel()
                    color: "white"
                    font: FluTextStyle.Caption
                }
            }
        }
    }

    // 内容区（default property → 基类 contentHost）：标题/订阅数/进度/状态。
    FluText {
        Layout.fillWidth: true
        text: String(root.field("title", qsTr("未命名作品")))
        elide: Text.ElideRight
        font: FluTextStyle.BodyStrong
    }
    FluText {
        Layout.fillWidth: true
        visible: !root.compact
        text: String(root.field("typeLabel", ""))
            + (String(root.field("subscriptions", "")).length > 0
                ? "  ·  " + String(root.field("subscriptions", "")) + " " + qsTr("订阅") : "")
        elide: Text.ElideRight
        color: FluTheme.fontSecondaryColor
        font: FluTextStyle.Caption
    }
    FluProgressBar {
        Layout.fillWidth: true
        visible: root.active
        indeterminate: root.progress < 0
        from: 0
        to: 1
        value: root.progressValue()
    }
    FluText {
        Layout.fillWidth: true
        visible: root.active || root.downloaded || root.state === "failed"
        text: root.downloaded && root.state !== "failed"
            ? (Boolean(root.field("needsDependency", false))
                ? qsTr("缺少基础壁纸") : qsTr("已下载"))
            : String(root.field("downloadMessage", root.statusLabel()))
        elide: Text.ElideRight
        color: root.state === "failed"
            ? Qt.rgba(196 / 255, 43 / 255, 28 / 255, 1)
            : FluTheme.fontSecondaryColor
        font: FluTextStyle.Caption
    }

    // 回调：基类信号携带 mouse（含右键）；本卡片只响应左键，
    // 右键被基类 MouseArea 接收但忽略（右键菜单是已安装卡片的行为）。
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
