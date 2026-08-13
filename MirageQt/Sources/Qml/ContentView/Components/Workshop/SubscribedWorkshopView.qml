import QtQuick
import QtQuick.Layouts
import FluentUI
import "../../../GlobalComponents"
import "../../../MirageBridge.js" as MirageBridge

// 已订阅壁纸独立视图：对齐 macOS Components/Workshop/SubscribedWorkshopView.swift。
// 工具栏（筛选/标题/计数/搜索/下载全部/刷新/Steam 状态）与内容状态机
// （加载/未登录/未订阅/无匹配/网格）均对齐上游；下载全部经后端生成
// SubscriptionDownloadPlan 后由确认弹窗呈现（对齐上游 alert）。
Item {
    id: root
    required property var host
    // 窗口较窄时网格内容可能短暂超出容器，裁剪以免与右侧详情重叠。
    clip: true

    // 订阅状态全部由后端 WorkshopViewModel 持有，经 mirage 读取、
    // 经 mirage.setSubscription*/loadSubscriptions 写回（对齐上游
    // workshopViewModel.subscription* 属性）。
    property var subscriptions: mirage.subscriptions
    property bool subscriptionsLoading: Boolean(mirage.subscriptionsLoading)
    property int subscriptionTotal: Number(mirage.subscriptionTotal)
    property int subscriptionPage: Number(mirage.subscriptionPage)
    property int subscriptionPageCount: Number(mirage.subscriptionPageCount)
    property var subscriptionFilters: mirage.subscriptionFilters
    property bool steamReady: Boolean(mirage.steamReady)
    property bool steamLoggedIn: Boolean(mirage.steamLoggedIn)
    property string steamUsername: String(mirage.steamUsername)
    property bool downloadPreparing: Boolean(mirage.subscriptionDownloadPreparing)
    property var downloadPlan: mirage.subscriptionDownloadPlan

    function invoke(name) {
        return MirageBridge.invoke(mirage, name, Array.prototype.slice.call(arguments, 1));
    }

    // 首次进入（订阅 tab 打开）且已登录但无缓存内容时加载（对齐 onAppear 分支）。
    Component.onCompleted: {
        if (mirage.steamLoggedIn && subscriptions.length === 0 && !subscriptionsLoading)
            invoke("loadSubscriptions");
    }
    // 登录状态变化时刷新（对齐 onChange(of: steamService.isLoggedIn)）。
    onSteamLoggedInChanged: {
        if (mirage.steamLoggedIn)
            invoke("loadSubscriptions");
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // 工具栏（对齐 macOS SubscribedWorkshopView.toolbar）。
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            FluFilledButton {
                text: qsTr("筛选")
                onClicked: root.host.filtersVisible = !root.host.filtersVisible
            }
            FluText {
                text: qsTr("已订阅")
                font: FluTextStyle.BodyStrong
            }
            FluText {
                visible: root.subscriptions.length > 0
                text: qsTr("共 %1 项").arg(root.subscriptionTotal)
                color: FluTheme.fontSecondaryColor
                font: FluTextStyle.Caption
            }
            FluTextBox {
                Layout.preferredWidth: 200
                Layout.minimumWidth: 140
                Layout.maximumWidth: 240
                placeholderText: qsTr("搜索已订阅壁纸...")
                iconSource: FluentIcons.Search
                text: String(MirageBridge.field(root.subscriptionFilters, "searchText", ""))
                onTextChanged: root.invoke("setSubscriptionSearchText", text)
            }
            Item {
                Layout.fillWidth: true
            }
            // "下载全部"：生成确认计划后由 downloadPlanDialog 呈现
            // （对齐 macOS 的"下载全部"按钮 + alert）。
            FluFilledButton {
                text: root.downloadPreparing ? qsTr("正在准备下载…") : qsTr("下载全部")
                disabled: !root.steamLoggedIn || root.downloadPreparing || root.subscriptions.length === 0
                onClicked: root.invoke("downloadAllSubscriptions")
            }
            FluIconButton {
                iconSource: FluentIcons.Refresh
                text: qsTr("刷新已订阅壁纸")
                contentDescription: qsTr("刷新已订阅壁纸")
                disabled: !root.steamLoggedIn || root.subscriptionsLoading
                onClicked: root.invoke("loadSubscriptions")
            }
            // 视图菜单（图标尺寸/每页数量），对齐 macOS SubscribedWorkshopView
            // toolbar 的 WallpaperGridViewMenu(showsPageSize: true)；
            // 每页数量变化同时更新 host 偏好与后端订阅分页。
            WallpaperGridViewMenu {
                explorerIconSize: root.host.explorerIconSize
                wallpapersPerPage: root.host.wallpapersPerPage
                showsPageSize: true
                onIconSizeChanged: size => root.host.explorerIconSize = size
                onPageSizeChanged: count => {
                    root.host.wallpapersPerPage = count;
                    root.invoke("setSubscriptionPerPage", count);
                }
            }
            RowLayout {
                visible: root.steamLoggedIn
                spacing: 4
                FluIcon {
                    iconSource: FluentIcons.ContactSolid
                    iconSize: 15
                    iconColor: Qt.rgba(16 / 255, 124 / 255, 16 / 255, 1)
                }
                FluText {
                    text: root.steamUsername
                    elide: Text.ElideRight
                    Layout.maximumWidth: 80
                    color: FluTheme.fontSecondaryColor
                    font: FluTextStyle.Caption
                }
                FluIconButton {
                    iconSource: FluentIcons.SignOut
                    text: qsTr("退出 Steam")
                    contentDescription: qsTr("退出 Steam")
                    onClicked: root.invoke("logoutSteam")
                }
            }
            FluFilledButton {
                visible: !root.steamLoggedIn
                text: qsTr("登录 Steam")
                onClicked: root.host.openSteamSetup()
            }
        }

        FluScrollablePage {
            Layout.fillWidth: true
            Layout.fillHeight: true
            // 空态（无网格内容）时内容区铺满视口，使空态提示垂直+水平居中
            // （对齐 macOS 的 centered() frame(maxHeight: .infinity)）；
            // 有内容时恢复自然高度（滚动模型）。
            columnHeight: root.subscriptions.length === 0 ? height : undefined

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8

                // 首次加载（无缓存内容）时显示进度（居中）。
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.subscriptionsLoading && root.subscriptions.length === 0
                    FluProgressRing {
                        anchors.centerIn: parent
                        indeterminate: true
                    }
                }
                // 未登录空态（对齐 !steamService.isLoggedIn 分支，居中显示）。
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: !root.steamLoggedIn
                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 8
                        FluIcon {
                            Layout.alignment: Qt.AlignHCenter
                            iconSource: FluentIcons.Contact
                            iconSize: 36
                            iconColor: FluTheme.fontSecondaryColor
                        }
                        FluText {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("登录 Steam 后即可查看已订阅壁纸")
                            font: FluTextStyle.BodyStrong
                        }
                        FluFilledButton {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("登录 Steam")
                            onClicked: root.host.openSteamSetup()
                        }
                    }
                }
                // 空态：尚未订阅任何壁纸（对齐 subscriptionCatalogItems.isEmpty 分支）。
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.steamLoggedIn && !root.subscriptionsLoading && root.subscriptionTotal === 0
                    FluText {
                        anchors.centerIn: parent
                        text: qsTr("尚未订阅任何壁纸")
                        color: FluTheme.fontSecondaryColor
                    }
                }
                // 空态：有订阅记录但当前筛选无匹配（对齐 subscriptionItems.isEmpty 分支）。
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.steamLoggedIn && !root.subscriptionsLoading && root.subscriptionTotal > 0
                            && root.subscriptions.length === 0
                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 8
                        FluText {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("没有符合筛选条件的已订阅壁纸")
                            color: FluTheme.fontSecondaryColor
                        }
                        FluFilledButton {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("重置筛选")
                            onClicked: mirage.clearSubscriptionFilters()
                        }
                    }
                }
                // 结果计数（对齐"共 %d 项"）。
                FluText {
                    Layout.alignment: Qt.AlignHCenter
                    visible: root.subscriptions.length > 0
                    text: qsTr("已订阅 %1 项").arg(root.subscriptionTotal)
                    color: FluTheme.fontSecondaryColor
                    font: FluTextStyle.Caption
                }
                // 订阅网格：与浏览页共用 WorkshopItemGrid（滚动模型/列数
                // 自适应/cellHeight 均一致），卡片点击进入详情。
                WorkshopItemGrid {
                    host: root.host
                    items: root.subscriptions
                }
                // 客户端过滤后的分页（对齐 PageNavigator）。
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    visible: root.subscriptionPageCount > 1
                    FluIconButton {
                        iconSource: FluentIcons.ChevronLeft
                        text: qsTr("上一页")
                        contentDescription: qsTr("上一页")
                        disabled: root.subscriptionPage <= 1
                        onClicked: root.invoke("goToSubscriptionPage", root.subscriptionPage - 1)
                    }
                    FluText {
                        text: root.subscriptionPage + " / " + root.subscriptionPageCount
                    }
                    FluIconButton {
                        iconSource: FluentIcons.ChevronRight
                        text: qsTr("下一页")
                        contentDescription: qsTr("下一页")
                        disabled: root.subscriptionPage >= root.subscriptionPageCount
                        onClicked: root.invoke("goToSubscriptionPage", root.subscriptionPage + 1)
                    }
                }
            }
        }
    }

    // 下载全部确认弹窗（对齐 macOS 的 alert：plan.downloadCount 决定按钮文案）。
    FluContentDialog {
        id: downloadPlanDialog
        title: qsTr("下载全部已订阅壁纸")
        message: root.downloadPlanMessage()
        negativeText: qsTr("取消")
        positiveText: root.downloadCount() > 0 ? qsTr("开始下载") : qsTr("好")
        buttonFlags: root.downloadCount() > 0
            ? FluContentDialogType.NegativeButton | FluContentDialogType.PositiveButton
            : FluContentDialogType.PositiveButton
        onNegativeClicked: mirage.dismissSubscriptionDownloadPlan()
        onPositiveClicked: {
            if (root.downloadCount() > 0)
                mirage.confirmSubscriptionDownloads();
            else
                mirage.dismissSubscriptionDownloadPlan();
        }
    }
    // 后端生成/更新计划时弹出确认框。
    onDownloadPlanChanged: {
        if (downloadPlan && downloadPlan.subscriptionCount !== undefined)
            downloadPlanDialog.open();
    }

    function downloadCount() {
        return Number(MirageBridge.field(root.downloadPlan, "downloadCount", 0));
    }

    function downloadPlanMessage() {
        var plan = root.downloadPlan;
        if (!plan || plan.subscriptionCount === undefined)
            return "";
        var subscriptionCount = Number(plan.subscriptionCount);
        var remainingCount = Number(plan.remainingCount);
        var downloadCount = Number(plan.downloadCount);
        // 文案对齐 macOS SubscribedWorkshopView 的 alert message。
        if (downloadCount > 0)
            return qsTr("已订阅 %1 个壁纸，还剩 %2 个未下载，本次将下载 %3 个。")
                .arg(subscriptionCount).arg(remainingCount).arg(downloadCount);
        if (remainingCount > 0)
            return qsTr("已订阅 %1 个壁纸，还剩 %2 个正在下载，本次不需要新增下载。")
                .arg(subscriptionCount).arg(remainingCount);
        return qsTr("已订阅 %1 个壁纸，已全部下载，不需要下载。").arg(subscriptionCount);
    }
}
