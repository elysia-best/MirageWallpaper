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
        // 14px 间隔对齐 macOS DiscoverSectionView 的 LazyHStack(spacing: 14)，
        // 与各网格分区的卡片间隔统一。
        spacing: 14
        model: root.host.filterDiscoverItems(root.section.items)
        delegate: WorkshopItemCard {
            required property var modelData
            host: root.host
            itemData: modelData
            compact: true
        }
    }
}
