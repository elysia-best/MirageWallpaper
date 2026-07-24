#pragma once

#include "Services/PlaylistManager.h"

#include <QDialog>

class QListWidget;

namespace Mirage {

class PlaylistOpenDialog : public QDialog {
    Q_OBJECT

public:
    explicit PlaylistOpenDialog(PlaylistManager* manager, int screen, QWidget* parent = nullptr);

private:
    void rebuild();

    PlaylistManager* m_manager = nullptr;
    int m_screen = 0;
    QListWidget* m_list = nullptr;
};

} // namespace Mirage
