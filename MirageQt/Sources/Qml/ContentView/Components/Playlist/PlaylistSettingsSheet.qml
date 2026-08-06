import QtQuick
import QtQuick.Layouts
import FluentUI
import "../../../GlobalComponents"

MirageDialogWindow {
    property var settings: ({})
    title: "播放列表设置"
    positiveText: "好"
    negativeText: "取消"
    contentDelegate: ColumnLayout {
        FluText {
            text: "播放顺序"
            font: FluTextStyle.BodyStrong
        }
        FluComboBox {
            Layout.fillWidth: true
            model: ["有序", "随机"]
        }
        FluText {
            text: "更换壁纸"
            font: FluTextStyle.BodyStrong
        }
        FluComboBox {
            Layout.fillWidth: true
            model: ["计时器", "登录时", "当日时间", "星期", "从不"]
        }
        FluToggleSwitch {
            text: "总是从第一张壁纸开始"
        }
        FluToggleSwitch {
            text: "第一张壁纸仅在启动时播放"
        }
        FluToggleSwitch {
            text: "在视频结束时更换壁纸"
        }
        FluToggleSwitch {
            text: "允许壁纸在暂停时更换"
        }
    }
}
