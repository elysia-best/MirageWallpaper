#!/usr/bin/env bash
#
# MirageWallpaper — one-shot build + packaging script for Linux.
#
# Builds all Linux components in dependency order:
#   1. SceneRenderer → SceneWallpaper   (linux-clang-{config} CMake preset)
#   2. VideoRenderer → VideoWallpaper   (release|debug CMake preset)
#   3. WebRenderer   → WebWallpaper     (CMake, QtWebEngine + Vulkan)
#   4. MirageQt      → MirageQt app     (CMake, Qt 6.8+)
#
# Finally everything is staged in an AppDir and converted to an AppImage. The
# host's graphics driver stack remains a system dependency.
#
# Usage:
#   scripts/build_all_linux.sh                 full build (release, default) + AppImage
#   scripts/build_all_linux.sh debug           debug build
#   scripts/build_all_linux.sh renderers       build only the renderers (skip app)
#   scripts/build_all_linux.sh app             build only MirageQt (assumes renderers are ready)
#   scripts/build_all_linux.sh scene|video|web build a single named renderer
#   scripts/build_all_linux.sh package         stage + pack existing artifacts into dist/*.AppImage
#   scripts/build_all_linux.sh clean           remove all sub-project build dirs and dist
#   scripts/build_all_linux.sh -h|--help
#
# Environment variables:
#   JOBS=N                       parallel build jobs (default: nproc)
#   MIRAGE_STEAM_WEB_API_KEY     optional built-in Steam Web API Key (32 hex chars)
#   MIRAGEQT_BUILD_DIR=PATH      MirageQt build dir override (default: MirageQt/build/{release,debug})
#   PACKAGE_DIR=PATH             output/staging root for the AppImage (default: $ROOT_DIR/dist)
#   VERSION=STRING               package version override (default: git describe)
#   FFMPEG_BIN=PATH              ffmpeg executable to bundle (default: PATH lookup)
#   LINUXDEPLOY=PATH             linuxdeploy executable (default: PATH lookup)
#   LINUXDEPLOY_PLUGIN_QT=PATH   linuxdeploy Qt plugin executable (default: PATH lookup)
#   APPIMAGETOOL=PATH            appimagetool executable (default: download latest)
#   NO_PACKAGE=1                 build without creating the AppImage

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# --- terminal colors (disabled when not a TTY) ---
if [[ -t 1 ]]; then
    C_CYAN=$'\033[1;36m'; C_GRN=$'\033[1;32m'; C_RED=$'\033[1;31m'; C_YLW=$'\033[1;33m'; C_MAG=$'\033[1;35m'; C_OFF=$'\033[0m'
else
    C_CYAN=''; C_GRN=''; C_RED=''; C_YLW=''; C_MAG=''; C_OFF=''
fi
step() { printf '\n%s========== %s ==========%s\n' "$C_MAG" "$*" "$C_OFF"; }
info() { printf '%s==>%s %s\n' "$C_CYAN" "$C_OFF" "$*"; }
good() { printf '%sOK:%s %s\n'  "$C_GRN" "$C_OFF" "$*"; }
warn() { printf '%sWARN:%s %s\n' "$C_YLW" "$C_OFF" "$*" >&2; }
die()  { printf '%sERROR:%s %s\n' "$C_RED" "$C_OFF" "$*" >&2; exit 1; }

