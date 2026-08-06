import QtQuick
import QtQuick.Layouts
import FluentUI
import "../GlobalComponents"

MirageDialogWindow {
    id: dialog
    property bool hideUntilUpdate: false
    title: "欢迎使用 Mirage"
    positiveText: "开始使用"
    negativeText: ""
    contentDelegate: ColumnLayout {
        spacing: 14
        WelcomeFeature {
            title: "三类壁纸，一站渲染"
            detail: "支持 Wallpaper Engine 的场景、网页、视频三类壁纸。"
        }
        WelcomeFeature {
            title: "自动加载创意工坊壁纸"
            detail: "读取已订阅壁纸，也可导入本地文件。"
        }
        WelcomeFeature {
            title: "熟悉的界面布局"
            detail: "沿用 Wallpaper Engine 的布局，并针对 FluentUI 做响应式适配。"
        }
        WelcomeFeature {
            title: "实时属性调节"
            detail: "音量、速度和壁纸属性即时生效。"
        }
        FluToggleSwitch {
            text: "在下次更新前不再显示"
            checked: dialog.hideUntilUpdate
            clickListener: function () {
                dialog.hideUntilUpdate = !checked;
            }
        }
    }

    component WelcomeFeature: RowLayout {
        required property string title
        required property string detail
        spacing: 12
        FluIcon {
            iconSource: FluentIcons.Picture
            iconSize: 28
            iconColor: FluTheme.primaryColor
        }
        ColumnLayout {
            Layout.fillWidth: true
            FluText {
                text: parent.parent.title
                font: FluTextStyle.BodyStrong
            }
            FluText {
                Layout.fillWidth: true
                text: parent.parent.detail
                wrapMode: Text.WordWrap
            }
        }
    }
}
