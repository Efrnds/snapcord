#!/usr/bin/env bash
# Packages a Release build for Linux as an AppImage, using linuxdeploy and its Qt plugin.
#   packaging/linux/package.sh [build dir] [output dir]
# Needs QT_ROOT_DIR (for qmake, which the Qt plugin uses to find Qt).
set -euo pipefail

build_dir="${1:-build/release}"
out_dir="${2:-dist}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
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
export QMAKE="$QT_ROOT_DIR/bin/qmake"
# Native Wayland support when the Qt installation has it; X11 (also used through XWayland) is always included.
if [ -f "$QT_ROOT_DIR/plugins/platforms/libqwayland-generic.so" ]; then
    export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so"
fi
export LDAI_OUTPUT="$out_dir/Snapcord-$version-linux-x86_64.AppImage"
export OUTPUT="$LDAI_OUTPUT"

linuxdeploy --appdir "$appdir" --plugin qt --output appimage \
    --desktop-file "$appdir/usr/share/applications/$app_id.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/$app_id.png"
echo "Created $LDAI_OUTPUT"
