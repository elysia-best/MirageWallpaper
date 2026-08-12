import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI
import "../../../MirageBridge.js" as MirageBridge

FluPopup {
    id: popup

    property var anchorItem
    property var tasks: undefined
    property var fallbackTasks: []
    property var queue: {
        var current = tasks;
        if (current !== undefined && current !== null)
            return current;
        return fallbackTasks;
    }

    width: 430
    height: Math.min(520, Math.max(180, queue.length > 0 ? 490 : 230))
    modal: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function value(name, fallback) {
        return MirageBridge.value(mirage, name, fallback);
    }

    function taskValue(task, name, fallback) {
        var result = task ? task[name] : undefined;
        if ((result === undefined || result === null) && task && task.workshopItem)
            result = task.workshopItem[name];
        return result === undefined || result === null ? fallback : result;
    }

    function taskId(task) {
        return String(taskValue(task, "id", taskValue(task, "workshopId", "")));
    }

    function state(task) {
        return String(taskValue(task, "downloadState", taskValue(task, "state", "queued")));
    }

    function progress(task) {
        var number = Number(taskValue(task, "downloadProgress", taskValue(task, "progress", -1)));
        if (number > 1)
            number /= 100;
        return Math.max(0, Math.min(1, number));
    }

    function invoke(name) {
        return MirageBridge.invoke(mirage, name, Array.prototype.slice.call(arguments, 1));
    }

    function openFor(item) {
        anchorItem = item;
        open();
        Qt.callLater(positionNearAnchor);
    }

    function positionNearAnchor() {
        if (!anchorItem || !parent)
            return;
        var point = anchorItem.mapToItem(parent, 0, anchorItem.height);
        x = Math.max(8, Math.min(parent.width - width - 8, point.x + anchorItem.width - width));
        y = Math.max(8, Math.min(parent.height - height - 8, point.y + 4));
    }

    onOpened: positionNearAnchor()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            Layout.leftMargin: 16
            Layout.rightMargin: 10
            FluText {
                text: qsTr("下载管理")
                font: FluTextStyle.Subtitle
            }
            Item { Layout.fillWidth: true }
            FluButton {
                visible: popup.queue.some(function(task) {
                    var state = popup.state(task);
                    return state === "completed" || state === "failed" || state === "cancelled";
                })
                text: qsTr("清除记录")
                onClicked: popup.invoke("clearCompletedDownloads")
            }
        }

        FluDivider { Layout.fillWidth: true }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.alignment: Qt.AlignHCenter
            visible: popup.queue.length === 0
            spacing: 8
            FluIcon {
                Layout.alignment: Qt.AlignHCenter
                iconSource: FluentIcons.Download
                iconSize: 34
                iconColor: FluTheme.fontTertiaryColor
            }
            FluText {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("暂无下载任务")
                color: FluTheme.fontSecondaryColor
            }
            FluText {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("在创意工坊中浏览并下载壁纸")
                color: FluTheme.fontTertiaryColor
                font: FluTextStyle.Caption
            }
        }

        ListView {
            id: taskList
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: popup.queue.length > 0
            clip: true
            model: popup.queue
            delegate: FluFrame {
                required property var modelData
                width: taskList.width
                implicitHeight: 86
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: 5
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        FluImage {
                            Layout.preferredWidth: 58
                            Layout.preferredHeight: 58
                            source: popup.taskValue(modelData, "preview", "")
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            RowLayout {
                                Layout.fillWidth: true
                                FluText {
                                    Layout.fillWidth: true
                                    text: String(popup.taskValue(modelData, "title", qsTr("创意工坊作品")))
                                    elide: Text.ElideRight
                                    font: FluTextStyle.BodyStrong
                                }
                                FluText {
                                    visible: popup.taskValue(modelData, "purpose", "") === "presetDependency"
                                    text: qsTr("基础壁纸")
                                    color: Qt.rgba(196 / 255, 121 / 255, 0, 1)
                                    font: FluTextStyle.Caption
                                }
                            }
                            FluProgressBar {
                                Layout.fillWidth: true
                                visible: ["downloading", "starting"].indexOf(popup.state(modelData)) >= 0
                                indeterminate: popup.state(modelData) === "starting"
                                from: 0
                                to: 1
                                value: popup.progress(modelData)
                            }
                            FluText {
                                Layout.fillWidth: true
                                text: {
                                    var state = popup.state(modelData);
                                    var message = String(popup.taskValue(modelData, "downloadMessage",
                                        popup.taskValue(modelData, "message", "")));
                                    if (message.length > 0)
                                        return message;
                                    if (state === "queued") return qsTr("等待 SteamCMD 按顺序下载…");
                                    if (state === "starting") return qsTr("正在启动 SteamCMD…");
                                    if (state === "downloading") return qsTr("正在下载 (%1%)").arg(Math.round(popup.progress(modelData) * 100));
                                    if (state === "validating") return qsTr("验证中...");
                                    if (state === "completed") return qsTr("已完成");
                                    if (state === "cancelled") return qsTr("已取消");
                                    if (state === "failed") return qsTr("失败");
                                    return state;
                                }
                                elide: Text.ElideRight
                                color: popup.state(modelData) === "failed"
                                    ? Qt.rgba(196 / 255, 43 / 255, 28 / 255, 1)
                                    : FluTheme.fontSecondaryColor
                                font: FluTextStyle.Caption
                            }
                        }
                        FluIconButton {
                            iconSource: {
                                var state = popup.state(modelData);
                                if (state === "failed") return FluentIcons.Refresh;
                                if (state === "completed") return FluentIcons.FolderOpen;
                                return FluentIcons.Cancel;
                            }
                            text: {
                                var state = popup.state(modelData);
                                if (state === "failed") return qsTr("重试");
                                if (state === "completed") return qsTr("打开下载目录");
                                return qsTr("取消下载");
                            }
                            contentDescription: text
                            onClicked: {
                                var state = popup.state(modelData);
                                var id = popup.taskId(modelData);
                                if (state === "failed") {
                                    popup.invoke("retryWorkshopDownload", id);
                                } else if (state === "completed") {
                                    popup.invoke("revealWorkshopDownload", id);
                                } else {
                                    popup.invoke("cancelWorkshopDownload", id);
                                }
                            }
                        }
                    }
                }
            }
        }

        FluDivider { Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            FluText {
                text: qsTr("%1 下载中").arg(popup.value("activeDownloadCount", 0))
                color: FluTheme.fontSecondaryColor
                font: FluTextStyle.Caption
            }
            Item { Layout.fillWidth: true }
            FluText {
                text: qsTr("%1 已完成").arg(popup.queue.filter(function(task) { return popup.state(task) === "completed"; }).length)
                color: Qt.rgba(16 / 255, 124 / 255, 16 / 255, 1)
                font: FluTextStyle.Caption
            }
        }
    }
}
