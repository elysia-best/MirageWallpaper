import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    id: editor
    property var properties: []
    property var host
    Repeater {
        model: editor.properties
        delegate: ColumnLayout {
            required property var modelData
            Layout.fillWidth: true
            FluText {
                Layout.fillWidth: true
                text: modelData.text || modelData.key
                wrapMode: Text.WordWrap
            }
            FluTextBox {
                Layout.fillWidth: true
                visible: modelData.type === "textinput"
                text: String(modelData.value || "")
                onCommit: editor.host.setSelectedProperty(modelData.key, text)
            }
        }
    }
}
