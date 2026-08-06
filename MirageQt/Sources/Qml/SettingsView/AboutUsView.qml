import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    required property var host

                        spacing: 12
                        FluText {
                            text: "Mirage"
                            font: FluTextStyle.Title
                        }
                        FluText {
                            text: "版本 1.0.0"
                        }
                        FluText {
                            Layout.fillWidth: true
                            text: "Wallpaper Engine 壁纸管理与 Linux 渲染控制。"
                            wrapMode: Text.WordWrap
                        }
                    }
