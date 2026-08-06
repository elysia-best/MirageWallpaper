import QtQuick
import QtQuick.Layouts
import FluentUI

Flow {
    property var tags: []
    property var selectedTags: []
    signal toggled(string tag)
    spacing: 6
    Repeater {
        model: parent.tags
        delegate: FluToggleButton {
            required property var modelData
            text: modelData.label || modelData
            checked: parent.parent.selectedTags.indexOf(modelData.key || modelData) >= 0
            clickListener: function () {
                parent.parent.toggled(modelData.key || modelData);
            }
        }
    }
}
