#!/bin/bash
# install.sh: build Liberty Pro Max from your own copy of GTA IV (PC 1.0.8.0)
# and put "Liberty Pro Max.app" in /Applications.
#   ./install.sh "/path/to/Grand Theft Auto IV"
# The game folder is remembered (tools/local.env); later runs, after a
# `git pull`, need no argument.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
cd "$here"

say() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
die() { printf '\n\033[31merror:\033[0m %s\n' "$*" >&2; exit 1; }

[ "$(uname -s)" = Darwin ] || die "Liberty Pro Max builds on macOS."
[ "$(uname -m)" = arm64 ] || die "Liberty Pro Max needs a Mac with Apple silicon."
major=$(sw_vers -productVersion | cut -d. -f1)
[ "$major" -ge 14 ] || die "macOS 14 or newer is needed."

[ -f tools/local.env ] && . tools/local.env
if [ $# -ge 1 ]; then GTAIV_GAME_DIR=$(cd "$1" && pwd); fi
[ -n "${GTAIV_GAME_DIR:-}" ] || die "give the game folder (the one with GTAIV.exe): ./install.sh \"/path/to/Grand Theft Auto IV\""
[ -f "$GTAIV_GAME_DIR/GTAIV.exe" ] || die "no GTAIV.exe in $GTAIV_GAME_DIR"
[ "$(shasum -a 256 "$GTAIV_GAME_DIR/GTAIV.exe" | cut -d' ' -f1)" = 3d90e7c516fa450ca002e5031e62c0f66b404590f33e6cc9793b0da4fffbfd0f ] ||
  die "this GTAIV.exe is not the one supported: version 1.0.8.0 as Rockstar signed it"
printf 'GTAIV_GAME_DIR=%q\n' "$GTAIV_GAME_DIR" > tools/local.env
export GTAIV_GAME_DIR

say "Build tools"
xcode-select -p >/dev/null 2>&1 || die "install the Xcode Command Line Tools first: xcode-select --install"
command -v brew >/dev/null 2>&1 || die "install Homebrew first: https://brew.sh"
brew install cmake ninja sdl3 ffmpeg llvm@22 glslang spirv-cross python

say "x86recomp"
pin=$(cut -d' ' -f2 X86RECOMP)
if [ ! -d vendor/x86recomp/.git ]; then
  git clone --branch liberty https://github.com/Awis13/x86recomp vendor/x86recomp
fi
git -C vendor/x86recomp fetch --quiet origin liberty
# A pinned runtime may be newer than the fetched branch (for example
# while its companion runtime PR is under review). Fetch that exact
# public commit when it is not present locally.
if ! git -C vendor/x86recomp cat-file -e "$pin^{commit}" 2>/dev/null; then
  git -C vendor/x86recomp fetch --quiet origin "$pin"
fi
git -C vendor/x86recomp checkout --quiet "$pin"

say "Lifter and runtime (first build)"
cmake --preset release >/dev/null
cmake --build --preset release

say "The game's code, from your GTAIV.exe"
tools/lift.sh

say "The app's icon, from the game's own logo"
icon=out/icons/AppIcon.icns
if ! { python3 -m venv out/venv && out/venv/bin/pip -q install pillow &&
       out/venv/bin/python -I tools/make_ios_icon.py "$GTAIV_GAME_DIR" out/icons --mac; }; then
  echo "No icon could be made from the game's files: the app has the standard one."
fi

say "The game"
cmake --preset release >/dev/null
cmake --build --preset release

say "Installing to /Applications"
app="out/build/release/Liberty Pro Max.app"
[ -d "$app" ] || die "the build made no app: $app"
rm -rf "/Applications/Liberty Pro Max.app"
ditto "$app" "/Applications/Liberty Pro Max.app"
# (The build puts the icon in only when it links the game anew.)
if [ -f "$icon" ]; then
  mkdir -p "/Applications/Liberty Pro Max.app/Contents/Resources"
  cp -f "$icon" "/Applications/Liberty Pro Max.app/Contents/Resources/"
fi
codesign --force --sign - "/Applications/Liberty Pro Max.app" >/dev/null 2>&1 || true

say "Done"
cat <<EOF
Open "Liberty Pro Max" from Applications (or Launchpad), pick the game
folder in the launcher if asked, and press Play.

Game folder: $GTAIV_GAME_DIR
Saves and settings: ~/Library/Application Support/gtaiv
Log: ~/Library/Logs/gtaiv.log
EOF
