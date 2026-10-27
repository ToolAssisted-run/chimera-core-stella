#!/bin/bash
# The frontend half of the gate: load the Stella package in Chimera (its window,
# run headless: no display), boot a homebrew cartridge for a fixed
# number of frames with nothing pressed, and require the machine's whole RAM -
# all 128 bytes of it - to be byte-identical to the native reference. Then
# prove a machine-shaping setting arrives (format=PAL builds a 50Hz machine
# that still matches ITS native reference), and that the package's keybinds
# are the machine's when nobody changed them.
#
# Usage: ./run-frontend.sh [--chimera-root <path>] [--frames N]
set -u

here="$(cd "$(dirname "$0")" && pwd)"
wb="$(cd "$here/.." && pwd)"
root="$(cd "$wb/.." && pwd)"
frames=300
chimera_root=""
while [ $# -gt 0 ]; do
	case "$1" in
		--chimera-root) chimera_root="$2"; shift ;;
		--frames) frames="$2"; shift ;;
		-*) echo "unknown option: $1" >&2; exit 2 ;;
		*) break ;;
	esac
	shift
done

if [ -z "$chimera_root" ]; then
	for candidate in "$root/../chimera" "$HOME/chimera"; do
		[ -d "$candidate" ] && { chimera_root="$candidate"; break; }
	done
fi
[ -n "$chimera_root" ] && [ -d "$chimera_root" ] || {
	echo "chimera checkout not found; pass --chimera-root <path>" >&2; exit 1; }
chimera_root="$(cd "$chimera_root" && pwd)"

chimera_exe="$chimera_root/build/meson-linux/source/ui/chimera"
package="$chimera_root/build/Cores/stella.chimeraCore"
rn="$root/build/meson-native/run-native"
rom="$root/tests/roms/berryfun.bin"
[ -x "$chimera_exe" ] || { echo "Chimera not built: $chimera_exe" >&2; exit 1; }
[ -f "$package" ] || { echo "package not installed: $package (run ../build-package.sh)" >&2; exit 1; }
[ -x "$rn" ] || { echo "native reference not built" >&2; exit 1; }

work="$here/work"
mkdir -p "$work"

# Chimera's window, run headless: the cores installed in the checkout, its
# settings, cache and recovery files the run's own, and nothing carried over
# from a Chimera installed on this machine
export ALSOFT_DRIVERS=null CHIMERA_CORES_DIR="$chimera_root/build/Cores" CHIMERA_QT_CACHE="$work/qt-cache" CHIMERA_QT_RECOVERY="$work/qt-recovery" \
	CHIMERA_QT_HASHES="$work/qt-file-hashes.txt" CHIMERA_QT_CHIMERA_SETTINGS="$work/no-chimera-config.ini"

ok=0
failed=0
report() { printf "%-28s %-9s %s\n" "$1" "$2" "$3"; case "$2" in PASS) ok=$((ok+1)) ;; *) failed=$((failed+1)) ;; esac; }
printf "%-28s %-9s %s\n" "Check" "Result" "Detail"
printf "%-28s %-9s %s\n" "-----" "------" "------"

run_frontend() {
	local tag="$1" cfg="$2" nframes="$3" shot="${4:-}" pkg="${5:-$package}" therom="${6:-$rom}"
	local job="$work/job.$tag.txt"
	{
		echo "frames=$nframes"
		echo "out=$work/$tag.ram.bin"
		echo "meta=$work/$tag.meta.txt"
		echo "shot=$shot"
	} > "$job"
	rm -f "$work/$tag.ram.bin" "$work/$tag.meta.txt"
	[ -n "$shot" ] && rm -f "$shot"
	local project
	project="$(game_project "$tag" "$cfg" "$pkg" "$therom")" || { echo "no project made (see tests/work/project.$tag/make.log)" > "$work/$tag.log"; return 1; }
	( cd "$chimera_root" && MINIHAWK_JOB="$job" timeout -s KILL 900 "$chimera_exe" --headless \
		"--config=$cfg" "--core=$pkg" \
		"--lua=$here/frontend-ram.lua" "--project=$project" ) > "$work/$tag.log" 2>&1
	[ -f "$work/$tag.meta.txt" ] && grep -q "^status=OK" "$work/$tag.meta.txt"
}

# a native reference run: same rom, same idle schedule, settings JSON in $2
native_ram() {
	local tag="$1" settings="$2"
	local wd="$work/native.$tag"
	rm -rf "$wd"
	mkdir -p "$wd"
	cp "$rom" "$wd/"
	printf '{"cart":["%s"]}' "$(basename "$rom")" > "$wd/slots"
	[ -n "$settings" ] && printf '%s' "$settings" > "$wd/settings"
	"$rn" "$wd" --frames "$frames" --dump-domain "Main RAM" "$work/native.$tag.ram.bin" \
		> "$work/native.$tag.txt" 2>&1
}

