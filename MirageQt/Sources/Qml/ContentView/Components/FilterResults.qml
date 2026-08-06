import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    id: filters
    property var host
    Layout.fillWidth: true
    FluFilledButton {
        Layout.fillWidth: true
        text: "重置筛选"
        onClicked: filters.host.resetFilters()
    }
    FluText {
        text: "类型"
        font: FluTextStyle.BodyStrong
    }
    Repeater {
        model: filters.host ? filters.host.typeFilters : []
        delegate: FluCheckBox {
            required property var modelData
            text: modelData.label
            checked: filters.host.isEnabled(filters.host.enabledTypes, modelData.key)
            clickListener: function () {
                filters.host.setEnabled("enabledTypes", modelData.key, !checked);
            }
        }
    }
}
