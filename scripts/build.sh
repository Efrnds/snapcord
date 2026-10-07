#!/usr/bin/env bash
# Builds Snapcord on Linux and refreshes the entry in the application menu.
#   ./scripts/build.sh              -> Debug build + install to app list
#   ./scripts/build.sh release      -> Release build + install to app list
#   ./scripts/build.sh release --run -> build, install, and launch
set -euo pipefail

config="debug"
run=0

for arg in "$@"; do
  case "$arg" in
    debug|release) config="$arg" ;;
    --run|-r) run=1 ;;
    -h|--help)
      sed -n '2,6p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "Unknown argument: $arg" >&2
      echo "Usage: $0 [debug|release] [--run]" >&2
      exit 1
      ;;
  esac
done

export QT_ROOT_DIR="${QT_ROOT_DIR:-/usr}"
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
export VCPKG_DEFAULT_BINARY_CACHE="${VCPKG_DEFAULT_BINARY_CACHE:-$HOME/.cache/vcpkg}"
mkdir -p "$VCPKG_DEFAULT_BINARY_CACHE"

if [[ ! -x "$VCPKG_ROOT/vcpkg" ]]; then
  echo "error: vcpkg not found at $VCPKG_ROOT" >&2
  echo "Clone and bootstrap it, or set VCPKG_ROOT." >&2
  exit 1
fi

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

cmake --preset "$config"
cmake --build --preset "$config"

"$root/scripts/install-desktop.sh" "$config"

exe="${XDG_BIN_HOME:-$HOME/.local/bin}/Snapcord"
echo "Done: $exe"
if [[ "$run" -eq 1 ]]; then
  exec "$exe"
fi
