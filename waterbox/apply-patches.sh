#!/bin/sh
# Overlays the chimera patch set on the pristine upstream submodule.
#
# Idempotent: each patch is checked before it is applied, so a configured build
# tree can be reconfigured without the patches stacking up. The submodule pin
# stays UNMODIFIED upstream - what chimera needs is here, in patches/, where it
# can be read in one sitting.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
tree="$root/extern/stella"

[ -d "$tree/src" ] || { echo "extern/stella is not checked out; run: git submodule update --init" >&2; exit 1; }

for patch in "$root"/patches/*.patch; do
	[ -e "$patch" ] || continue
	if git -C "$tree" apply --check --reverse "$patch" 2>/dev/null; then
		continue  # already applied
	fi
	git -C "$tree" apply "$patch"
	echo "applied $(basename "$patch")"
done