# A game runs only in a project (Chimera, 2026-10-26): each run opens one made
# of its game with chimera-run --new-project, holding the settings
# settings_config was given for the config it runs with.
declare -A settings_of
settings_config() { python3 "$here/settings-config.py" "$1" "$2"; settings_of["$1"]="$2"; }
crun="$chimera_root/build/dll/chimera-run"
game_project() { # tag, config, package, game: prints the project's path
	local dir="$work/project.$1" settings="${settings_of[$2]:-}"
	local pkg="$3" game="$4"
	local fw=()
	rm -rf "$dir" && mkdir -p "$dir" && cp "$game" "$dir/"
	"$crun" --new-project "$dir/$1.chimeraProject" "$pkg" "$dir/$(basename "$game")" \
		--settings "${settings:-"{}"}" "${fw[@]}" > "$dir/make.log" 2>&1 && echo "$dir/$1.chimeraProject"
}

# The bindings the machine has, written by dump-controls.lua (the window keeps
# only the keys a person changed; fresh settings have none, so these are the
# package's).
run_controls() {
	local tag="$1" cfg="$2" pkg="${3:-$package}" therom="${4:-$rom}"
	local job="$work/job.$tag.txt" project
	printf 'out=%s/%s.controls.json\nmeta=%s/%s.meta.txt\n' "$work" "$tag" "$work" "$tag" > "$job"
	rm -f "$work/$tag.controls.json" "$work/$tag.meta.txt"
	project="$(game_project "$tag" "$cfg" "$pkg" "$therom")" || { echo "no project made (see tests/work/project.$tag/make.log)" > "$work/$tag.log"; return 1; }
	( cd "$chimera_root" && MINIHAWK_JOB="$job" timeout -s KILL 900 "$chimera_exe" --headless \
		"--config=$cfg" "--core=$pkg" \
		"--lua=$here/dump-controls.lua" "--project=$project" ) > "$work/$tag.log" 2>&1
	[ -f "$work/$tag.meta.txt" ] && grep -q "^status=OK" "$work/$tag.meta.txt"
}

# --- the machine the frontend builds must be the one the gate signed off on ---
settings_config "$work/settings.base.json" '{}'
if ! native_ram "base" ""; then
	report "cart:frontend" FAIL "native runner error (see tests/work/native.base.txt)"
elif ! run_frontend "base" "$work/settings.base.json" "$frames" "$work/base.png"; then
	report "cart:frontend" FAIL "no OK meta (see tests/work/base.log)"
elif cmp -s "$work/native.base.ram.bin" "$work/base.ram.bin"; then
	report "cart:frontend" PASS "$frames frames, Main RAM identical to the native reference"
else
	report "cart:frontend" FAIL "Main RAM differs from the native reference"
fi

# --- a machine-shaping setting must reach the guest through the frontend ---
settings_config "$work/settings.pal.json" '{"format": "PAL"}'
if ! native_ram "pal" '{"format":"PAL"}'; then
	report "settings:format" FAIL "native runner error (see tests/work/native.pal.txt)"
elif ! run_frontend "pal" "$work/settings.pal.json" "$frames"; then
	report "settings:format" FAIL "run did not report OK (see tests/work/pal.log)"
elif ! cmp -s "$work/native.pal.ram.bin" "$work/pal.ram.bin"; then
	report "settings:format" FAIL "PAL Main RAM differs from its native reference"
else
	# RAM alone cannot show this: a rom whose memory does not depend on the
	# television still draws a TALLER PICTURE on a PAL machine, and that is
	# the machine having changed.
	ntscH="$(sed -n 's/^height=//p' "$work/base.meta.txt")"
	palH="$(sed -n 's/^height=//p' "$work/pal.meta.txt")"
	if [ -n "$palH" ] && [ -n "$ntscH" ] && [ "$palH" -gt "$ntscH" ]; then
		report "settings:format" PASS "format=PAL matches its native reference and draws $palH lines against NTSC's $ntscH"
	else
		report "settings:format" FAIL "format=PAL drew $palH lines against NTSC's $ntscH (expected more)"
	fi
fi

# --- the bindings the package ships are the machine's when nobody changed them ---
if run_controls "keys" "$work/settings.keys.json"; then
	if python3 "$here/check-keybinds.py" "$work/keys.controls.json" \
		"$wb/default_keybinds.json" "Atari 2600 Controller" > "$work/keys.txt" 2>&1; then
		report "keybinds" PASS "$(cat "$work/keys.txt")"
	else
		report "keybinds" FAIL "$(head -1 "$work/keys.txt")"
	fi
else
	report "keybinds" FAIL "run did not report OK (see tests/work/keys.log)"
fi

echo
echo "$ok ok, $failed failed"
[ "$failed" -eq 0 ]
