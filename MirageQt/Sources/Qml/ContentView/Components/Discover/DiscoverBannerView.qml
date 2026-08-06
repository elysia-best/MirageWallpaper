import QtQuick
import QtQuick.Layouts
import FluentUI

FluFrame {
    property var itemData: ({})
    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        Image {
            Layout.preferredWidth: 220
            Layout.preferredHeight: 120
            source: parent.parent.itemData.preview
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }
        ColumnLayout {
            Layout.fillWidth: true
            FluText {
                text: parent.parent.itemData.title || "Mirage"
                font: FluTextStyle.Subtitle
            }
            FluText {
                Layout.fillWidth: true
                text: parent.parent.itemData.description || ""
                wrapMode: Text.WordWrap
            }
        }
    }
}
