#include "ContentView/Components/Playlist/PlaylistOpenDialog.h"

#include <QDateTime>
#include <QUuid>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QIcon>

namespace Mirage {

PlaylistOpenDialog::PlaylistOpenDialog(PlaylistManager* manager, int screen, QWidget* parent)
    : QDialog(parent)
    , m_manager(manager)
    , m_screen(screen) {
    setWindowTitle(QStringLiteral("打开播放列表"));
    resize(460, 360);

    auto* title = new QLabel(QStringLiteral("打开播放列表"), this);
    QFont font = title->font();
    font.setPointSize(16);
    font.setBold(true);
    title->setFont(font);

    m_list = new QListWidget(this);
    m_list->setMinimumHeight(220);

    auto* done = new QPushButton(QStringLiteral("完成"), this);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(done);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(title);
    layout->addWidget(m_list, 1);
    layout->addLayout(buttons);

    connect(done, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_manager, &PlaylistManager::savedChanged, this, &PlaylistOpenDialog::rebuild);
    rebuild();
}

void PlaylistOpenDialog::rebuild() {
    m_list->clear();
    const QVector<Playlist> saved = m_manager->saved();
    if (saved.isEmpty()) {
        auto* item = new QListWidgetItem(QStringLiteral("您尚未创建任何播放列表。"));
        item->setFlags(Qt::NoItemFlags);
        m_list->addItem(item);
        return;
    }

    for (const Playlist& playlist : saved) {
        auto* row = new QWidget(m_list);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(8, 6, 8, 6);
        auto* text = new QLabel(QStringLiteral("%1\n%2 · %3")
                                    .arg(playlist.name)
                                    .arg(playlist.items.size())
                                    .arg(playlist.updatedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))),
                                row);
        auto* load = new QPushButton(QStringLiteral("读取"), row);
        load->setProperty("accent", true);
        auto* remove = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-delete")), QString(), row);
        remove->setFlat(true);
        rowLayout->addWidget(text, 1);
        rowLayout->addWidget(load);
        rowLayout->addWidget(remove);

        auto* item = new QListWidgetItem(m_list);
        item->setSizeHint(row->sizeHint());
        m_list->addItem(item);
        m_list->setItemWidget(item, row);

        const QUuid id = playlist.id;
        connect(load, &QPushButton::clicked, this, [this, playlist] {
            m_manager->loadSaved(playlist, m_screen);
            accept();
        });
        connect(remove, &QPushButton::clicked, this, [this, id, playlist] {
            if (QMessageBox::question(this, QStringLiteral("删除"),
                                      QStringLiteral("“%1”").arg(playlist.name)) == QMessageBox::Yes) {
                m_manager->deleteSaved(id);
            }
        });
    }
}

} // namespace Mirage
