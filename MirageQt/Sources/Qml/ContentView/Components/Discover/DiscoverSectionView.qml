import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    property string title: ""
    property var items: []
    Layout.fillWidth: true
    FluText {
        text: parent.title
        font: FluTextStyle.Subtitle
    }
    ListView {
        Layout.fillWidth: true
        Layout.preferredHeight: 218
        orientation: ListView.Horizontal
        spacing: 10
        model: parent.items
        delegate: FluFrame {
            required property var modelData
            width: 164
            height: 204
            FluText {
                anchors.centerIn: parent
                text: modelData.title
                elide: Text.ElideRight
            }
        }
    }
}
