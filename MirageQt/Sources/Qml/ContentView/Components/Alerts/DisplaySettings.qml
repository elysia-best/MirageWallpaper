import QtQuick
import QtQuick.Layouts
import FluentUI
import "../../../GlobalComponents"

MirageDialogWindow {
    id: dialog
    property var displays: []
    property string currentTitle: "未选择"
    title: "显示器"
    positiveText: "全部停止"
    contentDelegate: ColumnLayout {
        FluText {
            Layout.fillWidth: true
            text: "将当前壁纸“" + dialog.currentTitle + "”指派到指定显示器。"
            wrapMode: Text.WordWrap
        }
        Repeater {
            model: dialog.displays
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                FluIcon {
                    iconSource: FluentIcons.TVMonitor
                    iconSize: 28
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    FluText {
                        text: modelData.name
                    }
                    FluText {
                        text: modelData.width + " × " + modelData.height
                    }
                    FluText {
                        text: modelData.wallpaperTitle || "未渲染壁纸"
                    }
                }
                FluButton {
                    text: "应用到此屏"
                }
                FluButton {
                    visible: modelData.running
                    text: "停止"
                }
            }
        }
    }
}
