import QtQuick
import QtQuick.Layouts
import FluentUI
import "../../GlobalComponents"

// 已安装壁纸卡片：继承 WallpaperItemCard 公共基类。悬停放大、选中态、
// 列表不播 GIF（基类 WorkshopImage 固定 isAnimating: false）、图片填满
// 剩余空间 + 底部标题均来自基类；本文件只定义已安装语义的回调：
// 左键 selectWallpaper、右键打开上下文菜单、双击应用壁纸（网页壁纸
// 走 host.runWithWallpaperTrust 信任流程）。
WallpaperItemCard {
    id: root

    // 选中判定：已安装壁纸选中态由 mirage.selectedWallpaperId 驱动。
    property bool selected: String(field("id", "")) === String(mirage.selectedWallpaperId)

    // 内容区：标题位于底部（图片区 fillHeight 吃掉剩余空间，
    // 文本自然落在 ColumnLayout 底部，与旧内联 delegate 布局一致）。
    FluText {
        Layout.fillWidth: true
        Layout.preferredHeight: implicitHeight + 12
        text: String(root.field("title", ""))
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    onClicked: function(mouse) {
        if (mouse.button === Qt.RightButton) {
            // 右键：先选中再弹上下文菜单（对齐旧 delegate 行为）。
            mirage.selectWallpaper(String(root.field("id", "")));
            root.host.openWallpaperContextMenu(root.itemData);
            return;
        }
        mirage.selectWallpaper(String(root.field("id", "")));
    }
    onDoubleClicked: function(mouse) {
        if (mouse.button !== Qt.LeftButton)
            return;
        // 应用壁纸前对网页壁纸执行信任确认（对齐旧 delegate 的
        // runWithWallpaperTrust 包装）。
        root.host.runWithWallpaperTrust(root.itemData, function() {
            mirage.applyWallpaper(String(root.field("id", "")), false);
        });
    }
}
