import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    property var values: ({})
    spacing: 14
    FluText {
        text: "启动"
        font: FluTextStyle.Subtitle
    }
    FluToggleSwitch {
        text: "开机时自动启动 Mirage"
        checked: !!values.autoStart
    }
    FluDivider {
        Layout.fillWidth: true
    }
    FluText {
        text: "更新"
        font: FluTextStyle.Subtitle
    }
    FluToggleSwitch {
        text: "自动检查并下载更新（TODO：Linux 更新服务）"
        enabled: false
    }
    FluToggleSwitch {
        text: "接收测试版更新（TODO：Linux 更新服务）"
        enabled: false
    }
    FluDivider {
        Layout.fillWidth: true
    }
    FluText {
        text: "语言与外观"
        font: FluTextStyle.Subtitle
    }
    RowLayout {
        Layout.fillWidth: true
        FluText {
            text: "语言"
        }
        FluComboBox {
            Layout.fillWidth: true
            model: ["跟随系统", "简体中文", "繁體中文", "English"]
        }
    }
    RowLayout {
        Layout.fillWidth: true
        FluText {
            text: "外观"
        }
        FluComboBox {
            Layout.fillWidth: true
            model: ["跟随系统", "浅色", "深色"]
        }
    }
    FluDivider {
        Layout.fillWidth: true
    }
    FluText {
        text: "Steam 与路径"
        font: FluTextStyle.Subtitle
    }
    FluTextBox {
        Layout.fillWidth: true
        placeholderText: "Steam Web API Key（32 位十六进制）"
        text: values.steamAPIKey || ""
    }
    FluTextBox {
        Layout.fillWidth: true
        placeholderText: "自定义创意工坊目录"
        text: values.customWorkshopDirectory || ""
    }
    FluTextBox {
        Layout.fillWidth: true
        placeholderText: "自定义导入目录"
        text: values.customImportedDirectory || ""
    }
    FluText {
        Layout.fillWidth: true
        text: "桌面壁纸覆盖（TODO：Linux 桌面覆盖服务）"
        color: FluTheme.fontSecondaryColor
        wrapMode: Text.WordWrap
    }
}
