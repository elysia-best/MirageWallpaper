import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    spacing: 12
    FluText {
        Layout.alignment: Qt.AlignHCenter
        text: "Mirage"
        font: FluTextStyle.Title
    }
    FluText {
        Layout.alignment: Qt.AlignHCenter
        text: "Linux Qt 动态壁纸引擎"
    }
    FluText {
        Layout.alignment: Qt.AlignHCenter
        text: "版本 1.0.0"
        color: FluTheme.fontSecondaryColor
    }
    FluText {
        Layout.alignment: Qt.AlignHCenter
        text: "作者 王孝慈 (laobamac)"
    }
    FluTextButton {
        Layout.alignment: Qt.AlignHCenter
        text: "github.com/laobamac/MirageWallpaper"
        onClicked: Qt.openUrlExternally("https://github.com/laobamac/MirageWallpaper")
    }
    FluDivider {
        Layout.fillWidth: true
    }
    FluText {
        text: "支持 Mirage"
        font: FluTextStyle.Subtitle
    }
    FluText {
        Layout.fillWidth: true
        text: "Mirage 会继续免费开放开发。每一份支持都会用于持续维护与兼容性改进。"
        wrapMode: Text.WordWrap
    }
    RowLayout {
        Layout.fillWidth: true
        Image {
            Layout.preferredWidth: 118
            Layout.preferredHeight: 154
            source: "qrc:/sponsorship/afdian.jpg"
            fillMode: Image.PreserveAspectFit
        }
        Image {
            Layout.preferredWidth: 118
            Layout.preferredHeight: 154
            source: "qrc:/sponsorship/wechat-pay.png"
            fillMode: Image.PreserveAspectFit
        }
        Image {
            Layout.preferredWidth: 118
            Layout.preferredHeight: 154
            source: "qrc:/sponsorship/alipay.jpg"
            fillMode: Image.PreserveAspectFit
        }
        FluButton {
            text: "爱发电"
            onClicked: Qt.openUrlExternally("https://www.ifdian.net/a/laobamac")
        }
        FluButton {
            text: "微信支付"
        }
        FluButton {
            text: "支付宝"
        }
    }
    FluTextBox {
        Layout.fillWidth: true
        text: "0xFc0a5C52e3A085FEc7b077FE3D2C413114Bf880D"
        readOnly: true
    }
}
