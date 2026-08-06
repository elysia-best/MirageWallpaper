import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    id: root
    required property var host
    spacing: 10

    FluText {
        text: "创意工坊筛选"
        font: FluTextStyle.Subtitle
    }
    FluFilledButton {
        Layout.fillWidth: true
        text: "重置筛选"
        onClicked: root.host.resetWorkshopFilters()
    }
    FluText {
        text: "类型"
        font: FluTextStyle.BodyStrong
    }
    Repeater {
        model: root.host.workshopTypeFilters
        delegate: FluCheckBox {
            required property var modelData
            text: modelData.label
            checked: root.host.workshopType === modelData.key
            clickListener: function () {
                if (checked) {
                    root.host.workshopType = modelData.key;
                    mirage.setWorkshopTypeFilter(modelData.key);
                }
            }
        }
    }
    FluDivider { Layout.fillWidth: true }
    FluText {
        text: "分级"
        font: FluTextStyle.BodyStrong
    }
    Repeater {
        model: root.host.ratingFilters
        delegate: FluCheckBox {
            required property var modelData
            text: modelData.label
            checked: root.host.isEnabled(root.host.workshopRatings, modelData.key)
            clickListener: function () {
                root.host.setWorkshopRating(modelData.key, checked);
            }
        }
    }
    FluDivider { Layout.fillWidth: true }
    FluText {
        text: "标签"
        font: FluTextStyle.BodyStrong
    }
    RowLayout {
        FluTextButton {
            text: "全选"
            onClicked: root.host.selectAllWorkshopTags()
        }
        FluTextButton {
            text: "清空"
            onClicked: root.host.clearWorkshopTags()
        }
    }
    Repeater {
        model: root.host.workshopTagFilters
        delegate: FluCheckBox {
            required property var modelData
            text: modelData.label
            checked: root.host.isEnabled(root.host.workshopSelectedTags, modelData.key)
            clickListener: function () {
                root.host.toggleWorkshopTag(modelData.key);
            }
        }
    }
}
