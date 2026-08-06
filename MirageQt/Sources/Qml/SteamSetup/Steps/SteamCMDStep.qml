import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    spacing: 12
    FluText {
        text: "SteamCMD"
        font: FluTextStyle.Title
    }
    FluText {
        Layout.fillWidth: true
        text: mirage.steamCMDPath || "尚未检测到 SteamCMD"
        wrapMode: Text.WordWrap
    }
    FluProgressBar {
        Layout.fillWidth: true
        visible: mirage.steamInstallState === "downloading"
        value: mirage.steamInstallProgress
    }
    FluText {
        Layout.fillWidth: true
        text: mirage.steamInstallMessage
        wrapMode: Text.WordWrap
    }
    RowLayout {
        FluButton {
            text: "检测"
            onClicked: mirage.detectSteamCMD()
        }
        FluFilledButton {
            text: "安装 SteamCMD"
            enabled: mirage.steamInstallState === "notFound" || mirage.steamInstallState === "failed"
            onClicked: mirage.installSteamCMD()
        }
        FluButton {
            text: "取消"
            visible: mirage.steamInstallState === "downloading"
            onClicked: mirage.cancelSteamCMDInstallation()
        }
    }
}
