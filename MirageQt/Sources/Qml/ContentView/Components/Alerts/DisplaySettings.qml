import QtQuick
import QtQuick.Layouts
import FluentUI
import "../../../GlobalComponents"

// 显示器设置对话框：对应 macOS Alerts/DisplaySettings.swift。
// 把当前壁纸指派到指定显示器，或停止某屏/全部渲染。
MirageDialogWindow {
    id: root
    required property var host

    title: "显示器设置"
    width: 520
    height: 450
    neutralText: "完成"
    buttonFlags: FluContentDialogType.NeutralButton
    contentDelegate: Component {
        ColumnLayout {
            width: parent.width
            spacing: 10

            FluText {
                Layout.fillWidth: true
                text: "将当前壁纸「" + (mirage.selectedWallpaper.title || "未选择") + "」指派到指定显示器。"
                wrapMode: Text.WordWrap
            }
            ListView {
                id: displayList
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                implicitHeight: Math.min(320, Math.max(76, contentHeight))
                clip: true
                spacing: 8
                model: mirage.displays
                delegate: FluFrame {
                    required property var modelData
                    width: displayList.width
                    height: displayRow.implicitHeight + 16

                    RowLayout {
                        id: displayRow
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 10

                        FluIcon {
                            iconSource: FluentIcons.SettingsDisplaySound
                            iconSize: 24
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            FluText {
                                Layout.fillWidth: true
                                text: modelData.name + (modelData.primary ? " · 主显示器" : "")
                                elide: Text.ElideRight
                                font: FluTextStyle.BodyStrong
                            }
                            FluText {
                                Layout.fillWidth: true
                                text: modelData.width + " × " + modelData.height + (modelData.running ? " · 正在渲染：" + modelData.wallpaperTitle : " · 未渲染壁纸")
                                elide: Text.ElideRight
                            }
                        }
                        ColumnLayout {
                            spacing: 4
                            FluFilledButton {
                                text: "应用到此屏"
                                enabled: mirage.selectedWallpaperId.length > 0
                                onClicked: root.host.runWithWallpaperTrust(mirage.selectedWallpaper, function () {
                                    mirage.applySelectedToScreen(modelData.index);
                                })
                            }
                            FluButton {
                                text: "停止"
                                visible: modelData.running
                                onClicked: mirage.stopScreen(modelData.index)
                            }
                        }
                    }
                }
            }
            FluButton {
                Layout.alignment: Qt.AlignHCenter
                text: "全部停止"
                enabled: mirage.displays.some(function (display) {
                    return display.running;
                })
                onClicked: mirage.stopWallpapers()
            }
        }
    }
}
