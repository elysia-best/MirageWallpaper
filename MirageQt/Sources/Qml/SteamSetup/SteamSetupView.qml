import QtQuick
import QtQuick.Layouts
import FluentUI
import "Steps"

FluWindow {
    id: setup
    property int currentStep: 0
    property string username: ""
    property string password: ""
    property string guardCode: ""
    property bool canProceed: currentStep === 0
        || currentStep === 3
        || (currentStep === 1
            && (mirage.steamInstallState === "found"
                || mirage.steamInstallState === "installed"))
        || (currentStep === 2 && mirage.steamLoginState === "success")
    width: 560
    height: 640
    minimumWidth: 520
    minimumHeight: 560
    title: "Steam 设置"
    onVisibleChanged: {
        if (visible) mirage.detectSteamCMD()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        RowLayout {
            Layout.fillWidth: true
            Repeater {
                model: ["欢迎", "SteamCMD", "登录", "完成"]
                delegate: FluToggleButton {
                    required property string modelData
                    required property int index
                    Layout.fillWidth: true
                    text: (index + 1) + ". " + modelData
                    checked: setup.currentStep === index
                    enabled: index <= setup.currentStep
                    clickListener: function () {
                        setup.currentStep = index;
                    }
                }
            }
        }
        FluDivider {
            Layout.fillWidth: true
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: setup.currentStep
            ColumnLayout {
                Layout.fillWidth: true
                FluText {
                    text: "连接 Steam 以下载创意工坊壁纸"
                    font: FluTextStyle.Title
                }
                FluText {
                    Layout.fillWidth: true
                    text: "Mirage 将使用 SteamCMD 管理下载。"
                    wrapMode: Text.WordWrap
                }
            }
            SteamCMDStep {}
            SteamLoginStep {
                username: setup.username
                password: setup.password
                guardCode: setup.guardCode
            }
            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter
                FluIcon {
                    Layout.alignment: Qt.AlignHCenter
                    iconSource: FluentIcons.CheckMark
                    iconSize: 48
                }
                FluText {
                    text: "Steam 设置完成"
                    font: FluTextStyle.Title
                }
            }
        }
        FluDivider {
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            FluButton {
                visible: setup.currentStep > 0
                text: "上一步"
                onClicked: setup.currentStep -= 1
            }
            Item {
                Layout.fillWidth: true
            }
            FluFilledButton {
                text: setup.currentStep === 3 ? "完成" : "下一步"
                enabled: setup.canProceed
                onClicked: setup.currentStep < 3 ? setup.currentStep += 1 : setup.close()
            }
        }
    }
}
