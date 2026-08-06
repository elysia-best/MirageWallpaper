import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    property var items: []
    property bool loading: false
    property string errorText: ""
    signal selected(string id)
    FluProgressRing {
        Layout.alignment: Qt.AlignHCenter
        visible: parent.loading && parent.items.length === 0
        indeterminate: true
    }
    FluText {
        Layout.alignment: Qt.AlignHCenter
        visible: !parent.loading && parent.items.length === 0
        text: parent.errorText || "没有找到壁纸"
    }
    GridView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        model: parent.items
        cellWidth: 204
        cellHeight: 248
        delegate: WorkshopItemCard {
            required property var modelData
            width: 194
            height: 234
            itemData: modelData
            onSelected: parent.parent.selected(modelData.id)
        }
    }
}
