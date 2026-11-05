#!/bin/bash
# Apply the reviewed renderer changes to the exact X86RECOMP pin, once.
# Both the runtime and the Builder use this source through CMake.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
renderer=${1:-$here/vendor/x86recomp}
pin=$(cut -d' ' -f2 "$here/X86RECOMP")
if [ "$(git -C "$renderer" rev-parse HEAD)" != "$pin" ]; then
  echo "Renderer patches require the X86RECOMP pin $pin. Check out that commit first." >&2
  exit 1
fi
for patch in "$here"/patches/x86recomp/*.patch; do
  [ -f "$patch" ] || continue
  if git -C "$renderer" apply --reverse --check "$patch" 2>/dev/null; then
    continue
  fi
  if ! git -C "$renderer" apply --check "$patch"; then
    echo "Cannot apply $(basename "$patch"). Preserve local renderer changes, then restore the pinned source." >&2
    exit 1
  fi
  git -C "$renderer" apply "$patch"
done
