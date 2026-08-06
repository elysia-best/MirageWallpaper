import QtQuick
import QtQuick.Layouts
import FluentUI

Item {
    id: explorer
    property var model
    property int iconSize: 170
    property string selectedId: ""
    signal selected(string id)
    GridView {
        anchors.fill: parent
        clip: true
        model: explorer.model
        cellWidth: explorer.iconSize + 16
        cellHeight: explorer.iconSize + 48
        delegate: FluFrame {
            required property var modelData
            width: explorer.iconSize
            height: explorer.iconSize + 32
            ColumnLayout {
                anchors.fill: parent
                Image {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    source: modelData.preview
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
                FluText {
                    Layout.fillWidth: true
                    text: modelData.title
                    elide: Text.ElideRight
                }
            }
            MouseArea {
                anchors.fill: parent
                onClicked: explorer.selected(modelData.id)
            }
        }
    }
}
