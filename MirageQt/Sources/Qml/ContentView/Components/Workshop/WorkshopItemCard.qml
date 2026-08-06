import QtQuick
import QtQuick.Layouts
import FluentUI

Item {
    id: root
    required property var host
    required property var itemData
    property bool compact: false
    implicitWidth: compact ? 164 : 194
    implicitHeight: compact ? 204 : 234
    width: implicitWidth
    height: implicitHeight

    FluFrame {
        anchors.fill: parent
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 4
            Image {
                Layout.fillWidth: true
                Layout.preferredHeight: root.compact ? 124 : 146
                source: root.itemData.preview
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
            FluText {
                Layout.fillWidth: true
                text: root.itemData.title
                elide: Text.ElideRight
                font: FluTextStyle.BodyStrong
            }
            FluText {
                Layout.fillWidth: true
                visible: !root.compact
                text: root.itemData.typeLabel + " · " + root.itemData.subscriptions + " 订阅"
                elide: Text.ElideRight
            }
            FluProgressBar {
                Layout.fillWidth: true
                visible: root.itemData.downloadActive
                indeterminate: root.itemData.downloadProgress < 0
                value: Math.max(0, root.itemData.downloadProgress)
            }
            FluText {
                Layout.fillWidth: true
                visible: root.itemData.downloaded || root.itemData.downloadState.length > 0
                text: root.itemData.downloaded ? "已下载" : root.itemData.downloadMessage
                elide: Text.ElideRight
            }
        }
        FluFocusRectangle {
            anchors.fill: parent
            visible: root.itemData.id === mirage.selectedWorkshopItem.id
            radius: 4
        }
    }
    MouseArea {
        anchors.fill: parent
        onClicked: mirage.selectWorkshopItem(root.itemData.id)
        onDoubleClicked: mirage.downloadWorkshopItem(root.itemData.id)
    }
}
