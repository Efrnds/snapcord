#!/usr/bin/env bash
# Installs Snapcord into the user application menu (~/.local/share/applications).
# Copies the built binary to a stable path so the menu entry survives rebuilds.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
config="${1:-release}"

built="$root/build/$config/src/app/Snapcord"
if [[ ! -x "$built" ]]; then
  # Older / Windows-style layout fallback
  built="$root/build/$config/Snapcord"
fi
if [[ ! -x "$built" ]]; then
  echo "error: Snapcord binary not found for config '$config'." >&2
  echo "Build first with: ./scripts/build.sh $config" >&2
  exit 1
fi

bin_dir="${XDG_BIN_HOME:-$HOME/.local/bin}"
app_dir="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
icon_dir="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/scalable/apps"

mkdir -p "$bin_dir" "$app_dir" "$icon_dir"

install -m 755 "$built" "$bin_dir/Snapcord"
helper="$(dirname "$built")/snapcord-video"
if [[ -x "$helper" ]]; then
  install -m 755 "$helper" "$bin_dir/snapcord-video"
fi
install -m 644 "$root/resources/icons/snapcord.svg" "$icon_dir/snapcord.svg"

cat > "$app_dir/snapcord.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Snapcord
GenericName=Discord Voice Client
Comment=Native, lightweight Discord client focused on voice calls
Exec=$bin_dir/Snapcord
Icon=snapcord
Terminal=false
Categories=Network;Chat;InstantMessaging;
StartupNotify=true
StartupWMClass=Snapcord
Keywords=discord;voice;chat;
EOF

# Refresh the desktop database when the tool is available (no-op if missing).
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database "$app_dir" >/dev/null 2>&1 || true
fi

echo "Installed to app menu: $app_dir/snapcord.desktop"
echo "Binary: $bin_dir/Snapcord"
echo "Icon:   $icon_dir/snapcord.svg"
