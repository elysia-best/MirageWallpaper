import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    property var types: []
    property var ratings: []
    property var tags: []
    signal resetRequested
    FluFilledButton {
        Layout.fillWidth: true
        text: "重置筛选"
        onClicked: resetRequested()
    }
    FluText {
        text: "类型"
        font: FluTextStyle.BodyStrong
    }
    Repeater {
        model: parent.types
        delegate: FluCheckBox {
            required property var modelData
            text: modelData.label
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
        model: parent.ratings
        delegate: FluCheckBox {
            required property var modelData
            text: modelData.label
        }
    }
    FluDivider {
        Layout.fillWidth: true
    }
    FluText {
        text: "标签"
        font: FluTextStyle.BodyStrong
    }
    Repeater {
        model: parent.tags
        delegate: FluCheckBox {
            required property var modelData
            text: modelData.label
        }
    }
}
