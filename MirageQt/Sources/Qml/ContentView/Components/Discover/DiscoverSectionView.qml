import QtQuick
import QtQuick.Layouts
import FluentUI
import "../Workshop"

ColumnLayout {
    id: root
    required property var host
    required property var section
    width: parent ? parent.width : 0
    spacing: 6

    FluText {
        text: root.section.title
        font: FluTextStyle.Subtitle
    }
    ListView {
        Layout.fillWidth: true
        Layout.preferredWidth: Math.max(0, root.width)
        Layout.preferredHeight: 218
        clip: true
        orientation: ListView.Horizontal
        spacing: 10
        model: root.host.filterDiscoverItems(root.section.items)
        delegate: WorkshopItemCard {
            required property var modelData
            host: root.host
            itemData: modelData
            compact: true
        }
    }
}
