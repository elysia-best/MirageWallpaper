# MirageQt

Linux Qt 6 implementation of Mirage Wallpaper.

The source tree mirrors the macOS app:

- `Sources/App` contains the Qt application entry point.
- `Sources/ContentView` contains the main installed-library and workshop UI.
- `Sources/Services` contains the Wallpaper Engine project parser, library scanner, settings, Steam Web API, SteamCMD and renderer process controller.
- `Sources/SettingsView` contains global settings.
- `Sources/SteamSetup` contains SteamCMD setup and login UI.

Dynamic desktop wallpapers are applied through the mirage-display protocol
(the vendored `MirageLinuxDisplay` library): MirageQt hosts the display broker,
and the `SceneWallpaper` / `VideoWallpaper` / `WebWallpaper` renderer processes export frames to
the desktop environment's display adapter. The consumer adapter is the KDE
Plasma wallpaper plugin, so applying a live wallpaper requires a Plasma session
(either X11 or Wayland). MirageQt resolves `SceneWallpaper`, `VideoWallpaper`, and `WebWallpaper`
beside the application first, then from their repository build directories
during development.

Web wallpapers require the QtWebEngine runtime and the same Vulkan-capable
mirage-display/Plasma environment as scene wallpapers.

Build:

The mirage-display integration library is vendored in-tree under
`Vendors/MirageLinuxDisplay/` (pinned snapshot, see its README for the upstream
commit); no external `MirageLinuxDisplay` checkout is required.

```sh
cmake -S VideoRenderer -B VideoRenderer/build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build VideoRenderer/build/release

cmake -S WebRenderer -B WebRenderer/build/linux-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DWEBRENDERER_BUILD_VIEWER=OFF
cmake --build WebRenderer/build/linux-release

cmake -S MirageQt -B MirageQt/build -G Ninja
cmake --build MirageQt/build
```
