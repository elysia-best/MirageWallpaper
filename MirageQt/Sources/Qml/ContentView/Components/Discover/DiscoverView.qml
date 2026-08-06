import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI

Item {
    id: root
    required property var host

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            FluFilledButton {
                text: "筛选"
                onClicked: root.host.filtersVisible = !root.host.filtersVisible
            }
            FluText { text: "趋势范围" }
            FluComboBox {
                model: ["今日", "本周", "本月", "三个月", "半年", "一年"]
                currentIndex: [1, 7, 30, 90, 180, 365].indexOf(root.host.discoverTrendDays)
                onActivated: {
                    root.host.discoverTrendDays = [1, 7, 30, 90, 180, 365][currentIndex];
                    mirage.setDiscoverTrendDays(root.host.discoverTrendDays);
                }
            }
            Item { Layout.fillWidth: true }
            FluIconButton {
                text: "刷新发现"
                iconSource: FluentIcons.Refresh
                enabled: !mirage.discoverLoading
                onClicked: mirage.refreshDiscover()
            }
        }

        ScrollView {
            id: discoverScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: Math.max(0, availableWidth)
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                id: discoverColumn
                width: Math.max(0, discoverScroll.availableWidth)
                spacing: 18

                FluProgressRing {
                    Layout.alignment: Qt.AlignHCenter
                    visible: mirage.discoverLoading && mirage.discoverSections.length === 0
                    indeterminate: true
                }
                FluText {
                    Layout.alignment: Qt.AlignHCenter
                    visible: !mirage.discoverLoading && mirage.discoverSections.length === 0
                    text: "暂无发现内容"
                }
                Repeater {
                    model: mirage.discoverSections
                    delegate: DiscoverSectionView {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredWidth: discoverColumn.width
                        host: root.host
                        section: modelData
                    }
                }
            }
        }
    }
}
