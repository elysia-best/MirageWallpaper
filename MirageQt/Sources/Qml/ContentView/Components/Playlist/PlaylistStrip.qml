import QtQuick
import QtQuick.Layouts
import FluentUI

ListView {
    id: strip
    property var items: []
    property string selectedId: ""
    signal activated(string id)
    orientation: ListView.Horizontal
    spacing: 8
    model: strip.items
    delegate: FluFrame {
        required property var modelData
        width: 112
        height: strip.height
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 4
            Image {
                Layout.fillWidth: true
                Layout.fillHeight: true
                source: modelData.preview
                fillMode: Image.PreserveAspectCrop
            }
            FluText {
                Layout.fillWidth: true
                text: modelData.title
                elide: Text.ElideRight
            }
        }
        MouseArea {
            anchors.fill: parent
            onDoubleClicked: strip.activated(modelData.id)
        }
    }
}
