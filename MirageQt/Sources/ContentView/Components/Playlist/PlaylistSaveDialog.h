#pragma once

#include "Services/PlaylistManager.h"

#include <QDialog>

class QLineEdit;

namespace Mirage {

class PlaylistSaveDialog : public QDialog {
    Q_OBJECT

public:
    explicit PlaylistSaveDialog(PlaylistManager* manager, int screen, QWidget* parent = nullptr);

private:
    void save();

    PlaylistManager* m_manager = nullptr;
    int m_screen = 0;
    QLineEdit* m_name = nullptr;
};

} // namespace Mirage
