#include "ContentView/Components/Playlist/PlaylistSaveDialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace Mirage {

PlaylistSaveDialog::PlaylistSaveDialog(PlaylistManager* manager, int screen, QWidget* parent)
    : QDialog(parent)
    , m_manager(manager)
    , m_screen(screen) {
    setWindowTitle(QStringLiteral("保存播放列表"));
    resize(380, 180);

    auto* title = new QLabel(QStringLiteral("保存播放列表"), this);
    QFont font = title->font();
    font.setPointSize(16);
    font.setBold(true);
    title->setFont(font);

    auto* nameLabel = new QLabel(QStringLiteral("名称"), this);
    m_name = new QLineEdit(this);
    m_name->setText(m_manager->current(m_screen).name);

    auto* note = new QLabel(QStringLiteral("如果已存在相同名称的播放列表，则它将会被覆盖。"), this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: #8b8680;"));

    auto* cancel = new QPushButton(QStringLiteral("取消"), this);
    auto* save = new QPushButton(QStringLiteral("保存"), this);
    save->setProperty("accent", true);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(save);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(title);
    layout->addWidget(nameLabel);
    layout->addWidget(m_name);
    layout->addWidget(note);
    layout->addStretch(1);
    layout->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, &PlaylistSaveDialog::save);
    connect(m_name, &QLineEdit::returnPressed, this, &PlaylistSaveDialog::save);
}

void PlaylistSaveDialog::save() {
    if (m_manager->saveAs(m_name->text(), m_screen).name.isEmpty()) return;
    accept();
}

} // namespace Mirage
