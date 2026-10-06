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
cp -R "$build_dir/Snapcord.app" "$stage/"

# Qt frameworks and plugins inside the bundle.
"$QT_ROOT_DIR/bin/macdeployqt" "$stage/Snapcord.app" -always-overwrite

# Without a developer certificate the app gets an ad-hoc signature: Apple Silicon refuses to run unsigned code.
codesign --force --deep --sign - "$stage/Snapcord.app"

ln -s /Applications "$stage/Applications"
dmg="$out_dir/Snapcord-$version-macos-$arch.dmg"
rm -f "$dmg"
hdiutil create -volname Snapcord -srcfolder "$stage" -ov -format UDZO "$dmg"
echo "Created $dmg"
