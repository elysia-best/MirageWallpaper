import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    id: login
    property string username: ""
    property string password: ""
    property string guardCode: ""
    spacing: 10
    FluText {
        text: "Steam 登录"
        font: FluTextStyle.Title
    }
    FluText {
        Layout.fillWidth: true
        visible: mirage.steamLoginState === "success"
        text: "已登录 Steam：" + (mirage.steamUsername || login.username)
        color: FluTheme.primaryColor
    }
    FluTextBox {
        Layout.fillWidth: true
        visible: mirage.steamLoginState !== "success"
        placeholderText: "Steam 用户名"
        text: login.username
        onTextChanged: login.username = text
    }
    FluPasswordBox {
        Layout.fillWidth: true
        visible: mirage.steamLoginState !== "success"
        placeholderText: "密码"
        text: login.password
        onTextChanged: login.password = text
    }
    FluTextBox {
        Layout.fillWidth: true
        visible: mirage.steamLoginState === "waitingForGuard"
        placeholderText: "Steam Guard 验证码"
        text: login.guardCode
        onTextChanged: login.guardCode = text
    }
    FluText {
        Layout.fillWidth: true
        visible: mirage.steamLoginMessage.length > 0
        text: mirage.steamLoginMessage
        wrapMode: Text.WordWrap
    }
    FluFilledButton {
        visible: mirage.steamLoginState !== "success"
        text: mirage.steamLoginState === "waitingForGuard" ? "提交验证码" : "登录"
        onClicked: mirage.steamLoginState === "waitingForGuard"
            ? mirage.submitSteamGuardCode(login.guardCode)
            : mirage.loginSteam(login.username, login.password)
    }
    FluButton {
        visible: mirage.steamLoginState === "success"
        text: "退出登录"
        onClicked: mirage.logoutSteam()
    }
    FluButton {
        text: "取消登录"
        visible: mirage.steamLoginState === "loggingIn" || mirage.steamLoginState === "waitingForGuard"
        onClicked: mirage.cancelSteamLogin()
    }
    ScrollView {
        Layout.fillWidth: true
        Layout.preferredHeight: 120
        FluText {
            text: mirage.steamLoginLog.join("\n")
            wrapMode: Text.Wrap
        }
    }
}
