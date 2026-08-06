import QtQuick
import QtQuick.Layouts
import FluentUI

RowLayout {
    id: controls
    property int currentPage: 1
    property int pageCount: 1
    signal selected(int page)
    spacing: 6
    FluIconButton {
        text: "上一页"
        iconSource: FluentIcons.ChevronLeft
        enabled: controls.currentPage > 1
        onClicked: controls.selected(controls.currentPage - 1)
    }
    FluText {
        text: controls.currentPage + " / " + controls.pageCount
        font: FluTextStyle.BodyStrong
    }
    FluIconButton {
        text: "下一页"
        iconSource: FluentIcons.ChevronRight
        enabled: controls.currentPage < controls.pageCount
        onClicked: controls.selected(controls.currentPage + 1)
    }
}
