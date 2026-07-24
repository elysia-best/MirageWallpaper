#pragma once

#include "Services/PlaylistManager.h"

#include <QDialog>
#include <QHash>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;

namespace Mirage {

class PlaylistSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit PlaylistSettingsDialog(PlaylistManager* manager, int screen, QWidget* parent = nullptr);

private:
    void loadFromManager();
    void commit();
    void updateTimingPanels();
    void toggleAnchor(int hour);

    PlaylistManager* m_manager = nullptr;
    int m_screen = 0;
    PlaylistSettings m_settings;

    QComboBox* m_order = nullptr;
    QComboBox* m_timing = nullptr;
    QWidget* m_timerPanel = nullptr;
    QWidget* m_daytimePanel = nullptr;
    QWidget* m_dayOfWeekPanel = nullptr;
    QSpinBox* m_timerHours = nullptr;
    QSpinBox* m_timerMinutes = nullptr;
    QComboBox* m_transition = nullptr;
    QDoubleSpinBox* m_transitionSeconds = nullptr;
    QCheckBox* m_alwaysBeginFirst = nullptr;
    QCheckBox* m_introOnStartup = nullptr;
    QCheckBox* m_videoSequence = nullptr;
    QCheckBox* m_updateOnPause = nullptr;
    QHash<int, QPushButton*> m_anchorButtons;
};

} // namespace Mirage
