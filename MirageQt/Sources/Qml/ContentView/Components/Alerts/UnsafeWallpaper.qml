import QtQuick
import QtQuick.Layouts
import FluentUI
import "../../../GlobalComponents"

MirageDialogWindow {
    id: dialog
    property string wallpaperType: "网页"
    property string wallpaperPath: ""
    property int seconds: 5
    title: "正在打开" + dialog.wallpaperType + "类壁纸"
    positiveText: dialog.seconds > 0 ? "请等待 " + dialog.seconds + " 秒" : "继续"
    negativeText: "取消"
    contentDelegate: ColumnLayout {
        FluText {
            Layout.fillWidth: true
            text: "你即将把以下文件作为壁纸运行：\n" + dialog.wallpaperPath
            wrapMode: Text.WordWrap
        }
        FluText {
            Layout.fillWidth: true
            text: dialog.seconds > 0 ? "请等待倒计时结束。" : "请注意潜在的恶意代码风险。"
            wrapMode: Text.WordWrap
        }
        FluToggleSwitch {
            text: "对此壁纸不再提示"
        }
    }
}
