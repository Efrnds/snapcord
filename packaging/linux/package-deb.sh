#!/usr/bin/env bash
# Packages a Release build for Linux as a .deb with Qt/libs under /opt/snapcord.
#   packaging/linux/package-deb.sh [build dir] [output dir]
# Reuses dist/AppDir when present (e.g. after package.sh); otherwise builds it.
# Needs QT_ROOT_DIR when AppDir must be created, and dpkg-deb (dpkg-dev).
set -euo pipefail

build_dir="${1:-build/release}"
out_dir="${2:-dist}"
here="$(cd "$(dirname "$0")" && pwd)"

if ! command -v dpkg-deb >/dev/null 2>&1; then
    echo "error: dpkg-deb not found (install dpkg-dev)" >&2
    exit 1
fi

# shellcheck source=prepare-appdir.sh
source "$here/prepare-appdir.sh"

mkdir -p "$out_dir"
out_dir="$(cd "$out_dir" && pwd)"
appdir="$out_dir/AppDir"

if [ ! -x "$appdir/usr/bin/Snapcord" ]; then
    prepare_appdir "$build_dir" "$out_dir"
else
    # AppDir already filled (e.g. by package.sh); still need version/app_id.
    PREPARE_APPDIR_ROOT="$(cd "$here/../.." && pwd)"
    PREPARE_APPDIR_VERSION="$(sed -nE 's/^[[:space:]]*VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$PREPARE_APPDIR_ROOT/CMakeLists.txt" | head -n 1)"
    PREPARE_APPDIR_APP_ID="io.github.pedrordgsr.Snapcord"
    PREPARE_APPDIR_OUT="$out_dir"
    PREPARE_APPDIR_DIR="$appdir"
fi

version="$PREPARE_APPDIR_VERSION"
app_id="$PREPARE_APPDIR_APP_ID"
pkg_root="$out_dir/deb-root"
deb_file="$out_dir/Snapcord-$version-linux-amd64.deb"

rm -rf "$pkg_root"
mkdir -p "$pkg_root/opt/snapcord" "$pkg_root/usr/bin" "$pkg_root/usr/share" "$pkg_root/DEBIAN"

# Bundle the AppDir usr tree under /opt (binary, Qt libs, plugins).
cp -a "$appdir/usr/." "$pkg_root/opt/snapcord/"

# Desktop integration stays in the FHS locations the session looks for.
if [ -d "$appdir/usr/share/applications" ]; then
    cp -a "$appdir/usr/share/applications" "$pkg_root/usr/share/"
fi
if [ -d "$appdir/usr/share/icons" ]; then
    cp -a "$appdir/usr/share/icons" "$pkg_root/usr/share/"
fi
if [ -d "$appdir/usr/share/metainfo" ]; then
    cp -a "$appdir/usr/share/metainfo" "$pkg_root/usr/share/"
fi

# Wrapper so Exec=Snapcord in the .desktop file finds the bundled build.
cat > "$pkg_root/usr/bin/Snapcord" <<'EOF'
#!/bin/sh
# Launches the /opt bundle with its own Qt libraries and plugins.
HERE=/opt/snapcord
export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if [ -d "$HERE/lib/qt6/plugins" ]; then
    export QT_PLUGIN_PATH="$HERE/lib/qt6/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
elif [ -d "$HERE/plugins" ]; then
    export QT_PLUGIN_PATH="$HERE/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
fi
if [ -d "$HERE/qml" ]; then
    export QML2_IMPORT_PATH="$HERE/qml${QML2_IMPORT_PATH:+:$QML2_IMPORT_PATH}"
fi
exec "$HERE/bin/Snapcord" "$@"
EOF
chmod 755 "$pkg_root/usr/bin/Snapcord"

# Installed size in KiB for the control file.
installed_size="$(du -sk "$pkg_root" | cut -f1)"

cat > "$pkg_root/DEBIAN/control" <<EOF
Package: snapcord
Version: $version
Section: net
Priority: optional
Architecture: amd64
Installed-Size: $installed_size
Maintainer: Snapcord contributors <noreply@users.noreply.github.com>
Homepage: https://github.com/pedrordgsr/snapcord
Depends: libc6 (>= 2.35), libsecret-1-0, libgl1
Description: Lightweight, voice-focused Discord client
 Snapcord is a native Discord client built with C++ and Qt, without an
 embedded browser. It focuses on voice calls (DAVE E2EE), with chat and
 direct messages as supporting features.
 .
 Snapcord is not affiliated with Discord. Using third-party clients
 violates Discord's Terms of Service and may get your account banned.
EOF

# Prefer xz compression when available (default on modern dpkg).
dpkg-deb --root-owner-group -b "$pkg_root" "$deb_file"
echo "Created $deb_file"
