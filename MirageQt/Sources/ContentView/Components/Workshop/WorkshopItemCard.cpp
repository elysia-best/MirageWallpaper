#include "ContentView/Components/Workshop/WorkshopItemCard.h"

#include <QAbstractItemView>
#include <QAbstractItemModel>
#include <QBuffer>
#include <QEvent>
#include <QIcon>
#include <QLinearGradient>
#include <QListWidgetItem>
#include <QMovie>
#include <QPainter>
#include <QPainterPath>

namespace Mirage {
namespace {

QColor kindColor(WallpaperKind kind) {
    switch (kind) {
    case WallpaperKind::Scene: return QColor(QStringLiteral("#a747d1"));
    case WallpaperKind::Web: return QColor(QStringLiteral("#d97a27"));
    case WallpaperKind::Video: return QColor(QStringLiteral("#2789d9"));
    case WallpaperKind::Unsupported: return QColor(QStringLiteral("#74716d"));
    }
    return QColor(QStringLiteral("#74716d"));
}

void drawBadge(QPainter* painter,
               const QRectF& bounds,
               const QString& text,
               const QColor& color,
               Qt::Alignment alignment) {
    QFont font = painter->font();
    font.setPixelSize(10);
    font.setBold(true);
    painter->setFont(font);
    const int width = QFontMetrics(font).horizontalAdvance(text) + 14;
    const qreal x = alignment.testFlag(Qt::AlignRight)
        ? bounds.right() - width - 7
        : bounds.left() + 7;
    const QRectF badge(x, bounds.top() + 7, width, 21);
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawRoundedRect(badge, 4, 4);
    painter->setPen(Qt::white);
    painter->drawText(badge, Qt::AlignCenter, text);
}

QString downloadBadgeText(const DownloadState& state) {
    switch (state.kind) {
    case DownloadStateKind::Queued: return QStringLiteral("排队中");
    case DownloadStateKind::Starting:
    case DownloadStateKind::Validating: return QStringLiteral("处理中");
    case DownloadStateKind::Downloading:
        return state.percent >= 0.0
            ? QStringLiteral("%1%").arg(qRound(state.percent * 100.0))
            : QStringLiteral("连接中");
    case DownloadStateKind::Completed: return QStringLiteral("已下载");
    case DownloadStateKind::Failed: return QStringLiteral("失败");
    case DownloadStateKind::Cancelled: return QStringLiteral("已取消");
    }
    return {};
}

QColor downloadBadgeColor(const DownloadState& state) {
    switch (state.kind) {
    case DownloadStateKind::Queued: return QColor(QStringLiteral("#d98222"));
    case DownloadStateKind::Failed:
    case DownloadStateKind::Cancelled: return QColor(QStringLiteral("#c44336"));
    case DownloadStateKind::Completed: return QColor(QStringLiteral("#29964a"));
    default: return QColor(QStringLiteral("#0a84ff"));
    }
}

} // namespace

WorkshopItemCard::WorkshopItemCard(QObject* parent)
    : QStyledItemDelegate(parent)
    , m_previewAnimator(qobject_cast<QAbstractItemView*>(parent)) {
}

QSize WorkshopItemCard::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
    return QSize(240, 143);
}

void WorkshopItemCard::paint(QPainter* painter,
                             const QStyleOptionViewItem& option,
                             const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    const WorkshopItem item = index.data(WorkshopItemRole).value<WorkshopItem>();
    QRectF card = option.rect.adjusted(5, 5, -5, -5);
    const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    if (hovered) card.adjust(1, 1, -1, -1);

    QPainterPath clip;
    clip.addRoundedRect(card, 8, 8);
    painter->setClipPath(clip);
    painter->fillPath(clip, QColor(QStringLiteral("#292622")));

    const QPixmap preview = m_previewAnimator.pixmapFor(index, hovered);
    if (!preview.isNull()) {
        const qreal sourceRatio = qreal(preview.width()) / qMax(1, preview.height());
        const qreal targetRatio = card.width() / qMax<qreal>(1.0, card.height());
        QRectF source = preview.rect();
        if (sourceRatio > targetRatio) {
            const qreal width = preview.height() * targetRatio;
            source.setLeft((preview.width() - width) / 2.0);
            source.setWidth(width);
        } else {
            const qreal height = preview.width() / targetRatio;
            source.setTop((preview.height() - height) / 2.0);
            source.setHeight(height);
        }
        painter->drawPixmap(card, preview, source);
    } else {
        painter->fillRect(card, QColor(QStringLiteral("#504c47")));
        const QPixmap fallback = QIcon::fromTheme(QStringLiteral("image-x-generic")).pixmap(42, 42);
        painter->drawPixmap(QRectF(card.center().x() - 21, card.center().y() - 28, 42, 42), fallback, fallback.rect());
    }

    QLinearGradient fade(card.left(), card.top() + card.height() * 0.47,
                         card.left(), card.bottom());
    fade.setColorAt(0.0, QColor(0, 0, 0, 0));
    fade.setColorAt(0.58, QColor(0, 0, 0, 115));
    fade.setColorAt(1.0, QColor(0, 0, 0, 205));
    painter->fillRect(card, fade);
    painter->setClipping(false);

    if (item.isPreset()) drawBadge(painter, card, QStringLiteral("预设"), QColor(QStringLiteral("#a747d1")), Qt::AlignLeft);

    const bool needsDependency = index.data(WorkshopPresetNeedsDependencyRole).toBool();
    const QVariant downloadValue = index.data(WorkshopDownloadStateRole);
    if (downloadValue.isValid()) {
        const DownloadState state = downloadValue.value<DownloadState>();
        if (!(needsDependency && state.kind == DownloadStateKind::Completed)) {
            drawBadge(painter, card, downloadBadgeText(state), downloadBadgeColor(state), Qt::AlignRight);
        }
    } else if (index.data(WorkshopDownloadedRole).toBool()) {
        drawBadge(painter,
                  card,
                  needsDependency ? QStringLiteral("缺少基础壁纸") : QStringLiteral("已下载"),
                  needsDependency ? QColor(QStringLiteral("#d98222")) : QColor(QStringLiteral("#29964a")),
                  Qt::AlignRight);
    }

    QFont titleFont = option.font;
    titleFont.setPixelSize(12);
    titleFont.setBold(true);
    painter->setFont(titleFont);
    painter->setPen(Qt::white);
    const QFontMetrics titleMetrics(titleFont);
    const QRectF titleRect(card.left() + 9, card.bottom() - 43, card.width() - 18, 18);
    painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                      titleMetrics.elidedText(item.title, Qt::ElideRight, qRound(titleRect.width())));

    QFont metaFont = option.font;
    metaFont.setPixelSize(10);
    painter->setFont(metaFont);
    const QString counts = QStringLiteral("↓ %1    ◉ %2").arg(item.formattedSubscriptions(), item.formattedViews());
    painter->setPen(QColor(255, 255, 255, 195));
    painter->drawText(QRectF(card.left() + 9, card.bottom() - 24, card.width() - 90, 16),
                      Qt::AlignLeft | Qt::AlignVCenter, counts);

    const QString kind = item.displayTypeName();
    const QFontMetrics metaMetrics(metaFont);
    const int kindWidth = metaMetrics.horizontalAdvance(kind) + 10;
    const QRectF kindRect(card.right() - kindWidth - 8, card.bottom() - 25, kindWidth, 17);
    painter->setPen(Qt::NoPen);
    painter->setBrush(kindColor(item.kind()));
    painter->drawRoundedRect(kindRect, 3, 3);
    painter->setPen(Qt::white);
    painter->drawText(kindRect, Qt::AlignCenter, kind);

    if (hovered || selected) {
        painter->setPen(QPen(QColor(QStringLiteral("#0a84ff")), selected ? 3.0 : 1.2));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(card.adjusted(1, 1, -1, -1), 8, 8);
    }
    painter->restore();
}

