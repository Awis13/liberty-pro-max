#!/bin/sh
# radio_import.sh: import the Radio Restoration mod payload (the radio songs
# the 2018 update cut, among them the full 2008 Vladivostok FM playlist)
# into the port's per-user data directory, where the game reads the files
# in place of the install's while GTAIV_RADIO=1 (the launcher's "original
# radio songs" option, src/radio.cpp). The install itself is not modified,
# and without the option the game is as it was.
#
#   radio_import.sh [--variant vanilla|classic|all] [--data-dir DIR]
#                   [--src DIR] [--purge]
#
# The payload is that of the Radio Restoration mod by Tomasak / Fusion
# Team (github.com/Tomasak/GTA-Downgraders, the iv-latest release; see
# Gillian's guide), downloaded and unpacked under --src (default
# the unpacked mod, GTAIV_RADIO_SRC):
#
#   payload-base/   the mod's data1.dat unpacked: the restored waves
#                   archive, the audio configs (the song table and the
#                   archive list among them), the song lyrics (*.RR.gxt),
#                   common to every variant;
#   payload-VARIANT the variant's own game.dat16 - the Vladivostok FM
#                   playlist: vanilla the 2008 songs only (the default),
#                   classic also the 2018 replacements, all also four beta
#                   songs.
#
# Missing payload directories are unpacked from the mod's own archives
# (unar or unrar for the .rar, 7z for the .dat files).
#
# --data-dir: the port's data directory (default: $GTAIV_DATA_DIR, else
# ~/Library/Application Support/gtaiv on macOS, else $XDG_DATA_HOME or
# ~/.local/share/gtaiv - where the game's saves live). Every machine that
# runs the port imports its own copy. --purge: remove the imported payload.
set -eu

SRC=${GTAIV_RADIO_SRC:?set GTAIV_RADIO_SRC to the unpacked Radio Restoration mod}
VARIANT=vanilla
DATA=${GTAIV_DATA_DIR:-}
PURGE=0

while [ $# -gt 0 ]; do
  case $1 in
    --variant) VARIANT=$2; shift 2 ;;
    --data-dir) DATA=$2; shift 2 ;;
    --src) SRC=$2; shift 2 ;;
    --purge) PURGE=1; shift ;;
    *) echo "usage: $0 [--variant vanilla|classic|all] [--data-dir DIR] [--src DIR] [--purge]" >&2; exit 2 ;;
  esac
done

case $VARIANT in vanilla|classic|all) ;; *) echo "radio_import: variant is vanilla, classic or all" >&2; exit 2 ;; esac

# The port's data directory, mirroring the runtime's DataDir().
if [ -z "$DATA" ]; then
  uname_s=$(uname -s)
  if [ "$uname_s" = Darwin ]; then
    DATA="$HOME/Library/Application Support/gtaiv"
  else
    DATA="${XDG_DATA_HOME:-$HOME/.local/share}/gtaiv"
  fi
fi
DEST="$DATA/drive_c/radio-restored"

if [ "$PURGE" = 1 ]; then
  rm -rf "$DEST"
  echo "radio_import: removed $DEST"
  exit 0
fi

MOD=$SRC/unpacked/Radio.Restoration.Mod.23-05-2025
RES="$MOD/Resources/Radio Restorer"

# unpack_dat ARCHIVE DIR: unpack one of the mod's 7z archives into DIR.
unpack_dat() {
  [ -f "$1" ] || { echo "radio_import: $1 not found: download the mod first (release iv-latest of github.com/Tomasak/GTA-Downgraders) and unpack it into $SRC" >&2; exit 1; }
  tmp=$(mktemp -d)
  7z x -y -bso0 -bse0 -o"$tmp" "$1" >/dev/null
  mkdir -p "$2"
  (cd "$tmp" && tar cf - .) | (cd "$2" && tar xf -)
  rm -rf "$tmp"
}

# The shared base: unpack it from the mod when it is not there yet.
if [ ! -d "$SRC/payload-base/update" ]; then
  echo "radio_import: unpacking the payload (data1.dat, 1.1 GB, takes a while)"
  unpack_dat "$RES/data1.dat" "$SRC/payload-base"
fi
# The variant's playlist on top of it.
if [ ! -d "$SRC/payload-$VARIANT/update" ]; then
  case $VARIANT in
    vanilla) OP=opVANILLA.dat ;;
    classic) OP=opCLASSIC.dat ;;
    all) OP=opALL.dat ;;
  esac
  unpack_dat "$RES/$OP" "$SRC/payload-$VARIANT"
fi

# Copy it under drive_c/radio-restored, dropping the update/ prefix the
# payload carries for the Windows overloader (guest C:\radio-restored).
mkdir -p "$DEST"
rsync -a --delete "$SRC/payload-base/update/" "$DEST/"
rsync -a "$SRC/payload-$VARIANT/update/" "$DEST/"

echo "radio_import: $VARIANT payload imported into $DEST"
du -sh "$DEST"
echo "run the game with the launcher's \"original radio songs\" option (or GTAIV_RADIO=1) to play it"
