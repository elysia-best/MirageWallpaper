import QtQuick
import QtQuick.Layouts
import FluentUI

RowLayout {
    id: topBar
    property int currentTab: 0
    property string searchText: ""
    property int explorerIconSize: 170
    property int wallpapersPerPage: 25
    property string sortMode: "name"
    property bool sortDescending: false
    property var host
    property alias searchBox: searchInput
    signal filterRequested
    signal refreshRequested
    signal sortChanged(string key)
    signal pageSizeChanged(int count)
    signal iconSizeChanged(int size)
    Layout.fillWidth: true
    spacing: 8

    FluTextBox {
        id: searchInput
        Layout.preferredWidth: topBar.currentTab === 0 ? 160 : 260
        placeholderText: topBar.currentTab === 0 ? "搜索已安装的壁纸" : "搜索创意工坊"
        iconSource: FluentIcons.Search
        text: topBar.searchText
        onTextChanged: {
            topBar.searchText = text;
            if (topBar.host) {
                if (topBar.currentTab === 0)
                    topBar.host.searchText = text;
                if (topBar.currentTab === 2) {
                    topBar.host.workshopSearchText = text;
                    mirage.setWorkshopSearchText(text);
                }
            }
        }
    }
    FluFilledButton {
        text: "筛选"
        onClicked: topBar.filterRequested()
    }
    FluIconButton {
        text: "刷新"
        iconSource: FluentIcons.Refresh
        onClicked: topBar.refreshRequested()
    }
    FluDropDownButton {
        visible: topBar.currentTab === 0
        text: "视图"
        FluMenuItem {
            text: "小图标"
            onTriggered: topBar.iconSizeChanged(140)
        }
        FluMenuItem {
            text: "中图标"
            onTriggered: topBar.iconSizeChanged(170)
        }
        FluMenuItem {
            text: "大图标"
            onTriggered: topBar.iconSizeChanged(200)
        }
        FluMenuSeparator {}
        FluMenuItem {
            text: "每页 10 个"
            onTriggered: topBar.pageSizeChanged(10)
        }
        FluMenuItem {
            text: "每页 25 个"
            onTriggered: topBar.pageSizeChanged(25)
        }
        FluMenuItem {
            text: "每页 50 个"
            onTriggered: topBar.pageSizeChanged(50)
        }
    }
    Item {
        Layout.fillWidth: true
    }
    FluIconButton {
        visible: topBar.currentTab === 0
        text: topBar.sortDescending ? "降序" : "升序"
        iconSource: FluentIcons.Sort
        onClicked: topBar.sortChanged("direction")
    }
    FluComboBox {
        Layout.preferredWidth: 120
        model: topBar.currentTab === 0 ? ["名称", "评分", "文件大小"] : ["热门趋势", "最新发布", "订阅最多", "评分最高", "最多投票", "最近更新"]
        onActivated: topBar.sortChanged(currentText)
    }
}
