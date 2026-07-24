#pragma once

#include "Services/PlaylistManager.h"
#include "Services/WallpaperLibrary.h"
#include "Services/WEProject.h"

#include <QHash>
#include <QWidget>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace Mirage {

class ExplorerBottomBarWidget : public QWidget {
    Q_OBJECT

public:
    explicit ExplorerBottomBarWidget(PlaylistManager* playlistManager,
                                     WallpaperLibrary* library,
                                     QWidget* parent = nullptr);

    void setSelectedWallpaper(const Wallpaper& wallpaper);
    void setPlayingWallpaper(int screen, const Wallpaper& wallpaper);

signals:
    void importRequested();
    void playItemRequested(const Mirage::Wallpaper& wallpaper, int screen);
    void addWallpaperRequested(int screen);

private:
    void rebuildStrip();
    void updateHeader();
    void setCollapsed(bool collapsed);
    int targetScreen() const;
    void openLoadDialog();
    void openSaveDialog();
    void openSettingsDialog();
    void clearPlaylist();
    void removeSelected();
    void addCurrentSelection();

    PlaylistManager* m_manager = nullptr;
    WallpaperLibrary* m_library = nullptr;
    Wallpaper m_selectedWallpaper;
    QHash<int, QString> m_playingByScreen;

    QPushButton* m_collapse = nullptr;
    QLabel* m_title = nullptr;
    QComboBox* m_screen = nullptr;
    QPushButton* m_load = nullptr;
    QPushButton* m_save = nullptr;
    QPushButton* m_configure = nullptr;
    QPushButton* m_add = nullptr;
    QWidget* m_body = nullptr;
    QListWidget* m_strip = nullptr;
    QLabel* m_empty = nullptr;
    QPushButton* m_remove = nullptr;
    QPushButton* m_clear = nullptr;
    QString m_selectedItemID;
    bool m_collapsed = false;
};

} // namespace Mirage
