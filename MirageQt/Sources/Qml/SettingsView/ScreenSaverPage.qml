import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    required property var host

                        spacing: 12
                        FluText {
                            text: "屏保"
                            font: FluTextStyle.BodyStrong
                        }
                        FluText {
                            Layout.fillWidth: true
                            text: "TODO: 屏保集成尚未在 Linux Qt 版本实现。"
                            wrapMode: Text.WordWrap
                        }
                        FluButton {
                            text: "打开显示器设置"
                            onClicked: host.showLinuxNotice()
                        }
                    }
