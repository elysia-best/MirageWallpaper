import QtQuick
import QtQuick.Layouts
import FluentUI
import "../../../MirageBridge.js" as MirageBridge

Item {
    id: root

    required property var host
    required property var itemData
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

    function field(name, fallback) {
        return MirageBridge.field(root.itemData, name, fallback);
    }

    function invoke(name) {
        return MirageBridge.invoke(mirage, name, Array.prototype.slice.call(arguments, 1));
    }

    function progressValue() {
        return MirageBridge.progressValue(root.progress);
    }

    FluFrame {
        id: card
        anchors.fill: parent
        color: root.selected ? FluTools.withOpacity(FluTheme.primaryColor, 0.12) : FluTheme.frameColor

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 7
            spacing: 5

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: root.compact ? 124 : 146
                FluImage {
                    anchors.fill: parent
                    source: root.field("preview", "")
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
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
        }
        FluFocusRectangle {
            anchors.fill: parent
            visible: root.selected
            radius: 4
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.invoke("selectWorkshopItem", String(root.field("id", "")))
        onDoubleClicked: root.invoke("downloadWorkshopItem", String(root.field("id", "")))
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
