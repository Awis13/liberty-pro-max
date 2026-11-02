#!/bin/sh
# Play the Mac build: the launcher window first, sound, the user's own data directory
# (~/Library/Application Support/gtaiv). The log goes to
# ~/Library/Logs/gtaiv.log (with an fps line every 10 s, and what the main and
# presenting threads did in each frame over 80 ms). Graphics: Metal unless
# GTAIV_GFX says otherwise; display, vsync, anti-aliasing and the performance
# overlay as set in the launcher.
# Runs the app bundle ("Liberty Pro Max.app"): full screen in front, macOS
# turns Game Mode on for it.
#   play-mac.sh [GAME_DIR] [-- game arguments]
P=$(cd "$(dirname "$0")/.." && pwd)
G=${1:-$P/run/game}
[ $# -gt 0 ] && shift
GTAIV_FPS_REPORT=${GTAIV_FPS_REPORT:-10} GTAIV_HITCH_STACKS=${GTAIV_HITCH_STACKS:-80} GTAIV_GFX=${GTAIV_GFX:-metal} exec "$P/out/build/release/Liberty Pro Max.app/Contents/MacOS/gtaiv" --game-dir "$G" "$@" > "$HOME/Library/Logs/gtaiv.log" 2>&1
