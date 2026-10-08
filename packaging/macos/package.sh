#!/usr/bin/env bash
# Packages a Release build for macOS as a .dmg with the usual "drag to Applications" layout.
#   packaging/macos/package.sh [build dir] [output dir]
# Needs QT_ROOT_DIR (for macdeployqt).
set -euo pipefail

build_dir="${1:-build/release}"
out_dir="${2:-dist}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
version="$(sed -nE 's/^[[:space:]]*VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$root/CMakeLists.txt" | head -n 1)"
arch="$(uname -m)"

mkdir -p "$out_dir"
stage="$out_dir/dmg"
rm -rf "$stage"
mkdir -p "$stage"

# CMake puts the bundle under src/app/ when building in-tree; allow either layout.
app="$build_dir/Snapcord.app"
if [ ! -d "$app" ]; then
    app="$build_dir/src/app/Snapcord.app"
fi
if [ ! -d "$app" ]; then
    echo "error: Snapcord.app not found under $build_dir" >&2
    exit 1
fi
cp -R "$app" "$stage/"

# The video player sits beside Snapcord inside the bundle. macdeployqt only inspects the
# main executable unless told about this one, and that is what pulls in Qt Multimedia.
player="$stage/Snapcord.app/Contents/MacOS/snapcord-video"
if [ ! -x "$player" ]; then
    src_player="$build_dir/src/app/snapcord-video"
    if [ ! -x "$src_player" ]; then
        src_player="$build_dir/snapcord-video"
    fi
    cp "$src_player" "$player"
fi

# Qt frameworks and plugins inside the bundle.
"$QT_ROOT_DIR/bin/macdeployqt" "$stage/Snapcord.app" -always-overwrite \
    -executable="$player"

# Without a developer certificate the app gets an ad-hoc signature: Apple Silicon refuses to run unsigned code.
codesign --force --deep --sign - "$stage/Snapcord.app"

ln -s /Applications "$stage/Applications"
dmg="$out_dir/Snapcord-$version-macos-$arch.dmg"
rm -f "$dmg"
hdiutil create -volname Snapcord -srcfolder "$stage" -ov -format UDZO "$dmg"
echo "Created $dmg"
