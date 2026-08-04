#include "ContentView/Components/Workshop/WorkshopSearchBar.h"
#include "App/MirageWidgets.h"

#include <QHBoxLayout>
#include <QSignalBlocker>

namespace Mirage {

WorkshopSearchBar::WorkshopSearchBar(WorkshopViewModel* viewModel, QWidget* parent)
    : QWidget(parent)
    , m_viewModel(viewModel) {
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("搜索创意工坊壁纸..."));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(250);
    m_search->setMaximumWidth(520);

    m_sort = new MirageComboBox(this);
    m_sort->setAccessibleName(QStringLiteral("排序"));
    for (WorkshopSortOrder order : {WorkshopSortOrder::Trending,
                                    WorkshopSortOrder::MostRecent,
                                    WorkshopSortOrder::MostSubscribed,
                                    WorkshopSortOrder::TopRated,
                                    WorkshopSortOrder::MostUpvoted,
                                    WorkshopSortOrder::PlaytimeTrend,
                                    WorkshopSortOrder::TotalPlaytime,
                                    WorkshopSortOrder::AveragePlaytimeTrend,
                                    WorkshopSortOrder::LifetimeAveragePlaytime,
                                    WorkshopSortOrder::SessionsTrend,
                                    WorkshopSortOrder::LifetimeSessions,
                                    WorkshopSortOrder::LastUpdated}) {
        m_sort->addItem(workshopSortLabel(order), QVariant::fromValue(order));
    }
    m_sort->setFixedWidth(120);

    m_trendPeriod = new MirageComboBox(this);
    m_trendPeriod->setAccessibleName(QStringLiteral("趋势范围"));
    for (WorkshopTrendPeriod period : {WorkshopTrendPeriod::Day, WorkshopTrendPeriod::Week,
                                       WorkshopTrendPeriod::Month, WorkshopTrendPeriod::ThreeMonths,
                                       WorkshopTrendPeriod::SixMonths, WorkshopTrendPeriod::Year}) {
        m_trendPeriod->addItem(workshopTrendPeriodLabel(period), static_cast<int>(period));
    }
    m_trendPeriod->setFixedWidth(96);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(m_search, 1);
    layout->addWidget(m_sort);
    layout->addWidget(m_trendPeriod);

    connect(m_search, &QLineEdit::textChanged, m_viewModel, &WorkshopViewModel::setSearchText);
    connect(m_search, &QLineEdit::returnPressed, m_viewModel, &WorkshopViewModel::submitSearch);
    connect(m_sort, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_viewModel->setSortOrder(m_sort->itemData(index).value<WorkshopSortOrder>());
    });
    connect(m_trendPeriod, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_viewModel->setTrendDays(m_trendPeriod->itemData(index).toInt());
    });
    connect(m_viewModel, &WorkshopViewModel::filtersChanged, this, &WorkshopSearchBar::syncFromViewModel);
    syncFromViewModel();
}

void WorkshopSearchBar::syncFromViewModel() {
    const QSignalBlocker searchBlocker(m_search);
    const QSignalBlocker sortBlocker(m_sort);
    const QSignalBlocker periodBlocker(m_trendPeriod);
    m_search->setText(m_viewModel->searchText());
    for (int i = 0; i < m_sort->count(); ++i) {
        if (m_sort->itemData(i).value<WorkshopSortOrder>() == m_viewModel->sortOrder()) {
            m_sort->setCurrentIndex(i);
            break;
        }
    }
    for (int i = 0; i < m_trendPeriod->count(); ++i) {
        if (m_trendPeriod->itemData(i).toInt() == m_viewModel->trendDays()) {
            m_trendPeriod->setCurrentIndex(i);
            break;
        }
    }
    m_trendPeriod->setVisible(workshopSortUsesTrendPeriod(m_viewModel->sortOrder()));
}

} // namespace Mirage
