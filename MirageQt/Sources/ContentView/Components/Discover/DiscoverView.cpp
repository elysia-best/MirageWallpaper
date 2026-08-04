#include "ContentView/Components/Discover/DiscoverView.h"

#include "ContentView/Components/Discover/DiscoverSectionView.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace Mirage {

DiscoverView::DiscoverView(WorkshopViewModel* viewModel,
                           SteamWebAPI* api,
                           GlobalSettingsService* settings,
                           QWidget* parent)
    : QWidget(parent)
    , m_viewModel(viewModel)
    , m_settings(settings) {
    m_stack = new QStackedWidget(this);

    auto* loading = new QWidget(m_stack);
    auto* progress = new QProgressBar(loading);
    progress->setRange(0, 0);
    progress->setFixedWidth(180);
    auto* loadingText = new QLabel(QStringLiteral("正在加载推荐内容..."), loading);
    loadingText->setAlignment(Qt::AlignCenter);
    loadingText->setProperty("secondary", true);
    auto* loadingLayout = new QVBoxLayout(loading);
    loadingLayout->addStretch(1);
    loadingLayout->addWidget(progress, 0, Qt::AlignHCenter);
    loadingLayout->addWidget(loadingText);
    loadingLayout->addStretch(1);

    auto* scroll = new QScrollArea(m_stack);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget(scroll);
    auto* sections = new QVBoxLayout(content);
    sections->setContentsMargins(0, 0, 0, 16);
    sections->setSpacing(24);

    m_apiKeyBanner = new QWidget(content);
    m_apiKeyBanner->setObjectName(QStringLiteral("apiKeyBanner"));
    auto* keyIcon = new QLabel(QStringLiteral("⚿"), m_apiKeyBanner);
    QFont iconFont = keyIcon->font();
    iconFont.setPointSize(20);
    keyIcon->setFont(iconFont);
    auto* keyText = new QLabel(QStringLiteral("<b>建议设置专属 Steam Web API Key</b><br>"
                                                  "<span style='color:#aaa59f'>内置 Key 由所有用户共享，可能因请求过多影响浏览；专属 Key 不影响 SteamCMD 登录和下载。</span>"),
                               m_apiKeyBanner);
    keyText->setWordWrap(true);
    auto* keySettings = new QPushButton(QStringLiteral("立即设置"), m_apiKeyBanner);
    keySettings->setProperty("warningAccent", true);
    auto* keyLayout = new QHBoxLayout(m_apiKeyBanner);
    keyLayout->setContentsMargins(12, 9, 12, 9);
    keyLayout->setSpacing(12);
    keyLayout->addWidget(keyIcon);
    keyLayout->addWidget(keyText, 1);
    keyLayout->addWidget(keySettings);
    sections->addWidget(m_apiKeyBanner);

    sections->addWidget(new DiscoverSectionView(QStringLiteral("本周最热"), QStringLiteral("weather-clear"), QColor(QStringLiteral("#ff9f0a")),
                                                DiscoverCollection::Trending, m_viewModel, api, content));
    sections->addWidget(new DiscoverSectionView(QStringLiteral("最多投票"), QStringLiteral("emblem-favorite"), QColor(QStringLiteral("#ff375f")),
                                                DiscoverCollection::MostUpvoted, m_viewModel, api, content));
    sections->addWidget(new DiscoverSectionView(QStringLiteral("最新上架"), QStringLiteral("document-open-recent"), QColor(QStringLiteral("#0a84ff")),
                                                DiscoverCollection::MostRecent, m_viewModel, api, content));
    sections->addWidget(new DiscoverSectionView(QStringLiteral("订阅最多"), QStringLiteral("folder-download"), QColor(QStringLiteral("#30d158")),
                                                DiscoverCollection::MostSubscribed, m_viewModel, api, content));
    sections->addWidget(new DiscoverSectionView(QStringLiteral("评分最高"), QStringLiteral("rating"), QColor(QStringLiteral("#ffd60a")),
                                                DiscoverCollection::TopRated, m_viewModel, api, content));
    sections->addWidget(new DiscoverSectionView(QStringLiteral("最近更新"), QStringLiteral("view-refresh"), QColor(QStringLiteral("#5e5ce6")),
                                                DiscoverCollection::LastUpdated, m_viewModel, api, content));
    sections->addWidget(new DiscoverSectionView(QStringLiteral("本周播放时长最多"), QStringLiteral("hourglass"), QColor(QStringLiteral("#30d158")),
                                                DiscoverCollection::PlaytimeTrend, m_viewModel, api, content));
    sections->addWidget(new DiscoverSectionView(QStringLiteral("本周平均播放时长最长"), QStringLiteral("chronometer"), QColor(QStringLiteral("#64d2ff")),
                                                DiscoverCollection::AveragePlaytimeTrend, m_viewModel, api, content));
    sections->addWidget(new DiscoverSectionView(QStringLiteral("本周播放次数最多"), QStringLiteral("view-refresh"), QColor(QStringLiteral("#40c8e0")),
                                                DiscoverCollection::SessionsTrend, m_viewModel, api, content));
    sections->addWidget(new DiscoverSectionView(QStringLiteral("总播放时长最多"), QStringLiteral("hourglass"), QColor(QStringLiteral("#bf5af2")),
                                                DiscoverCollection::TotalPlaytime, m_viewModel, api, content));
    sections->addWidget(new DiscoverSectionView(QStringLiteral("终身平均播放时长"), QStringLiteral("chronometer"), QColor(QStringLiteral("#0a84ff")),
                                                DiscoverCollection::LifetimeAveragePlaytime, m_viewModel, api, content));
    sections->addWidget(new DiscoverSectionView(QStringLiteral("总播放次数最多"), QStringLiteral("view-refresh"), QColor(QStringLiteral("#30d158")),
                                                DiscoverCollection::LifetimeSessions, m_viewModel, api, content));
    sections->addWidget(new DiscoverSectionView(QStringLiteral("动漫精选"), QStringLiteral("weather-stars"), QColor(QStringLiteral("#bf5af2")),
                                                DiscoverCollection::Anime, m_viewModel, api, content));
    sections->addWidget(new DiscoverSectionView(QStringLiteral("自然风光"), QStringLiteral("emblem-photos"), QColor(QStringLiteral("#30d158")),
                                                DiscoverCollection::Nature, m_viewModel, api, content));
    sections->addWidget(new DiscoverSectionView(QStringLiteral("抽象艺术"), QStringLiteral("applications-graphics"), QColor(QStringLiteral("#64d2ff")),
                                                DiscoverCollection::Abstract, m_viewModel, api, content));
    sections->addWidget(new DiscoverSectionView(QStringLiteral("风景壁纸"), QStringLiteral("image-x-generic"), QColor(QStringLiteral("#5ac8fa")),
                                                DiscoverCollection::Landscape, m_viewModel, api, content));
    sections->addStretch(1);
    scroll->setWidget(content);

    m_stack->addWidget(loading);
    m_stack->addWidget(scroll);
    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(12, 8, 12, 0);
    toolbar->addWidget(new QLabel(QStringLiteral("趋势范围"), this));
    m_trendPeriod = new QComboBox(this);
    for (WorkshopTrendPeriod period : {WorkshopTrendPeriod::Day, WorkshopTrendPeriod::Week,
                                       WorkshopTrendPeriod::Month, WorkshopTrendPeriod::ThreeMonths,
                                       WorkshopTrendPeriod::SixMonths, WorkshopTrendPeriod::Year}) {
        m_trendPeriod->addItem(workshopTrendPeriodLabel(period), static_cast<int>(period));
    }
    m_trendPeriod->setFixedWidth(110);
    toolbar->addWidget(m_trendPeriod);
    toolbar->addStretch(1);
    auto* refresh = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), QString(), this);
    refresh->setToolTip(QStringLiteral("刷新发现"));
    refresh->setFixedSize(34, 30);
    toolbar->addWidget(refresh);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(toolbar);
    layout->addWidget(m_stack);

    connect(keySettings, &QPushButton::clicked, this, &DiscoverView::settingsRequested);
    connect(m_viewModel, &WorkshopViewModel::discoverChanged, this, &DiscoverView::updateState);
    connect(m_trendPeriod, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_viewModel->setDiscoverTrendDays(m_trendPeriod->itemData(index).toInt());
    });
    connect(refresh, &QPushButton::clicked, m_viewModel, &WorkshopViewModel::refreshDiscover);
    connect(m_settings, &GlobalSettingsService::settingsChanged, this, &DiscoverView::updateAPIKeyBanner);
    updateAPIKeyBanner();
    m_trendPeriod->setCurrentIndex(m_trendPeriod->findData(m_viewModel->discoverTrendDays()));
    updateState();
}

void DiscoverView::activate() {
    if (m_viewModel->discoverItems(DiscoverCollection::Trending).isEmpty()) m_viewModel->loadDiscover();
}

void DiscoverView::updateState() {
    const bool initialLoading = m_viewModel->isDiscoverLoading() &&
                                m_viewModel->discoverItems(DiscoverCollection::Trending).isEmpty();
    m_stack->setCurrentIndex(initialLoading ? 0 : 1);
}

void DiscoverView::updateAPIKeyBanner() {
    m_apiKeyBanner->setVisible(!m_settings->hasValidCustomSteamAPIKey());
}

} // namespace Mirage
