#!/usr/bin/env bash
# Packages a Release build for Linux as an AppImage, using linuxdeploy and its Qt plugin.
#   packaging/linux/package.sh [build dir] [output dir]
# Needs QT_ROOT_DIR (for qmake, which the Qt plugin uses to find Qt).
set -euo pipefail

build_dir="${1:-build/release}"
out_dir="${2:-dist}"
here="$(cd "$(dirname "$0")" && pwd)"

# shellcheck source=prepare-appdir.sh
source "$here/prepare-appdir.sh"
prepare_appdir "$build_dir" "$out_dir"

export LDAI_OUTPUT="$PREPARE_APPDIR_OUT/Snapcord-$PREPARE_APPDIR_VERSION-linux-x86_64.AppImage"
export OUTPUT="$LDAI_OUTPUT"
export APPIMAGE_EXTRACT_AND_RUN=1

linuxdeploy --appdir "$PREPARE_APPDIR_DIR" --output appimage \
    --desktop-file "$PREPARE_APPDIR_DIR/usr/share/applications/$PREPARE_APPDIR_APP_ID.desktop" \
    --icon-file "$PREPARE_APPDIR_DIR/usr/share/icons/hicolor/256x256/apps/$PREPARE_APPDIR_APP_ID.png"
echo "Created $LDAI_OUTPUT"
