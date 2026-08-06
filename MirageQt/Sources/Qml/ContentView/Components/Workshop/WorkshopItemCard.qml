import QtQuick
import QtQuick.Layouts
import FluentUI

FluFrame {
    id: card
    property var itemData: ({})
    signal selected
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        Image {
            Layout.fillWidth: true
            Layout.preferredHeight: 146
            source: card.itemData.preview
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }
        FluText {
            Layout.fillWidth: true
            text: card.itemData.title
            elide: Text.ElideRight
            font: FluTextStyle.BodyStrong
        }
        FluText {
            Layout.fillWidth: true
            text: card.itemData.typeLabel || ""
            elide: Text.ElideRight
        }
        FluProgressBar {
            Layout.fillWidth: true
            visible: card.itemData.downloadActive
            value: Math.max(0, card.itemData.downloadProgress || 0)
            indeterminate: (card.itemData.downloadProgress || -1) < 0
        }
    }
    MouseArea {
        anchors.fill: parent
        onClicked: card.selected()
    }
}
