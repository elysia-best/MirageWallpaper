#pragma once

#include "Services/WorkshopModels.h"

#include <QHash>
#include <QPointer>
#include <QStyledItemDelegate>

#include <optional>

class QListWidgetItem;
class QAbstractItemView;
class QEvent;
class QMovie;

namespace Mirage {

enum WorkshopCardRole {
    WorkshopItemRole = Qt::UserRole + 1,
    WorkshopPreviewRole,
    WorkshopPreviewBytesRole,
    WorkshopDownloadedRole,
    WorkshopPresetNeedsDependencyRole,
    WorkshopDownloadStateRole,
};

class WorkshopPreviewAnimator final : public QObject {
public:
    explicit WorkshopPreviewAnimator(QAbstractItemView* view = nullptr);

    QPixmap pixmapFor(const QModelIndex& index, bool hovered);

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    QMovie* movieFor(const QString& key, const QByteArray& bytes);
    void stopAllExcept(const QString& key);

    QPointer<QAbstractItemView> m_view;
    QHash<QString, QPointer<QMovie>> m_movies;
    QString m_activeKey;
};

class WorkshopItemCard final : public QStyledItemDelegate {
public:
    explicit WorkshopItemCard(QObject* parent = nullptr);

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

private:
    mutable WorkshopPreviewAnimator m_previewAnimator;
};

void setWorkshopCardData(QListWidgetItem* row,
                         const WorkshopItem& item,
                         bool downloaded,
                         bool presetNeedsDependency,
                         const std::optional<DownloadState>& downloadState);

} // namespace Mirage
