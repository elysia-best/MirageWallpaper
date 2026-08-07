import QtQuick
import QtQuick.Layouts
import FluentUI
import "../GlobalComponents"

MirageDialogWindow {
    id: root
    required property var host

    title: "设置"
    width: 780
    height: 620
    positiveText: "保存"
    closeOnPositive: false

    onOpened: root.host.resetSettingsDraft()
    onNegativeClicked: root.host.cancelSettingsDraft()
    onPositiveClicked: {
        if (mirage.applySettings(root.host.settingsDraft)) {
            root.host.settingsDirty = false;
            root.close();
        }
    }
    onClosing: {
        if (root.host.settingsDirty)
            root.host.cancelSettingsDraft();
    }

    contentDelegate: Component {
        ColumnLayout {
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                FluText {
                    text: "设置"
                    font: FluTextStyle.Title
                }
                Item {
                    Layout.fillWidth: true
                }
                FluText {
                    visible: root.host.settingsDirty
                    text: "已修改"
                    color: FluTheme.fontSecondaryColor
                }
            }

            FluDivider {
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                Repeater {
                    model: ["性能", "通用", "插件", "屏保", "关于"]
                    delegate: FluToggleButton {
                        required property string modelData
                        required property int index
                        text: modelData
                        checked: root.host.settingsPage === index
                        clickListener: function () {
                            root.host.settingsPage = index;
                        }
                    }
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: root.host.settingsPage

                PerformancePage {
                    host: root.host
                }

                GeneralPage {
                    host: root.host
                }

                PluginsPage {
                    host: root.host
                }

                ScreenSaverPage {
                    host: root.host
                }

                AboutUsView {
                    host: root.host
                }
            }

        }
    }
}