usage() {
    cat <<'EOF'
MirageWallpaper one-shot build + packaging script for Linux.

Usage:
  scripts/build_all_linux.sh                 full build (release, default) + AppImage
  scripts/build_all_linux.sh debug           debug build
  scripts/build_all_linux.sh renderers       build only the renderers (skip app)
  scripts/build_all_linux.sh app             build only MirageQt (assumes renderers are ready)
  scripts/build_all_linux.sh scene           build SceneRenderer only
  scripts/build_all_linux.sh video           build VideoRenderer only
  scripts/build_all_linux.sh web             build WebRenderer only
  scripts/build_all_linux.sh steam           build SteamService (SteamKit2) bundle only
  scripts/build_all_linux.sh package         stage + pack existing artifacts into dist/*.AppImage
  scripts/build_all_linux.sh clean           remove all sub-project build dirs and dist
  scripts/build_all_linux.sh -h|--help       show this help

Environment variables:
  JOBS=N                      parallel build jobs (default: nproc)
  MIRAGE_STEAM_WEB_API_KEY    optional built-in Steam Web API Key (32 hex chars)
  MIRAGEQT_BUILD_DIR=PATH     MirageQt build dir override (default: MirageQt/build/{release,debug})
  PACKAGE_DIR=PATH            output/staging root for the AppImage (default: <repo>/dist)
  VERSION=STRING              package version override (default: git describe)
  FFMPEG_BIN=PATH             ffmpeg executable to bundle (default: PATH lookup)
  LINUXDEPLOY=PATH            linuxdeploy executable (default: PATH lookup)
  LINUXDEPLOY_PLUGIN_QT=PATH  linuxdeploy Qt plugin executable (default: PATH lookup)
  APPIMAGETOOL=PATH           appimagetool executable (default: download latest)
  NO_PACKAGE=1                build without creating the AppImage
EOF
}

# --- argument parsing ---
TARGET="all"
CONFIG="release"      # lowercase preset name passed to renderers (release|debug)
while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help) usage; exit 0 ;;
        all|renderers|app|scene|video|web|steam|package|clean) TARGET="$1"; shift ;;
        release|debug) CONFIG="$1"; shift ;;
        *) die "unknown argument: $1 (try --help)" ;;
    esac
done

# CMake expects a capitalised build type for the MirageQt app.
if [[ "$CONFIG" == "debug" ]]; then
    CMAKE_CONFIG="Debug"
else
    CMAKE_CONFIG="Release"
fi

# --- platform check ---
[[ "$(uname -s)" == "Linux" ]] || die "This script only supports Linux."

SCENE_SH="$ROOT_DIR/SceneRenderer/scripts/build.sh"
VIDEO_SH="$ROOT_DIR/VideoRenderer/scripts/build.sh"

for s in "$SCENE_SH" "$VIDEO_SH"; do
    [[ -f "$s" ]] || die "missing sub-script: $s"
done

JOBS="${JOBS:-$(nproc 2>/dev/null || echo 8)}"

# MirageQt has no sub-build-script; it is built directly with CMake into a
# config-specific directory so a dev build in MirageQt/build is never touched.
MIRAGEQT_DIR="$ROOT_DIR/MirageQt"
APP_BUILD_DIR="${MIRAGEQT_BUILD_DIR:-$MIRAGEQT_DIR/build/$CONFIG}"
PACKAGE_DIR="${PACKAGE_DIR:-$ROOT_DIR/dist}"
WEBRENDERER_DIR="$ROOT_DIR/WebRenderer"
WEB_BUILD_DIR="$WEBRENDERER_DIR/build/linux-$CONFIG"

# --- per-component build helpers ---
build_scene() {
    step "Building SceneRenderer ($CONFIG)"
    # The Linux distribution contains SceneWallpaper, not the optional GLFW
    # SceneViewer; disabling the viewer avoids requiring platform OpenGL SDK
    # headers that are unrelated to the packaged runtime.
    SCENERENDERER_BUILD_VIEWER=OFF bash "$SCENE_SH" "$CONFIG"
}
build_video() {
    step "Building VideoRenderer ($CONFIG)"
    VIDEORENDERER_BUILD_VIEWER=OFF bash "$VIDEO_SH" "$CONFIG"
}
build_web() {
    step "Building WebRenderer ($CONFIG)"
    info "cmake -S $WEBRENDERER_DIR -B $WEB_BUILD_DIR (jobs=$JOBS)"
    cmake -S "$WEBRENDERER_DIR" -B "$WEB_BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE="$CMAKE_CONFIG" \
        -DWEBRENDERER_BUILD_VIEWER=OFF
    cmake --build "$WEB_BUILD_DIR" --parallel "$JOBS"
    [[ -x "$WEB_BUILD_DIR/Tools/WebWallpaper/WebWallpaper" ]] || \
        die "WebWallpaper binary not produced at $WEB_BUILD_DIR/Tools/WebWallpaper/WebWallpaper"
    good "WebWallpaper binary: $WEB_BUILD_DIR/Tools/WebWallpaper/WebWallpaper"
}
build_renderers() {
    build_scene
    build_video
    build_web
}
build_app() {
    step "Building MirageQt ($CMAKE_CONFIG)"
    info "cmake -S $MIRAGEQT_DIR -B $APP_BUILD_DIR (jobs=$JOBS)"
    cmake -S "$MIRAGEQT_DIR" -B "$APP_BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE="$CMAKE_CONFIG" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DMIRAGEQT_RUNTIME_DIR=/usr/libexec/miragewallpaper \
        -DMIRAGEQT_DATA_DIR=/usr/share/miragewallpaper
    cmake --build "$APP_BUILD_DIR" --parallel "$JOBS"
    # MirageQt explicitly emits runtime targets under build/bin; package and
    # build verification must use that CMake-owned location consistently.
    [[ -x "$APP_BUILD_DIR/bin/MirageQt" ]] || die "MirageQt binary not produced at $APP_BUILD_DIR/bin/MirageQt"
    good "MirageQt binary: $APP_BUILD_DIR/bin/MirageQt"
}

# SteamService (SteamKit2) bundle: built by scripts/build_steam_service_linux.sh
# into SteamService/build/linux-x64; the app locates it via the same layout
# the macOS bundle uses (app/ + runtime/ + Licenses/).
STEAM_SERVICE_DIR="$ROOT_DIR/SteamService/build/linux-x64"
build_steam_service() {
    step "Building SteamService ($CONFIG)"
    bash "$ROOT_DIR/scripts/build_steam_service_linux.sh" "$STEAM_SERVICE_DIR"
}

# --- clean: remove all sub-project build dirs + dist ---
clean_all() {
    step "Cleaning all build directories"
    bash "$SCENE_SH" clean "$CONFIG" || true
    bash "$VIDEO_SH" clean "$CONFIG" || true
    if [[ -d "$WEB_BUILD_DIR" ]]; then
        info "Removing $WEB_BUILD_DIR"
        rm -rf "$WEB_BUILD_DIR"
    fi
    if [[ -d "$APP_BUILD_DIR" ]]; then
        info "Removing $APP_BUILD_DIR"
        rm -rf "$APP_BUILD_DIR"
    fi
    if [[ -d "$STEAM_SERVICE_DIR" ]]; then
        info "Removing $STEAM_SERVICE_DIR"
        rm -rf "$STEAM_SERVICE_DIR"
    fi
    if [[ -d "$PACKAGE_DIR" ]]; then
        info "Removing $PACKAGE_DIR"
        rm -rf "$PACKAGE_DIR"
    fi
}

# --- artifact lookup helpers ---
first_existing() {
    local p
    for p in "$@"; do
        if [[ -x "$p" ]]; then echo "$p"; return 0; fi
    done
    return 1
}

find_display_lib_dir() {
    # Roots where MirageLinuxDisplay is compiled in-tree.
    local roots=(
        "$ROOT_DIR/SceneRenderer/build/linux-clang-$CONFIG"
        "$ROOT_DIR/SceneRenderer/build"
        "$ROOT_DIR/VideoRenderer/build/$CONFIG"
        "$ROOT_DIR/VideoRenderer/build"
        "$WEB_BUILD_DIR"
        "$ROOT_DIR/WebRenderer/build"
        "$APP_BUILD_DIR"
        "$ROOT_DIR/MirageQt/build"
    )
    local r
    for r in "${roots[@]}"; do
        if [[ -e "$r/MirageLinuxDisplay/src/libmirage_display.so.0" ]]; then
            echo "$r/MirageLinuxDisplay/src"
            return 0
        fi
    done
    return 1
}

# --- package: stage existing artifacts and let linuxdeploy build the AppImage ---
package_linuxdeploy() {
    step "Packaging MirageWallpaper with linuxdeploy ($CONFIG)"

    local scene_bin video_bin web_bin app_bin
    scene_bin="$(first_existing "$ROOT_DIR/SceneRenderer/build/linux-clang-$CONFIG/Tools/SceneWallpaper/SceneWallpaper" \
        "$ROOT_DIR/SceneRenderer/build/release/Tools/SceneWallpaper/SceneWallpaper" \
        "$ROOT_DIR/SceneRenderer/build/debug/Tools/SceneWallpaper/SceneWallpaper")" || die "SceneWallpaper binary not found"
    video_bin="$(first_existing "$ROOT_DIR/VideoRenderer/build/$CONFIG/Tools/VideoWallpaper/VideoWallpaper" \
        "$ROOT_DIR/VideoRenderer/build/release/Tools/VideoWallpaper/VideoWallpaper" \
        "$ROOT_DIR/VideoRenderer/build/Tools/VideoWallpaper/VideoWallpaper")" || die "VideoWallpaper binary not found"
    web_bin="$(first_existing "$WEB_BUILD_DIR/Tools/WebWallpaper/WebWallpaper" \
        "$ROOT_DIR/WebRenderer/build/linux-release/Tools/WebWallpaper/WebWallpaper" \
        "$ROOT_DIR/WebRenderer/build/linux-debug/Tools/WebWallpaper/WebWallpaper")" || die "WebWallpaper binary not found"
    app_bin="$APP_BUILD_DIR/bin/MirageQt"
    [[ -x "$app_bin" ]] || die "MirageQt binary not found at $app_bin"

    local ffmpeg_bin="${FFMPEG_BIN:-$(command -v ffmpeg || true)}"
    [[ -x "$ffmpeg_bin" ]] || die "ffmpeg executable not found; set FFMPEG_BIN"
    local linuxdeploy_bin="${LINUXDEPLOY:-$(command -v linuxdeploy || true)}"
    local linuxdeploy_qt="${LINUXDEPLOY_PLUGIN_QT:-$(command -v linuxdeploy-plugin-qt || true)}"
    local appimagetool_bin="${APPIMAGETOOL:-$(command -v appimagetool || true)}"
    [[ -x "$linuxdeploy_bin" ]] || die "linuxdeploy executable not found; set LINUXDEPLOY"
    [[ -x "$linuxdeploy_qt" ]] || die "linuxdeploy Qt plugin not found; set LINUXDEPLOY_PLUGIN_QT"
    [[ -x "$appimagetool_bin" ]] || die "appimagetool executable not found; set APPIMAGETOOL"

    local version="${VERSION:-$(git -C "$ROOT_DIR" describe --tags --always --dirty 2>/dev/null || echo 0.1.0)}"
    version="${version//\//-}"
    local name="MirageWallpaper-${version}-linux-$(uname -m)"
    local pkg_dir="$PACKAGE_DIR/$name.AppDir"
    rm -rf "$pkg_dir"
    mkdir -p "$pkg_dir"

    # CMake owns the FHS layout; linuxdeploy scans every native entry point so
    # the package also covers the non-Qt wallpaper hosts and ffmpeg helper.
    cmake --install "${scene_bin%/Tools/SceneWallpaper/SceneWallpaper}" --prefix "$pkg_dir/usr"
    cmake --install "${video_bin%/Tools/VideoWallpaper/VideoWallpaper}" --prefix "$pkg_dir/usr"
    cmake --install "${web_bin%/Tools/WebWallpaper/WebWallpaper}" --prefix "$pkg_dir/usr"
    cmake --install "$APP_BUILD_DIR" --prefix "$pkg_dir/usr"
    install -Dm755 "$ffmpeg_bin" "$pkg_dir/usr/bin/ffmpeg"
    install -Dm644 "$ROOT_DIR/LICENSE" "$pkg_dir/LICENSE"
    install -d "$pkg_dir/usr/libexec/miragewallpaper/SteamService"
    cp -a "$STEAM_SERVICE_DIR" "$pkg_dir/usr/libexec/miragewallpaper/SteamService/linux-x64"

    # Preserve the sibling paths used by the Qt application for packaged tools.
    ln -sfn ../libexec/miragewallpaper/SceneWallpaper "$pkg_dir/usr/bin/SceneWallpaper"
    ln -sfn ../libexec/miragewallpaper/VideoWallpaper "$pkg_dir/usr/bin/VideoWallpaper"
    ln -sfn ../libexec/miragewallpaper/WebWallpaper "$pkg_dir/usr/bin/WebWallpaper"
    ln -sfn ../libexec/miragewallpaper/SteamService "$pkg_dir/usr/bin/SteamService"
    ln -sfn ../share/miragewallpaper/assets "$pkg_dir/usr/bin/assets"

    local desktop_file="$pkg_dir/usr/share/applications/mirageqt.desktop"
    local icon_file="$pkg_dir/usr/share/icons/hicolor/512x512/apps/mirageqt.png"
    [[ -f "$desktop_file" && -f "$icon_file" ]] || die "MirageQt desktop integration files are missing"
    sed -i -e 's|^Exec=.*|Exec=MirageQt|' -e 's|^Icon=.*|Icon=mirageqt|' "$desktop_file"
    install -Dm644 "$desktop_file" "$pkg_dir/mirageqt.desktop"
    install -Dm644 "$icon_file" "$pkg_dir/mirageqt.png"

    export QMAKE="$(command -v qmake6 || command -v qmake || true)"
    [[ -n "$QMAKE" ]] || die "qmake is required by linuxdeploy-plugin-qt"
    # QML is compiled into the executable's resources, so the Qt deploy plugin
    # scans the sources to discover runtime imports such as QtQuick.Controls.
    export QML_SOURCES_PATHS="$ROOT_DIR/MirageQt/Sources/Qml"
    # QPA plugins are loaded dynamically; explicitly deploy Wayland so the
    # AppImage can use compositor screen metadata rather than falling back to X11.
    export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so"
    # These Qt modules are also dynamically loaded and are not found through
    # MirageQt's ELF dependencies; SVG is used by the UI and Wayland support
    # is required for the compositor-backed application session.
    export EXTRA_QT_MODULES="waylandcompositor;svg"
    export PATH="$(dirname "$linuxdeploy_qt"):$(dirname "$appimagetool_bin"):$PATH"
    info "linuxdeploy is bundling Qt, WebEngine, and dependencies of all wallpaper hosts"
    (
        cd "$PACKAGE_DIR"
        APPIMAGE_EXTRACT_AND_RUN=1 "$linuxdeploy_bin" \
            --appdir "$pkg_dir" \
            --executable "$pkg_dir/usr/bin/MirageQt" \
            --executable "$pkg_dir/usr/bin/ffmpeg" \
            --executable "$pkg_dir/usr/libexec/miragewallpaper/SceneWallpaper" \
            --executable "$pkg_dir/usr/libexec/miragewallpaper/VideoWallpaper" \
            --executable "$pkg_dir/usr/libexec/miragewallpaper/WebWallpaper" \
            --executable "$pkg_dir/usr/libexec/miragewallpaper/SteamService/linux-x64/runtime/dotnet" \
            --desktop-file "$pkg_dir/mirageqt.desktop" \
            --icon-file "$pkg_dir/mirageqt.png" \
            --plugin qt \
            --output appimage
    )
    local generated_appimage
    generated_appimage="$(find "$PACKAGE_DIR" -maxdepth 1 -type f -name 'MirageQt-*.AppImage' -print -quit)"
    [[ -n "$generated_appimage" ]] || die "linuxdeploy did not create a MirageQt AppImage in $PACKAGE_DIR"
    mv -f "$generated_appimage" "$PACKAGE_DIR/$name.AppImage"
    [[ -x "$PACKAGE_DIR/$name.AppImage" ]] || die "AppImage was not created: $PACKAGE_DIR/$name.AppImage"
    rm -rf "$pkg_dir"
    good "artifact: $PACKAGE_DIR/$name.AppImage"
}

# Preserve the package function name used by callers that source this script.
package() {
    package_linuxdeploy "$@"
}

# --- dispatch ---
case "$TARGET" in
    scene)     build_scene ;;
    video)     build_video ;;
    web)       build_web ;;
    renderers) build_renderers ;;
    app)       build_app ;;
    steam)     build_steam_service ;;
    package)   package_linuxdeploy ;;
    clean)     clean_all ;;
    all)       build_renderers; build_app; build_steam_service; [[ "${NO_PACKAGE:-0}" == "1" ]] || package_linuxdeploy ;;
esac

if [[ "$TARGET" == "all" || "$TARGET" == "app" ]]; then
    step "Build complete"
    good "MirageQt binary: $APP_BUILD_DIR/bin/MirageQt"
    if [[ "$TARGET" == "all" && "${NO_PACKAGE:-0}" != "1" ]]; then
        good "AppImage: $PACKAGE_DIR/MirageWallpaper-*-linux-*.AppImage"
    fi
else
    good "done."
fi
