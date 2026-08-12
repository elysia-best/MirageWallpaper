import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI

// 已安装壁纸筛选侧栏：对应 macOS Components/FilterResults.swift。
// 控件类型与 macOS 对齐：checkbox + 可折叠分组 + “仅显示”边框组。
// 筛选状态（approvedOnly/enabledTypes 等）全部由 host（窗口）持有，
// 组件只做读写转发，不保存自己的状态。
// 注意：FluCheckBox 的 clickListener 在 onToggled 触发，checked 已是新值，
// 因此这里直接使用 checked，而不是旧 ToggleSwitch 的 !checked。
ScrollView {
    id: root
    required property var host
    clip: true

    ColumnLayout {
        Layout.fillWidth: true
        // ScrollView 内容区是 Flickable，Layout.fillWidth 不生效，
        // 需显式占满视口宽度，否则侧栏会缩成内容隐式宽度，
        // 边框组等控件无法包住文字。
        width: root.availableWidth
        spacing: 12

        FluText {
            text: "筛选"
            font: FluTextStyle.Subtitle
        }

        FluIconButton {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("重置筛选")
            onClicked: root.host.resetFilters()
            iconSource: FluentIcons.Refresh
            iconSize: 14
            display: Button.TextBesideIcon
        }


        // “仅显示”边框组，对应 macOS 的描边框 + 复选框。
        // 宽度收缩到刚好包住内容，并整体在侧栏中水平居中。
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: showOnlyColumn.implicitWidth + 20
            Layout.preferredHeight: showOnlyColumn.implicitHeight + 20
            radius: 4
            color: "transparent"
            border.color: FluTheme.dark ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(0, 0, 0, 0.15)
            ColumnLayout {
                id: showOnlyColumn
                anchors.fill: parent
                anchors.margins: 10
                spacing: 2
                FluText {
                    text: "仅显示："
                    font: FluTextStyle.BodyStrong
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    FluIcon {
                        iconSource: FluentIcons.FavoriteStarFill
                        iconSize: 13
                        color: "#4CAF50"
                    }
                    FluCheckBox {
                        Layout.fillWidth: true
                        text: "广受好评"
                        checked: root.host.approvedOnly
                        clickListener: function () {
                            root.host.approvedOnly = checked;
                        }
                    }
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    FluIcon {
                        iconSource: FluentIcons.HeartFill
                        iconSize: 13
                        color: "#FF69B4"
                    }
                    FluCheckBox {
                        Layout.fillWidth: true
                        text: "我的收藏"
                        checked: root.host.favoritesOnly
                        clickListener: function () {
                            root.host.favoritesOnly = checked;
                        }
                    }
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    FluIcon {
                        iconSource: FluentIcons.MobileTablet
                        iconSize: 13
                        color: "#FF9800"
                    }
                    FluCheckBox {
                        Layout.fillWidth: true
                        text: "移动端兼容"
                        checked: root.host.mobileOnly
                        clickListener: function () {
                            root.host.mobileOnly = checked;
                        }
                    }
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    FluIcon {
                        iconSource: FluentIcons.Audio
                        iconSize: 13
                        color: FluTheme.primaryColor
                    }
                    FluCheckBox {
                        Layout.fillWidth: true
                        text: "音频响应"
                        checked: root.host.audioOnly
                        clickListener: function () {
                            root.host.audioOnly = checked;
                        }
                    }
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    FluIcon {
                        iconSource: FluentIcons.Edit
                        iconSize: 13
                        color: FluTheme.primaryColor
                    }
                    FluCheckBox {
                        Layout.fillWidth: true
                        text: "可自定义"
                        checked: root.host.customizableOnly
                        clickListener: function () {
                            root.host.customizableOnly = checked;
                        }
                    }
                }
            }
        }

        FilterSection {
            Layout.fillWidth: true
            title: "类型"
            Repeater {
                model: root.host.typeFilters
                delegate: FluCheckBox {
                    required property var modelData
                    width: parent.width
                    text: modelData.label
                    checked: root.host.isEnabled(root.host.enabledTypes, modelData.key)
                    clickListener: function () {
                        root.host.setEnabled("enabledTypes", modelData.key, checked);
                    }
                }
            }
        }

        FilterSection {
            Layout.fillWidth: true
            title: "分级"
            Repeater {
                model: root.host.ratingFilters
                delegate: FluCheckBox {
                    required property var modelData
                    width: parent.width
                    text: modelData.label
                    checked: root.host.isEnabled(root.host.enabledRatings, modelData.key)
                    clickListener: function () {
                        root.host.setEnabled("enabledRatings", modelData.key, checked);
                    }
                }
            }
        }

        FilterSection {
            Layout.fillWidth: true
            title: "来源"
            Repeater {
                model: root.host.sourceFilters
                delegate: FluCheckBox {
                    required property var modelData
                    width: parent.width
                    text: modelData.label
                    checked: root.host.isEnabled(root.host.enabledSources, modelData.key)
                    clickListener: function () {
                        root.host.setEnabled("enabledSources", modelData.key, checked);
                    }
                }
            }
        }

        FilterSection {
            Layout.fillWidth: true
            title: "标签"
            RowLayout {
                width: parent.width
                FluTextButton {
                    text: "全选"
                    onClicked: {
                        root.host.enabledTags = root.host.tagFilters.map(function (filter) {
                            return filter.key;
                        });
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                FluTextButton {
                    text: "清空"
                    onClicked: root.host.enabledTags = []
                }
            }
            Repeater {
                model: root.host.tagFilters
                delegate: FluCheckBox {
                    required property var modelData
                    width: parent.width
                    text: modelData.label
                    checked: root.host.isEnabled(root.host.enabledTags, modelData.key)
                    clickListener: function () {
                        root.host.setEnabled("enabledTags", modelData.key, checked);
                    }
                }
            }
        }
    }
}
