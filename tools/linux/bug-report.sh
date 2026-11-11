#!/usr/bin/env bash
# Plays the game once with its sound diagnostics switched on and packs everything needed to study a sound / gameplay
# bug into ONE file to send: the log, the settings used, and the sound the game produced (compressed).
#
#   tools/linux/bug-report.sh              play with your current settings
#   tools/linux/bug-report.sh --defaults   play with the default settings (yours are put back afterwards)
#
# Play until the bug shows, let it last ~20 seconds, then quit the game. The result is shadow-bug-report.tar.gz in
# the project folder.
set -uo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)"
PLAY="$ROOT/play"
[ -x "$PLAY/shadow" ] || { echo "error: $PLAY/shadow does not exist - run ./build.sh first" >&2; exit 1; }
OUT="$ROOT/bug-report"
rm -rf "$OUT"; mkdir -p "$OUT"

DEFAULTS=0
[ "${1:-}" = "--defaults" ] && DEFAULTS=1
INI="$PLAY/shadow_settings.ini"
if [ $DEFAULTS = 1 ] && [ -f "$INI" ]; then
  mv "$INI" "$INI.bug-report-backup"
  trap 'if [ -f "$INI.bug-report-backup" ]; then cp -f "$INI" "$OUT/settings-used.ini" 2>/dev/null; mv -f "$INI.bug-report-backup" "$INI"; fi' EXIT
  echo "Playing with the default settings (your own are restored when the game closes)."
fi
[ -f "$INI" ] && cp -f "$INI" "$OUT/settings-at-start.ini"

echo "Starting the game. Play until the bug shows, wait ~20 seconds, then quit."
( cd "$PLAY" && AUDIO_STATS=1 SND_LOG=1 BOSS_LOG=1 AUDIO_DUMP="$OUT/sound.raw" ./shadow "${@:2}" 2> "$OUT/game.log" )
echo "Game closed (exit code $?)."

[ -f "$INI" ] && cp -f "$INI" "$OUT/settings-at-end.ini"
{ echo "date: $(date)"; echo "kernel: $(uname -r)"; echo "uptime: $(cat /proc/uptime 2>/dev/null)"
  echo "clang: $(clang --version 2>/dev/null | head -1)"; echo "git: $(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null)"
  echo "audio server: $(pactl info 2>/dev/null | grep -E 'Server Name|Default Sample' | tr '\n' ' ')"; } > "$OUT/system.txt"

# the sound the game produced: 32 kHz stereo 16-bit, compressed so it can be sent
if [ -s "$OUT/sound.raw" ]; then
  if command -v ffmpeg >/dev/null 2>&1; then
    ffmpeg -nostdin -hide_banner -loglevel error -y -f s16le -ar 32000 -ac 2 -i "$OUT/sound.raw" -c:a flac "$OUT/sound.flac" && rm -f "$OUT/sound.raw"
  else
    echo "ffmpeg is not installed: the sound is kept uncompressed (sudo dnf install ffmpeg makes the report much smaller)"
  fi
fi
tar -C "$ROOT" -czf "$ROOT/shadow-bug-report.tar.gz" bug-report
echo
echo "Report: $ROOT/shadow-bug-report.tar.gz ($(du -h "$ROOT/shadow-bug-report.tar.gz" | cut -f1))"
