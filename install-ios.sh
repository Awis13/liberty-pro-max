#!/bin/zsh
# install-ios.sh TEAM_ID [BUNDLE_ID]
# Builds Liberty Pro Max for the iPad connected by cable, installs it and
# copies the game's files (15 GB) into it. Run ./install.sh first: the iPad
# build uses the game's code it translated (generated/).
#   TEAM_ID    your Apple developer team (Xcode > Settings > Accounts)
#   BUNDLE_ID  the app's identifier (default io.github.<team>.libertypromax)
# Needs a paid Apple developer account signed in to Xcode, the iPad in
# Developer Mode and connected by cable.
set -euo pipefail
here=${0:A:h}
cd $here
say() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
die() { printf '\n\033[31merror:\033[0m %s\n' "$*" >&2; exit 1; }

team=${1:-}; [[ -n $team ]] || die "give your Apple developer team id: ./install-ios.sh TEAM_ID"
bundle=${2:-io.github.${(L)team}.libertypromax}
[[ -f tools/local.env ]] && . tools/local.env
[[ -n ${GTAIV_GAME_DIR:-} && -f $GTAIV_GAME_DIR/GTAIV.exe ]] || die "run ./install.sh first (it remembers the game folder)"
[[ -d generated/native ]] || die "run ./install.sh first: no translated game code in generated/"
xcode-select -p | grep -q Xcode.app || die "the iPad build needs Xcode (not only the Command Line Tools)"

say "iPad"
device=$(xcrun devicectl list devices 2>/dev/null | awk '/iPad/ && /available/ && /physical/ {for (i=1;i<=NF;i++) if ($i ~ /^[0-9A-F]{8}-[0-9A-F]{16}$/) print $i}' | head -1)
[[ -n $device ]] || die "no iPad connected (cable, unlocked, Developer Mode on)"
echo "device $device, app $bundle"

say "Build tools and the iPad's libraries"
brew install xcodegen
python3 -m venv out/venv && out/venv/bin/pip -q install pillow
[[ -d out/apple/prefix-ios ]] || tools/apple_deps.sh

say "Signing profile"
[[ -f out/apple/profiles/libertypromax.mobileprovision ]] || tools/ios_profile.sh $team $bundle $device

say "Icon, from the game's own logo"
out/venv/bin/python -I tools/make_ios_icon.py "$GTAIV_GAME_DIR" out/icons

say "The game for iPad"
cmake --preset iphoneos > /dev/null
cmake --build --preset iphoneos

say "Installing"
BUILD=out/apple/build-ios PREFIX=out/apple/prefix-ios ICONS=out/icons tools/ios_bundle.sh $device

say "The game's files (15 GB over the cable)"
STATE=out/apple/push.state python3 tools/ios_push_game.py "$GTAIV_GAME_DIR" $device $bundle

say "Done"
echo "Open Liberty Pro Max on the iPad, press Play. A controller or a keyboard plays it."
