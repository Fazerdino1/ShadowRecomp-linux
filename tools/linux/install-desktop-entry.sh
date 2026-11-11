#!/usr/bin/env bash
# Adds ShadowRecomp to the desktop's application menu (KDE, GNOME...): writes
# ~/.local/share/applications/shadowrecomp.desktop pointing at play/shadow of this checkout.
#   tools/linux/install-desktop-entry.sh            install / update
#   tools/linux/install-desktop-entry.sh --remove   remove it again
set -euo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)"
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
ENTRY="$APPS/shadowrecomp.desktop"

if [ "${1:-}" = "--remove" ]; then
  rm -f "$ENTRY"
  echo "Removed $ENTRY"
  exit 0
fi
if [ ! -x "$ROOT/play/shadow" ]; then
  echo "error: $ROOT/play/shadow does not exist yet - run ./build.sh first" >&2
  exit 1
fi
ICON="$ROOT/play/shadow.png"
[ -f "$ICON" ] || cp -f "$ROOT/runtime/assets/emblem.png" "$ICON" 2>/dev/null || cp -f "$ROOT/tools/release/placeholder/emblem.png" "$ICON"
mkdir -p "$APPS"
cat > "$ENTRY" <<ENTRY_EOF
[Desktop Entry]
Type=Application
Name=Shadow the Hedgehog (ShadowRecomp)
Comment=Native recompilation of Shadow the Hedgehog
Exec="$ROOT/play/shadow"
Path=$ROOT/play
Icon=$ICON
Terminal=false
Categories=Game;
ENTRY_EOF
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" >/dev/null 2>&1 || true
echo "Installed $ENTRY"
