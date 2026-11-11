#!/usr/bin/env bash
# Linux build of ShadowRecomp (the Windows one is build.cmd). See PORTAGE_LINUX.md.
#
#   ./build.sh /path/to/your/disc.iso      first time: unpack + check your disc, generate the code, build
#   ./build.sh                             later: rebuild only
#
# Your disc (USA, GUPE8P) is unpacked to ../Shadow, next to this project folder (SHADOW_DISC=<folder> to put it
# elsewhere). Nothing of the game is ever written into the repository except generated/ (ignored by git).
# The result is play/shadow; start it with ./play/shadow
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"

DISC_DIR="${SHADOW_DISC:-$(dirname "$PWD")/Shadow}"
PYTHON="${PYTHON:-python3}"

need() { command -v "$1" >/dev/null 2>&1 || { echo "error: '$1' is not installed (see the package list in PORTAGE_LINUX.md)" >&2; exit 1; }; }
need "$PYTHON"; need cmake; need ninja; need clang; need clang++

# 1. the game's own files, from your disc
if [ ! -f "$DISC_DIR/sys/main.dol" ]; then
  if [ $# -lt 1 ]; then
    echo "error: no unpacked disc at $DISC_DIR" >&2
    echo "       run:  ./build.sh /path/to/your/Shadow-the-Hedgehog-USA.iso   (.iso, .gcm, .rvz or a folder extracted with Dolphin)" >&2
    exit 1
  fi
  echo "== Checking and unpacking your disc to $DISC_DIR"
  "$PYTHON" tools/game_import/gameimport.py import "$1" "$DISC_DIR"
fi

# 2. the recompiled game code (generated/), from your main.dol
if [ ! -f generated/files.cmake ] || [ "$DISC_DIR/sys/main.dol" -nt generated/files.cmake ] || [ tools/gen.py -nt generated/files.cmake ] \
   || [ tools/lift.py -nt generated/files.cmake ] || [ overrides.txt -nt generated/files.cmake ] || [ callhooks.txt -nt generated/files.cmake ] \
   || [ dtscale.txt -nt generated/files.cmake ] || [ multientry.txt -nt generated/files.cmake ]; then
  echo "== Generating the game code (tools/gen.py)"
  SHADOW_DISC="$DISC_DIR" "$PYTHON" tools/gen.py
  touch generated/files.cmake
fi

# 3. build
echo "== Building (the first time this also downloads and builds SDL3 and fetches Dawn: several minutes)"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ${CMAKE_ARGS:-}
cmake --build build

# 4. a folder to play from: the binary, with the unpacked disc linked next to it (saves, logs and settings go here too)
mkdir -p play
cp -f build/shadow play/shadow
[ -e play/Shadow ] || ln -s "$DISC_DIR" play/Shadow
echo
echo "Done: $(pwd)/play/shadow"
echo "Start the game with:  ./play/shadow"
