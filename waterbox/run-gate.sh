#!/bin/bash
# The core-level equivalence gate: for every test the sandboxed core must
# produce byte-identical video, audio, lag and memory-domain digests to the
# native reference build (the same cinterface.cpp compiled natively), and must
# survive a whole-machine savestate round-trip around every frame.
#
# The roms are homebrew, free to distribute, and vendored under tests/roms.
# Commercial cartridges belong in tests/roms-local (gitignored) and are used
# by tests/run-roms.sh instead.
#
# Usage: ./run-gate.sh [-n <native build dir>] [-g <guest build dir>]
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
nat="$root/build/meson-native"
gst="$root/build/meson-guest"
while getopts "n:g:" opt; do
	case "$opt" in
		n) nat="$OPTARG" ;;
		g) gst="$OPTARG" ;;
		*) exit 2 ;;
	esac
done

[ -x "$nat/run-native" ] && [ -x "$nat/run-wbx" ] || {
	echo "native build missing: meson setup build/meson-native && ninja -C build/meson-native" >&2; exit 1; }
[ -f "$gst/core.wbx" ] || {
	echo "guest build missing: sh waterbox/setup-guest.sh && ninja -C build/meson-guest" >&2; exit 1; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
digests() { grep -E '^(frames|vsync|videoHash|audioHash|lagFrames|domain\[)'; }

ok=0
failed=0
report() { printf "%-30s %-6s %s\n" "$1" "$2" "$3"; case "$2" in PASS) ok=$((ok+1)) ;; *) failed=$((failed+1)) ;; esac; }
printf "%-30s %-6s %s\n" "Check" "Result" "Detail"
printf "%-30s %-6s %s\n" "-----" "------" "------"

# name rom frames settings movie(- = pad exercise)
tests=(
	"hellway hellway.a26 - {} hellway.playaround.sol"
	"berryfun berryfun.bin 600 {} -"
	"hellwayIdle hellway.a26 600 {} -"
	"berryfunPal berryfun.bin 300 {\"format\":\"PAL\"} -"
	"hellwayUnplugged hellway.a26 300 {\"port2\":\"none\"} -"
	"berryfunDriving berryfun.bin 300 {\"port1\":\"driving\"} -"
)

for t in "${tests[@]}"; do
	read -r name rom frames settings movie <<< "$t"

	wd="$work/$name"
	mkdir -p "$wd"
	cp "$root/tests/roms/$rom" "$wd/"
	printf '{"cart":["%s"]}' "$rom" > "$wd/slots"
	printf '%s' "$settings" > "$wd/settings"

	if [ "$movie" = "-" ]; then
		args=(--frames "$frames" --exercise)
	else
		# a real playaround, replayed frame for frame: the input a person
		# actually gave, not a pattern
		args=(--sol "$root/tests/movies/$movie" --ctl1 joystick --ctl2 none)
	fi

	if ! "$nat/run-native" "$wd" "${args[@]}" 2>"$work/nat.err" | digests > "$work/nat.txt"; then
		report "$name:equivalence" FAIL "native runner error: $(head -1 "$work/nat.err")"; continue
	fi
	if ! "$nat/run-wbx" "$gst/core.wbx" "$wd" "${args[@]}" 2>"$work/box.err" | digests > "$work/box.txt"; then
		report "$name:equivalence" FAIL "waterbox runner error: $(head -1 "$work/box.err")"; continue
	fi
	nframes="$(sed -n 's/^frames=//p' "$work/box.txt")"
	if cmp -s "$work/nat.txt" "$work/box.txt"; then
		report "$name:equivalence" PASS "$nframes frames, native == waterboxed"
	else
		report "$name:equivalence" FAIL "$(diff "$work/nat.txt" "$work/box.txt" | tr '\n' ' ' | head -c 120)"
		continue
	fi

	# a hollow pass cannot sneak through: the input schedule must have shaped
	# the machine - an idle run of the same length must differ
	"$nat/run-wbx" "$gst/core.wbx" "$wd" --frames "$nframes" 2>/dev/null | digests > "$work/idle.txt"
	if cmp -s "$work/box.txt" "$work/idle.txt"; then
		report "$name:input-shaped" FAIL "the pad exercise changed nothing"
	else
		report "$name:input-shaped" PASS "input visibly shaped the machine"
	fi

	if ! "$nat/run-wbx" "$gst/core.wbx" "$wd" "${args[@]}" --rerecord 2>/dev/null | digests > "$work/rr.txt"; then
		report "$name:savestate" FAIL "rerecord runner error"; continue
	fi
	if cmp -s "$work/box.txt" "$work/rr.txt"; then
		report "$name:savestate" PASS "per-frame round-trip is lossless"
	else
		report "$name:savestate" FAIL "$(diff "$work/box.txt" "$work/rr.txt" | tr '\n' ' ' | head -c 120)"
	fi
done

# ---- the television format is a machine choice, and it has to REACH the guest:
# the same cartridge on a PAL machine runs at 50Hz and draws more lines.
wd="$work/format"
mkdir -p "$wd"
cp "$root/tests/roms/berryfun.bin" "$wd/"
printf '{"cart":["berryfun.bin"]}' > "$wd/slots"
printf '{"format":"NTSC"}' > "$wd/settings"
"$nat/run-wbx" "$gst/core.wbx" "$wd" --frames 120 2>/dev/null | digests > "$work/ntsc.txt"
printf '{"format":"PAL"}' > "$wd/settings"
"$nat/run-wbx" "$gst/core.wbx" "$wd" --frames 120 2>/dev/null | digests > "$work/pal.txt"
ntscRate="$(sed -n 's/^vsync=//p' "$work/ntsc.txt")"
palRate="$(sed -n 's/^vsync=//p' "$work/pal.txt")"
if [ "$ntscRate" = "60/1" ] && [ "$palRate" = "50/1" ] && ! cmp -s "$work/ntsc.txt" "$work/pal.txt"; then
	report "settings:format" PASS "NTSC ran at $ntscRate, PAL at $palRate, and the machines differ"
else
	report "settings:format" FAIL "NTSC=$ntscRate PAL=$palRate (expected 60/1 and 50/1, and different digests)"
fi

echo
echo "$ok ok, $failed failed"
[ "$failed" -eq 0 ]
