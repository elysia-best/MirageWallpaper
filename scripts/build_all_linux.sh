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
# Finally everything is staged under dist/ and packed into a self-contained
# tar.gz: the MirageQt binary, the SceneWallpaper / VideoWallpaper / WebWallpaper
# renderer binaries, the mirage-display shared library (with the renderer
# RUNPATH rewritten to $ORIGIN), the shared assets directory, and a desktop
# entry — a complete distribution you can extract and run directly.
#
# Usage:
#   scripts/build_all_linux.sh                 full build (release, default) + tar.gz
#   scripts/build_all_linux.sh debug           debug build
#   scripts/build_all_linux.sh renderers       build only the renderers (skip app)
#   scripts/build_all_linux.sh app             build only MirageQt (assumes renderers are ready)
#   scripts/build_all_linux.sh scene|video|web build a single named renderer
#   scripts/build_all_linux.sh package         stage + pack existing artifacts into dist/*.tar.gz
#   scripts/build_all_linux.sh clean           remove all sub-project build dirs and dist
#   scripts/build_all_linux.sh -h|--help
#
# Environment variables:
#   JOBS=N                       parallel build jobs (default: nproc)
#   MIRAGE_STEAM_WEB_API_KEY     optional built-in Steam Web API Key (32 hex chars)
#   MIRAGEQT_BUILD_DIR=PATH      MirageQt build dir override (default: MirageQt/build/{release,debug})
#   PACKAGE_DIR=PATH             staging root for the tar.gz (default: $ROOT_DIR/dist)
#   VERSION=STRING               package version override (default: git describe)
#   QT_PREFIX=PATH               Qt installation prefix (default: qtpaths query)
#   FFMPEG_BIN=PATH              ffmpeg executable to bundle (default: PATH lookup)
#   NO_PACKAGE=1                 build without creating the tar.gz

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
  scripts/build_all_linux.sh                 full build (release, default) + tar.gz
  scripts/build_all_linux.sh debug           debug build
  scripts/build_all_linux.sh renderers       build only the renderers (skip app)
  scripts/build_all_linux.sh app             build only MirageQt (assumes renderers are ready)
  scripts/build_all_linux.sh scene           build SceneRenderer only
  scripts/build_all_linux.sh video           build VideoRenderer only
  scripts/build_all_linux.sh web             build WebRenderer only
  scripts/build_all_linux.sh steam           build SteamService (SteamKit2) bundle only
  scripts/build_all_linux.sh package         stage + pack existing artifacts into dist/*.tar.gz
  scripts/build_all_linux.sh clean           remove all sub-project build dirs and dist
  scripts/build_all_linux.sh -h|--help       show this help

Environment variables:
  JOBS=N                      parallel build jobs (default: nproc)
  MIRAGE_STEAM_WEB_API_KEY    optional built-in Steam Web API Key (32 hex chars)
  MIRAGEQT_BUILD_DIR=PATH     MirageQt build dir override (default: MirageQt/build/{release,debug})
  PACKAGE_DIR=PATH            staging root for the tar.gz (default: <repo>/dist)
  VERSION=STRING              package version override (default: git describe)
  QT_PREFIX=PATH              Qt installation prefix (default: qtpaths query)
  FFMPEG_BIN=PATH             ffmpeg executable to bundle (default: PATH lookup)
  NO_PACKAGE=1                build without creating the tar.gz
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
    bash "$VIDEO_SH" "$CONFIG"
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
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
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

# --- package: stage existing artifacts, copy runtime libraries, and archive ---
package() {
    step "Packaging MirageWallpaper ($CONFIG)"

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

    local qtpaths_bin="$(command -v qtpaths6 || command -v qtpaths-qt6 || true)" qt_prefix
    if [[ -n "${QT_PREFIX:-}" ]]; then
        qt_prefix="$QT_PREFIX"
    else
        [[ -n "$qtpaths_bin" ]] || die "Qt installation prefix is unknown; set QT_PREFIX or install qtpaths6"
        qt_prefix="$("$qtpaths_bin" --query QT_INSTALL_PREFIX)"
    fi
    [[ -d "$qt_prefix" ]] || die "Qt installation prefix does not exist: $qt_prefix"
    qt_query() {
        local key="$1"
        if [[ -n "$qtpaths_bin" ]]; then
            "$qtpaths_bin" --query "$key"
        else
            case "$key" in
                QT_INSTALL_PLUGINS) printf '%s/plugins\n' "$qt_prefix" ;;
                QT_INSTALL_QML) printf '%s/qml\n' "$qt_prefix" ;;
                QT_INSTALL_DATA) printf '%s/share/qt\n' "$qt_prefix" ;;
                QT_INSTALL_TRANSLATIONS) printf '%s/share/qt/translations\n' "$qt_prefix" ;;
                QT_INSTALL_LIBEXECS) printf '%s/libexec\n' "$qt_prefix" ;;
                QT_INSTALL_LIBS) printf '%s/lib\n' "$qt_prefix" ;;
                *) die "QT_PREFIX requires qtpaths for query: $key" ;;
            esac
        fi
    }

    local ffmpeg_bin="${FFMPEG_BIN:-$(command -v ffmpeg || true)}"
    [[ -x "$ffmpeg_bin" ]] || die "ffmpeg executable not found; set FFMPEG_BIN"
    local version="${VERSION:-$(git -C "$ROOT_DIR" describe --tags --always --dirty 2>/dev/null || echo 0.1.0)}"
    version="${version//\//-}"
    local name="MirageWallpaper-${version}-linux-$(uname -m)"
    local pkg_dir="$PACKAGE_DIR/$name"
    rm -rf "$pkg_dir"
    mkdir -p "$pkg_dir"

    # The CI and local builds use the same staging tree; only this function
    # owns runtime copying so the archive does not depend on the build host.
    install -Dm755 "$app_bin" "$pkg_dir/MirageQt"
    install -Dm755 "$scene_bin" "$pkg_dir/SceneWallpaper"
    install -Dm755 "$video_bin" "$pkg_dir/VideoWallpaper"
    install -Dm755 "$web_bin" "$pkg_dir/WebWallpaper"
    install -Dm755 "$ffmpeg_bin" "$pkg_dir/bin/ffmpeg"
    install -Dm644 "$ROOT_DIR/LICENSE" "$pkg_dir/LICENSE"
    cp -a "$ROOT_DIR/assets" "$pkg_dir/assets"
    install -Dm644 "$ROOT_DIR/MirageQt/Sources/Resources/mirage.png" "$pkg_dir/mirageqt.png"
    sed -e 's|@MIRAGEQT_EXECUTABLE@|MirageQt|' \
        -e 's|^Icon=.*|Icon=mirageqt.png|' \
        "$ROOT_DIR/MirageQt/Sources/Resources/mirageqt.desktop.in" > "$pkg_dir/mirageqt.desktop"
    install -d "$pkg_dir/SteamService"
    cp -a "$STEAM_SERVICE_DIR" "$pkg_dir/SteamService/linux-x64"

    # Qt's shared libraries, QML imports, plugins, WebEngine resources, and
    # helper are runtime inputs; qtpaths supplies their actual installation
    # prefix for both Homebrew and system Qt installations.
    local qt_plugins qt_qml qt_resources qt_translations qt_libexec qt_libs
    qt_plugins="$(qt_query QT_INSTALL_PLUGINS)"
    qt_qml="$(qt_query QT_INSTALL_QML)"
    qt_resources="$(qt_query QT_INSTALL_DATA)/resources"
    qt_translations="$(qt_query QT_INSTALL_TRANSLATIONS)"
    qt_libexec="$(qt_query QT_INSTALL_LIBEXECS)"
    qt_libs="$(qt_query QT_INSTALL_LIBS)"
    install -d "$pkg_dir/share/qt"
    cp -aL "$qt_plugins" "$pkg_dir/"
    cp -aL "$qt_qml" "$pkg_dir/"
    cp -aL "$qt_resources" "$pkg_dir/share/qt/"
    cp -aL "$qt_translations" "$pkg_dir/share/qt/"
    install -Dm755 "$qt_libexec/QtWebEngineProcess" "$pkg_dir/libexec/QtWebEngineProcess"
    command -v patchelf >/dev/null 2>&1 || die "patchelf is required for portable runtime RPATHs"
    cat > "$pkg_dir/qt.conf" <<'EOF'
[Paths]
Prefix=.
Libraries=lib
Plugins=plugins
QmlImports=qml
Qml2Imports=qml
Data=share/qt
Translations=share/qt/translations
LibraryExecutables=libexec
EOF

    # Copy every non-glibc ELF dependency from the build host, then reject any
    # unresolved or conflicting library so the archive remains self-contained.
    install -d "$pkg_dir/lib"
    runtime_library_path="$pkg_dir/lib:$qt_libs:$qt_prefix/lib"
    while IFS= read -r -d '' elf; do
        local elf_header elf_type dependencies
        elf_header="$(readelf -h "$elf" 2>/dev/null)" || continue
        elf_type="$(awk -F: '/^[[:space:]]*Type:/ { gsub(/^[[:space:]]+/, "", $2); print $2; exit }' <<< "$elf_header")"
        [[ "$elf_type" == EXEC* || "$elf_type" == DYN* ]] || continue
        if ! dependencies="$(LD_LIBRARY_PATH="$runtime_library_path${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ldd "$elf" 2>/dev/null)"; then
            die "failed to inspect runtime dependencies for $elf"
        fi
        while IFS= read -r absolute_needed; do
            [[ -n "$absolute_needed" ]] || continue
            [[ -f "$absolute_needed" ]] || die "absolute runtime dependency is unavailable: $absolute_needed"
            local needed_name="$(basename "$absolute_needed")"
            local needed_destination="$pkg_dir/lib/$needed_name"
            if [[ ! -e "$needed_destination" ]]; then
                install -Dm755 "$absolute_needed" "$needed_destination"
            fi
            patchelf --replace-needed "$absolute_needed" "$needed_name" "$elf"
        done < <(readelf -d "$elf" | awk -F'[][]' '/NEEDED/ && $2 ~ /^\// { print $2 }')
        grep -q 'not found' <<< "$dependencies" && die "unresolved runtime dependency in $elf"
        while IFS= read -r dependency; do
            case "$(basename "$dependency")" in
                libc.so.*|libm.so.*|libdl.so.*|libpthread.so.*|librt.so.*|ld-linux*.so.*|linux-vdso.so.*) continue ;;
            esac
            local destination="$pkg_dir/lib/$(basename "$dependency")"
            if [[ -e "$destination" ]]; then
                continue
            else
                install -Dm755 "$dependency" "$destination"
            fi
        done < <(awk '
            $2 == "=>" && substr($3, 1, 1) == "/" { print $3 }
            substr($1, 1, 1) == "/" && ($2 ~ /^\(0x/ || $2 == "=>") { print $1 }
        ' <<< "$dependencies")
    done < <(find "$pkg_dir" -type f -print0)

    while IFS= read -r -d '' elf; do
        local elf_header elf_type relative_lib dependencies
        elf_header="$(readelf -h "$elf" 2>/dev/null)" || continue
        elf_type="$(awk -F: '/^[[:space:]]*Type:/ { gsub(/^[[:space:]]+/, "", $2); print $2; exit }' <<< "$elf_header")"
        [[ "$elf_type" == EXEC* || "$elf_type" == DYN* ]] || continue
        relative_lib="$(realpath --relative-to="$(dirname "$elf")" "$pkg_dir/lib")"
        patchelf --set-rpath "\$ORIGIN:\$ORIGIN/$relative_lib" "$elf"
        while IFS= read -r absolute_needed; do
            [[ -n "$absolute_needed" ]] || continue
            patchelf --replace-needed "$absolute_needed" "$(basename "$absolute_needed")" "$elf"
        done < <(readelf -d "$elf" | awk -F'[][]' '/NEEDED/ && $2 ~ /^\// { print $2 }')
        if ! dependencies="$(LD_LIBRARY_PATH="$runtime_library_path${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ldd "$elf" 2>/dev/null)"; then
            die "failed to verify bundled runtime dependencies for $elf"
        fi
        grep -q 'not found' <<< "$dependencies" && die "unresolved bundled runtime dependency in $elf"
    done < <(find "$pkg_dir" -type f -print0)

    cat > "$pkg_dir/run.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PATH="$root/bin:$PATH"
export QT_PLUGIN_PATH="$root/plugins"
export QML2_IMPORT_PATH="$root/qml"
export QTWEBENGINEPROCESS_PATH="$root/libexec/QtWebEngineProcess"
export QTWEBENGINE_RESOURCES_PATH="$root/share/qt/resources"
export QTWEBENGINE_LOCALES_PATH="$root/share/qt/translations/qtwebengine_locales"
exec "$root/MirageQt" "$@"
EOF
    chmod 755 "$pkg_dir/run.sh"
    local tarball="$PACKAGE_DIR/$name.tar.gz"
    (cd "$PACKAGE_DIR" && tar -czf "$name.tar.gz" "$name")
    good "artifact: $tarball"
}

# --- dispatch ---
case "$TARGET" in
    scene)     build_scene ;;
    video)     build_video ;;
    web)       build_web ;;
    renderers) build_renderers ;;
    app)       build_app ;;
    steam)     build_steam_service ;;
    package)   package ;;
    clean)     clean_all ;;
    all)       build_renderers; build_app; build_steam_service; [[ "${NO_PACKAGE:-0}" == "1" ]] || package ;;
esac

if [[ "$TARGET" == "all" || "$TARGET" == "app" ]]; then
    step "Build complete"
    good "MirageQt binary: $APP_BUILD_DIR/bin/MirageQt"
    if [[ "$TARGET" == "all" && "${NO_PACKAGE:-0}" != "1" ]]; then
        good "tarball: $PACKAGE_DIR/MirageWallpaper-*-linux-*.tar.gz"
    fi
else
    good "done."
fi