WorkshopPreviewAnimator::WorkshopPreviewAnimator(QAbstractItemView* view)
    : m_view(view) {
    if (m_view && m_view->viewport()) m_view->viewport()->installEventFilter(this);
    if (m_view && m_view->model()) {
        connect(m_view->model(), &QAbstractItemModel::modelReset, this, [this] { stopAllExcept({}); });
    }
}

QPixmap WorkshopPreviewAnimator::pixmapFor(const QModelIndex& index, bool hovered) {
    const QPixmap fallback = index.data(WorkshopPreviewRole).value<QPixmap>();
    const QByteArray bytes = index.data(WorkshopPreviewBytesRole).toByteArray();
    if (!hovered || (!bytes.startsWith("GIF87a") && !bytes.startsWith("GIF89a"))) {
        return fallback;
    }

    const WorkshopItem item = index.data(WorkshopItemRole).value<WorkshopItem>();
    const QString key = item.previewImageUrl.toString();
    if (key.isEmpty()) return fallback;
    stopAllExcept(key);
    QMovie* movie = movieFor(key, bytes);
    if (!movie) return fallback;
    m_activeKey = key;
    if (movie->state() != QMovie::Running) movie->start();
    const QPixmap frame = movie->currentPixmap();
    return frame.isNull() ? fallback : frame;
}

QMovie* WorkshopPreviewAnimator::movieFor(const QString& key, const QByteArray& bytes) {
    if (QPointer<QMovie> existing = m_movies.value(key)) return existing;
    if (!m_view) return nullptr;

    auto* movie = new QMovie(m_view);
    auto* buffer = new QBuffer(movie);
    buffer->setData(bytes);
    if (!buffer->open(QIODevice::ReadOnly)) {
        movie->deleteLater();
        return nullptr;
    }
    movie->setDevice(buffer);
    movie->setFormat("gif");
    movie->setCacheMode(QMovie::CacheNone);
    connect(movie, &QMovie::frameChanged, m_view, [view = m_view] {
        if (view && view->viewport()) view->viewport()->update();
    });
    m_movies.insert(key, movie);
    return movie;
}

bool WorkshopPreviewAnimator::eventFilter(QObject* watched, QEvent* event) {
    if (m_view && watched == m_view->viewport() &&
        (event->type() == QEvent::Leave || event->type() == QEvent::Hide)) {
        stopAllExcept({});
    }
    return QObject::eventFilter(watched, event);
}

void WorkshopPreviewAnimator::stopAllExcept(const QString& key) {
    for (auto it = m_movies.begin(); it != m_movies.end(); ++it) {
        if (it.key() != key && it.value() && it.value()->state() == QMovie::Running) it.value()->stop();
    }
    if (key.isEmpty()) m_activeKey.clear();
}

void setWorkshopCardData(QListWidgetItem* row,
                         const WorkshopItem& item,
                         bool downloaded,
                         bool presetNeedsDependency,
                         const std::optional<DownloadState>& downloadState) {
    row->setData(WorkshopItemRole, QVariant::fromValue(item));
    row->setData(WorkshopDownloadedRole, downloaded);
    row->setData(WorkshopPresetNeedsDependencyRole, presetNeedsDependency);
    row->setData(WorkshopDownloadStateRole,
                 downloadState ? QVariant::fromValue(*downloadState) : QVariant());
    row->setToolTip(item.title);
}

} // namespace Mirage
