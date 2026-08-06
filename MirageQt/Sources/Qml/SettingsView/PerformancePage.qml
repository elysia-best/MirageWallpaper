import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    property var values: ({})
    spacing: 14
    FluText {
        text: "播放规则"
        font: FluTextStyle.Subtitle
    }
    Repeater {
        model: ["其他应用获得焦点时", "其他应用全屏时", "其他应用播放音频时", "显示器睡眠时", "笔记本使用电池时"]
        delegate: RowLayout {
            required property string modelData
            Layout.fillWidth: true
            FluText {
                Layout.fillWidth: true
                text: modelData
            }
            FluComboBox {
                Layout.preferredWidth: 150
                model: ["保持运行", "静音", "暂停", "停止（释放内存）"]
            }
        }
    }
    FluDivider {
        Layout.fillWidth: true
    }
    FluText {
        text: "渲染质量"
        font: FluTextStyle.Subtitle
    }
    RowLayout {
        Layout.fillWidth: true
        Repeater {
            model: ["低", "中", "高", "极致"]
            delegate: FluButton {
                required property string modelData
                Layout.fillWidth: true
                text: modelData
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        FluText {
            text: "抗锯齿"
        }
        FluComboBox {
            Layout.fillWidth: true
            model: ["关闭", "MSAA ×2", "MSAA ×4", "MSAA ×8"]
        }
    }
    RowLayout {
        Layout.fillWidth: true
        FluText {
            text: "帧率"
        }
        FluSlider {
            Layout.fillWidth: true
            from: 10
            to: 120
            value: values.fps || 30
        }
        FluText {
            text: String(Math.round(values.fps || 30))
        }
    }
    FluToggleSwitch {
        text: "启用音频频谱（场景与网页壁纸）"
    }
}
