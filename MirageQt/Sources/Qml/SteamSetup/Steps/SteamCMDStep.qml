import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    id: root
    spacing: 14

    property string state: mirage.steamInstallState
    property double progress: mirage.steamInstallProgress
    property string path: mirage.steamCMDPath
    property string message: mirage.steamInstallMessage
    property bool busy: ["detecting", "downloading", "extracting", "initializing"].indexOf(state) >= 0

    function invoke(name) {
        var fn = mirage[name];
        if (typeof fn !== "function")
            return false;
        fn.apply(mirage, Array.prototype.slice.call(arguments, 1));
        return true;
    }

    FluText {
        text: qsTr("SteamCMD")
        font: FluTextStyle.Title
    }

    FluText {
        Layout.fillWidth: true
        text: root.state === "found" || root.state === "installed"
            ? qsTr("SteamCMD 已准备就绪") : qsTr("需要 SteamCMD 才能下载创意工坊文件。")
        wrapMode: Text.WordWrap
        color: FluTheme.fontSecondaryColor
    }

    FluFrame {
        Layout.fillWidth: true
        Layout.preferredHeight: 112
        visible: root.state === "detecting"
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            FluProgressRing {
                Layout.alignment: Qt.AlignHCenter
                indeterminate: true
            }
            FluText {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("正在检测 SteamCMD...")
                color: FluTheme.fontSecondaryColor
            }
        }
    }

    FluFrame {
        Layout.fillWidth: true
        Layout.preferredHeight: steamReadyContent.implicitHeight + 24
        visible: root.state === "found" || root.state === "installed"
        RowLayout {
            id: steamReadyContent
            anchors.fill: parent
            anchors.margins: 12
            spacing: 10
            FluIcon {
                iconSource: FluentIcons.CheckMark
                iconSize: 24
                iconColor: Qt.rgba(16 / 255, 124 / 255, 16 / 255, 1)
            }
            ColumnLayout {
                Layout.fillWidth: true
                FluText {
                    text: root.state === "installed" ? qsTr("安装完成") : qsTr("已找到 SteamCMD")
                    font: FluTextStyle.BodyStrong
                }
                FluCopyableText {
                    Layout.fillWidth: true
                    text: root.path
                    visible: root.path.length > 0
                    font: FluTextStyle.Caption
                    wrapMode: Text.WrapAnywhere
                }
            }
        }
    }

    FluFrame {
        Layout.fillWidth: true
        Layout.preferredHeight: steamMissingContent.implicitHeight + 28
        visible: root.state === "notFound"
        ColumnLayout {
            id: steamMissingContent
            anchors.fill: parent
            anchors.margins: 14
            spacing: 8
            FluIcon {
                Layout.alignment: Qt.AlignHCenter
                iconSource: FluentIcons.Warning
                iconSize: 32
                iconColor: Qt.rgba(196 / 255, 121 / 255, 0, 1)
            }
            FluText {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("未找到 SteamCMD")
                font: FluTextStyle.Subtitle
            }
            FluText {
                Layout.fillWidth: true
                text: qsTr("SteamCMD 是 Valve 的官方命令行工具，用于下载 Steam 创意工坊内容。Mirage 会下载官方 bootstrap，随后由 SteamCMD 完成首次更新；所需时间和空间取决于网络。")
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                color: FluTheme.fontSecondaryColor
            }
        }
    }

    FluFrame {
        Layout.fillWidth: true
        Layout.preferredHeight: steamProgressContent.implicitHeight + 28
        visible: root.state === "downloading" || root.state === "extracting" || root.state === "initializing"
        ColumnLayout {
            id: steamProgressContent
            anchors.fill: parent
            anchors.margins: 14
            spacing: 9
            FluProgressBar {
                Layout.fillWidth: true
                indeterminate: root.state !== "downloading"
                from: 0
                to: 1
                value: Math.max(0, Math.min(1, root.progress))
                progressVisible: root.state === "downloading"
            }
            FluText {
                Layout.alignment: Qt.AlignHCenter
                text: root.state === "downloading"
                    ? qsTr("正在下载 SteamCMD (%1%)").arg(Math.round(root.progress * 100))
                    : root.state === "extracting"
                        ? qsTr("正在解压...")
                        : qsTr("正在完成 SteamCMD 首次初始化...")
                color: FluTheme.fontSecondaryColor
            }
            FluText {
                Layout.fillWidth: true
                visible: root.message.length > 0
                text: root.message
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                color: FluTheme.fontTertiaryColor
            }
        }
    }

    FluFrame {
        Layout.fillWidth: true
        Layout.preferredHeight: steamFailureContent.implicitHeight + 28
        visible: root.state === "failed"
        ColumnLayout {
            id: steamFailureContent
            anchors.fill: parent
            anchors.margins: 14
            spacing: 8
            FluIcon {
                Layout.alignment: Qt.AlignHCenter
                iconSource: FluentIcons.ErrorBadge
                iconSize: 30
                iconColor: Qt.rgba(196 / 255, 43 / 255, 28 / 255, 1)
            }
            FluText {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("SteamCMD 安装失败")
                font: FluTextStyle.Subtitle
            }
            FluText {
                Layout.fillWidth: true
                text: root.message.length > 0 ? root.message : qsTr("SteamCMD bootstrap 安装失败。")
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                color: Qt.rgba(196 / 255, 43 / 255, 28 / 255, 1)
            }
        }
    }

    FluText {
        Layout.fillWidth: true
        visible: root.state !== "found" && root.state !== "installed"
            && root.state !== "notFound" && root.state !== "failed"
            && root.message.length > 0
        text: root.message
        wrapMode: Text.WordWrap
        color: FluTheme.fontSecondaryColor
    }

    RowLayout {
        Layout.fillWidth: true
        FluButton {
            text: qsTr("重新检测")
            enabled: !root.busy
            onClicked: root.invoke("detectSteamCMD")
        }
        FluFilledButton {
            text: root.state === "failed" ? qsTr("重试") : qsTr("安装 SteamCMD")
            visible: root.state === "notFound" || root.state === "failed"
            onClicked: root.invoke("installSteamCMD")
        }
        FluButton {
            text: qsTr("取消")
            visible: root.busy
            onClicked: root.invoke("cancelSteamCMDInstallation")
        }
    }
}
