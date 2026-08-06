import QtQuick
import QtQuick.Layouts
import FluentUI

RowLayout {
    property string searchText: ""
    signal submitted(string text)
    FluTextBox {
        Layout.fillWidth: true
        placeholderText: "搜索作品、作者或作品 ID..."
        text: parent.searchText
        onTextChanged: parent.searchText = text
        onCommit: parent.submitted(text)
    }
    FluComboBox {
        Layout.preferredWidth: 140
        model: ["热门趋势", "最新发布", "订阅最多", "评分最高", "最多投票", "最近更新"]
    }
}
