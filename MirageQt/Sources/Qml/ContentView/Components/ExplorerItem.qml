import QtQuick
import QtQuick.Layouts
import FluentUI

FluFrame {
    id: item
    property var wallpaper
    property bool selected: false
    signal clicked
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        Image {
            Layout.fillWidth: true
            Layout.fillHeight: true
            source: item.wallpaper.preview
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }
        FluText {
            Layout.fillWidth: true
            text: item.wallpaper.title
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
    }
    MouseArea {
        anchors.fill: parent
        onClicked: item.clicked()
    }
}
