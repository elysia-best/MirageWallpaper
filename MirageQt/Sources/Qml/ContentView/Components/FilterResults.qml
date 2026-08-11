import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI

// 已安装壁纸筛选侧栏：对应 macOS Components/FilterResults.swift。
// 筛选状态（approvedOnly/enabledTypes 等）全部由 host（窗口）持有，
// 组件只做读写转发，不保存自己的状态。
ScrollView {
    id: root
    required property var host
    clip: true

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 12

        FluText {
            text: "筛选"
            font: FluTextStyle.Subtitle
        }
        FluFilledButton {
            Layout.fillWidth: true
            text: "重置筛选"
            onClicked: root.host.resetFilters()
        }
        FluText {
            text: "仅显示"
            font: FluTextStyle.BodyStrong
        }
        FluToggleSwitch {
            text: "广受好评"
            checked: root.host.approvedOnly
            clickListener: function () {
                root.host.approvedOnly = !root.host.approvedOnly;
            }
        }
        FluToggleSwitch {
            text: "我的收藏"
            checked: root.host.favoritesOnly
            clickListener: function () {
                root.host.favoritesOnly = !root.host.favoritesOnly;
            }
        }
        FluToggleSwitch {
            text: "移动端兼容"
            checked: root.host.mobileOnly
            clickListener: function () {
                root.host.mobileOnly = !root.host.mobileOnly;
            }
        }
        FluToggleSwitch {
            text: "音频响应"
            checked: root.host.audioOnly
            clickListener: function () {
                root.host.audioOnly = !root.host.audioOnly;
            }
        }
        FluToggleSwitch {
            text: "可自定义"
            checked: root.host.customizableOnly
            clickListener: function () {
                root.host.customizableOnly = !root.host.customizableOnly;
            }
        }
        FluDivider {
            Layout.fillWidth: true
        }
        FluText {
            text: "类型"
            font: FluTextStyle.BodyStrong
        }
        Repeater {
            model: root.host.typeFilters
            delegate: FluToggleSwitch {
                required property var modelData
                text: modelData.label
                checked: root.host.isEnabled(root.host.enabledTypes, modelData.key)
                clickListener: function () {
                    root.host.setEnabled("enabledTypes", modelData.key, !checked);
                }
            }
        }
        FluDivider {
            Layout.fillWidth: true
        }
        FluText {
            text: "分级"
            font: FluTextStyle.BodyStrong
        }
        Repeater {
            model: root.host.ratingFilters
            delegate: FluToggleSwitch {
                required property var modelData
                text: modelData.label
                checked: root.host.isEnabled(root.host.enabledRatings, modelData.key)
                clickListener: function () {
                    root.host.setEnabled("enabledRatings", modelData.key, !checked);
                }
            }
        }
        FluDivider {
            Layout.fillWidth: true
        }
        FluText {
            text: "来源"
            font: FluTextStyle.BodyStrong
        }
        Repeater {
            model: root.host.sourceFilters
            delegate: FluToggleSwitch {
                required property var modelData
                text: modelData.label
                checked: root.host.isEnabled(root.host.enabledSources, modelData.key)
                clickListener: function () {
                    root.host.setEnabled("enabledSources", modelData.key, !checked);
                }
            }
        }
        FluDivider {
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            FluText {
                text: "标签"
                font: FluTextStyle.BodyStrong
            }
            Item {
                Layout.fillWidth: true
            }
            FluTextButton {
                text: "全选"
                onClicked: {
                    root.host.enabledTags = root.host.tagFilters.map(function (filter) {
                        return filter.key;
                    });
                }
            }
            FluTextButton {
                text: "清空"
                onClicked: root.host.enabledTags = []
            }
        }
        Repeater {
            model: root.host.tagFilters
            delegate: FluToggleSwitch {
                required property var modelData
                text: modelData.label
                checked: root.host.isEnabled(root.host.enabledTags, modelData.key)
                clickListener: function () {
                    root.host.setEnabled("enabledTags", modelData.key, !checked);
                }
            }
        }
    }
}
