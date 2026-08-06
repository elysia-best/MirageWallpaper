import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    id: root

    required property var host
    property var item: mirage.selectedWorkshopItem
    property string itemId: String(field("id", ""))
    property string state: String(field("downloadState", ""))
    property bool installed: Boolean(field("downloaded", false))
    property bool needsDependency: Boolean(field("needsDependency", false))
    property bool active: Boolean(field("downloadActive", false))
        || ["queued", "starting", "downloading", "validating"].indexOf(root.state) >= 0
    property double progress: Number(field("downloadProgress", -1))

    spacing: 12

    function value(name, fallback) {
        var result = mirage[name];
        return result === undefined || result === null ? fallback : result;
    }

    function field(name, fallback) {
        var result = root.item ? root.item[name] : undefined;
        return result === undefined || result === null ? fallback : result;
    }

    function invoke(name) {
        var fn = mirage[name];
        if (typeof fn !== "function")
            return false;
        fn.apply(mirage, Array.prototype.slice.call(arguments, 1));
        return true;
    }

    function progressValue() {
        var number = root.progress;
        if (number > 1)
            number /= 100;
        return Math.max(0, Math.min(1, number));
    }

    function workshopUrl() {
        return "https://steamcommunity.com/sharedfiles/filedetails/?id=" + root.itemId;
    }

    function openDownloadDirectory() {
        root.invoke("revealWorkshopDownload", root.itemId);
    }

    ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 12
            visible: root.itemId.length > 0

            FluImage {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Math.min(280, Math.max(180, root.width - 30))
                Layout.preferredHeight: Layout.preferredWidth
                source: String(root.field("preview", ""))
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }

            FluText {
                Layout.fillWidth: true
                text: String(root.field("title", qsTr("创意工坊作品")))
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                font: FluTextStyle.Subtitle
            }

            FluButton {
                Layout.fillWidth: true
                visible: String(root.field("creatorSteamId", "")).length > 0
                text: qsTr("查看作者的其他创意工坊作品")
                onClicked: {
                    var id = String(root.field("creatorSteamId", ""));
                    if (!root.invoke("openCreatorWorkshop", id))
                        Qt.openUrlExternally("https://steamcommunity.com/profiles/" + id + "/myworkshopfiles/");
                }
            }

            FluFrame {
                Layout.fillWidth: true
                Layout.preferredHeight: presetNoticeContent.implicitHeight + 18
                visible: String(root.field("type", "")) === "preset"
                    || Boolean(root.field("preset", false))
                RowLayout {
                    id: presetNoticeContent
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: 8
                    FluIcon {
                        iconSource: FluentIcons.SliderThumb
                        iconSize: 18
                        iconColor: Qt.rgba(120 / 255, 70 / 255, 160 / 255, 1)
                    }
                    FluText {
                        Layout.fillWidth: true
                        text: qsTr("创意工坊预设可能需要基础壁纸。")
                        wrapMode: Text.WordWrap
                        color: FluTheme.fontSecondaryColor
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: [
                        [FluentIcons.Download, String(root.field("subscriptions", "")), qsTr("订阅")],
                        [FluentIcons.HeartFill, String(root.field("favorited", "")), qsTr("收藏")],
                        [FluentIcons.RedEye, String(root.field("views", "")), qsTr("浏览")]
                    ]
                    delegate: FluFrame {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 5
                            spacing: 2
                            FluIcon {
                                Layout.alignment: Qt.AlignHCenter
                                iconSource: modelData[0]
                                iconSize: 15
                                iconColor: FluTheme.primaryColor
                            }
                            FluText {
                                Layout.alignment: Qt.AlignHCenter
                                text: modelData[1] + " " + modelData[2]
                                font: FluTextStyle.Caption
                                color: FluTheme.fontSecondaryColor
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                FluText {
                    text: String(root.field("typeLabel", root.field("type", "")))
                    color: FluTheme.fontSecondaryColor
                }
                FluText {
                    Layout.fillWidth: true
                    text: String(root.field("sizeLabel", root.field("size", "")))
                    color: FluTheme.fontSecondaryColor
                }
                FluText {
                    visible: String(root.field("rating", "")).length > 0
                    text: String(root.field("rating", ""))
                    color: String(root.field("rating", "")) === "Mature"
                        ? Qt.rgba(196 / 255, 43 / 255, 28 / 255, 1)
                        : Qt.rgba(196 / 255, 121 / 255, 0, 1)
                }
            }

            FluDivider { Layout.fillWidth: true }
            FluText { text: qsTr("标签"); font: FluTextStyle.BodyStrong }
            Flow {
                Layout.fillWidth: true
                spacing: 5
                visible: root.field("tags", []).length > 0
                Repeater {
                    model: root.field("tags", [])
                    delegate: FluFrame {
                        required property var modelData
                        implicitWidth: tagText.implicitWidth + 14
                        implicitHeight: 24
                        radius: 3
                        FluText {
                            id: tagText
                            anchors.centerIn: parent
                            text: String(modelData)
                            color: FluTheme.fontSecondaryColor
                            font: FluTextStyle.Caption
                        }
                    }
                }
            }

            FluText {
                Layout.fillWidth: true
                visible: root.field("tags", []).length === 0
                text: qsTr("无标签")
                color: FluTheme.fontTertiaryColor
            }

            FluDivider { Layout.fillWidth: true }
            FluText { text: qsTr("描述"); font: FluTextStyle.BodyStrong }
            FluText {
                Layout.fillWidth: true
                text: String(root.field("description", "")).length > 0
                    ? String(root.field("description", "")) : qsTr("无描述")
                wrapMode: Text.WordWrap
                maximumLineCount: 8
                elide: Text.ElideRight
                color: FluTheme.fontSecondaryColor
            }

            FluDivider { Layout.fillWidth: true }
            FluText { text: qsTr("操作"); font: FluTextStyle.BodyStrong }

            FluFrame {
                Layout.fillWidth: true
                Layout.preferredHeight: dependencyNoticeContent.implicitHeight + 18
                visible: root.installed && root.needsDependency
                RowLayout {
                    id: dependencyNoticeContent
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: 8
                    FluIcon {
                        iconSource: FluentIcons.Warning
                        iconSize: 18
                        iconColor: Qt.rgba(196 / 255, 121 / 255, 0, 1)
                    }
                    FluText {
                        Layout.fillWidth: true
                        text: qsTr("此预设已安装，但缺少基础壁纸。")
                        wrapMode: Text.WordWrap
                        color: FluTheme.fontSecondaryColor
                    }
                    FluButton {
                        text: qsTr("下载基础壁纸")
                        onClicked: {
                            root.invoke("requestWorkshopPresetDependency", root.itemId);
                        }
                    }
                }
            }

            FluText {
                Layout.fillWidth: true
                visible: root.active
                text: {
                    if (root.state === "queued") return qsTr("等待 SteamCMD 按顺序下载…");
                    if (root.state === "starting") return qsTr("正在启动 SteamCMD…");
                    if (root.state === "validating") return qsTr("正在验证下载...");
                    if (root.state === "downloading") return root.progress < 0
                        ? qsTr("正在连接 Steam...")
                        : qsTr("正在下载 (%1%)").arg(Math.round(root.progressValue() * 100));
                    return String(root.field("downloadMessage", qsTr("正在处理下载...")));
                }
                color: FluTheme.fontSecondaryColor
            }
            FluProgressBar {
                Layout.fillWidth: true
                visible: root.active
                indeterminate: root.progress < 0 || root.state === "starting" || root.state === "validating"
                from: 0
                to: 1
                value: root.progressValue()
            }
            FluButton {
                Layout.fillWidth: true
                visible: root.state === "failed"
                text: qsTr("重试下载")
                onClicked: root.invoke("retryWorkshopDownload", root.itemId)
            }
            FluFilledButton {
                Layout.fillWidth: true
                visible: !root.installed && !root.active && root.state !== "failed"
                    && root.itemId.length > 0 && Boolean(root.value("steamReady", false))
                text: root.needsDependency ? qsTr("下载基础壁纸") : qsTr("下载壁纸")
                onClicked: root.invoke("downloadWorkshopItem", root.itemId)
            }
            FluButton {
                Layout.fillWidth: true
                visible: !root.installed && !root.active && root.state !== "failed"
                    && root.itemId.length > 0 && !Boolean(root.value("steamReady", false))
                text: qsTr("设置 Steam 后下载")
                onClicked: root.host.openSteamSetup()
            }
            FluButton {
                Layout.fillWidth: true
                visible: root.active
                text: qsTr("取消下载")
                onClicked: root.invoke("cancelWorkshopDownload", root.itemId)
            }
            FluButton {
                Layout.fillWidth: true
                visible: root.installed && !root.active
                text: qsTr("打开下载目录")
                onClicked: root.openDownloadDirectory()
            }
            FluButton {
                Layout.fillWidth: true
                visible: root.itemId.length > 0
                text: qsTr("在 Steam 中查看")
                onClicked: Qt.openUrlExternally(root.workshopUrl())
            }

            FluDivider { Layout.fillWidth: true }
            FluText {
                Layout.fillWidth: true
                text: qsTr("创意工坊 ID：%1").arg(root.itemId)
                color: FluTheme.fontTertiaryColor
                font: FluTextStyle.Caption
                elide: Text.ElideRight
            }
            FluText {
                Layout.fillWidth: true
                visible: String(root.field("updatedAt", "")).length > 0
                text: qsTr("更新于：%1").arg(String(root.field("updatedAt", "")))
                color: FluTheme.fontTertiaryColor
                font: FluTextStyle.Caption
            }
        }

        ColumnLayout {
            anchors.centerIn: parent
            visible: root.itemId.length === 0
            spacing: 10
            FluIcon {
                Layout.alignment: Qt.AlignHCenter
                iconSource: FluentIcons.PreviewLink
                iconSize: 34
                iconColor: FluTheme.fontTertiaryColor
            }
            FluText {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("选择创意工坊壁纸以查看详情")
                color: FluTheme.fontSecondaryColor
            }
        }
    }
}
