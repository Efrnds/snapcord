#!/usr/bin/env bash
# Builds a self-contained AppDir (binary + Qt/libs via linuxdeploy) under dist/AppDir.
# Used by package.sh (AppImage) and package-deb.sh (.deb).
#   source this file, then: prepare_appdir <build_dir> <out_dir>
# Needs QT_ROOT_DIR (qmake path for the Qt plugin).
set -euo pipefail

prepare_appdir() {
    local build_dir="$1"
    local out_dir="$2"
    local root version app_id appdir tools

    root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
    version="$(sed -nE 's/^[[:space:]]*VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$root/CMakeLists.txt" | head -n 1)"
    app_id="io.github.pedrordgsr.Snapcord"

    mkdir -p "$out_dir"
    out_dir="$(cd "$out_dir" && pwd)"
    appdir="$out_dir/AppDir"
    rm -rf "$appdir"
    DESTDIR="$appdir" cmake --install "$build_dir" --prefix /usr

    tools="$out_dir/tools"
    mkdir -p "$tools"
    for tool in linuxdeploy linuxdeploy-plugin-qt; do
        if [ ! -x "$tools/$tool" ]; then
            curl -fsSL -o "$tools/$tool" \
                "https://github.com/linuxdeploy/$tool/releases/download/continuous/$tool-x86_64.AppImage"
            chmod +x "$tools/$tool"
        fi
    done
    export PATH="$tools:$PATH"

    # Run the tools without FUSE (not available on CI machines and some distributions).
    export APPIMAGE_EXTRACT_AND_RUN=1
    export QMAKE="${QMAKE:-$QT_ROOT_DIR/bin/qmake}"
    # Native Wayland support when the Qt installation has it; X11 (also used through XWayland) is always included.
    if [ -f "$QT_ROOT_DIR/plugins/platforms/libqwayland-generic.so" ]; then
        export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so"
    fi

    # Fill AppDir with Qt plugins and dependent libraries (no AppImage yet).
    # snapcord-video is the direct-file player. It is the binary that links Qt Multimedia,
    # so linuxdeploy must see it or the package will miss the media plugins.
    linuxdeploy --appdir "$appdir" --plugin qt \
        --executable "$appdir/usr/bin/snapcord-video" \
        --desktop-file "$appdir/usr/share/applications/$app_id.desktop" \
        --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/$app_id.png"

    # Export for callers.
    PREPARE_APPDIR_ROOT="$root"
    PREPARE_APPDIR_VERSION="$version"
    PREPARE_APPDIR_APP_ID="$app_id"
    PREPARE_APPDIR_OUT="$out_dir"
    PREPARE_APPDIR_DIR="$appdir"
}
