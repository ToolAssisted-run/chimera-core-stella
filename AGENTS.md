# AGENTS.md - Stella core for Chimera

This repository builds upstream Stella as a sandboxed guest for Chimera, a
frontend for tool-assisted speedruns. It produces one file,
`stella.chimeraCore`: a zip holding `core.wbx` (the emulator built for the
miniBox sandbox), `waterbox.config`, `default_keybinds.json`,
`file_slots.json`, `build.json` and the licence texts. Chimera loads it as an
Atari 2600. `docs/BUILDING.md` has the detailed build instructions; this file
is the short operating guide.

## Layout

- `extern/stella/` - upstream, a git submodule pinned to unmodified upstream.
- `patches/0001` to `0004` - every change made to upstream: the input-read hook, the pinned random seed, the frame-driven event handler, turbo.
- `meson.build`, `meson_options.txt` - one build description, two flavors: native (`run-native`, `run-wbx`) and guest (`core.wbx`).
- `waterbox/cinterface.cpp` - the adapter: Stella's own libretro facade behind Chimera's guest ABI. Compiled unchanged into both flavors.
- `waterbox/vendored/blip_buf/` - the audio resampler, vendored.
- `waterbox/run-native.c`, `waterbox/run-wbx.c`, `waterbox/gate-harness.h` - the reference driver, the sandbox driver, and the replay-and-digest loop they share. `waterbox/native-shim/emulibc.h` replaces miniBox's emulibc in the native build.
- `waterbox/waterbox.config` - what Chimera is told: the system, the controller, the settings.
- `waterbox/file_slots.json` - the files the project wizard asks for: one cartridge.
- `waterbox/default_keybinds.json`, `waterbox/package-licenses.json` - shipped in the package.
- `waterbox/apply-patches.sh`, `waterbox/setup-guest.sh`, `waterbox/build-package.sh` - the build scripts.
- `waterbox/run-gate.sh` - the core gate.
- `waterbox/tests/run-frontend.sh` - the frontend gate; the rest of `waterbox/tests/` is its helpers.
- `tests/roms/`, `tests/movies/` - two homebrew ROMs and one `.sol` movie.
- `docs/PLAN.md` - design decisions, sharp edges and what remains.
- `.github/workflows/chimera.yml` - CI: core gate, frontend gate, publish. The authoritative build recipe.
- `build/` - all build output (gitignored).

## Set up the build environment

Linux on x86_64. Set `CHIMERA` and `MB` again in every new shell session.

```
sudo apt-get update
sudo apt-get install -y --no-install-recommends meson ninja-build build-essential python3

git submodule update --init

CHIMERA="$HOME/chimera"      # a Chimera checkout; any absolute path will do
[ -d "$CHIMERA" ] || git clone https://github.com/ToolAssisted-run/chimera.git "$CHIMERA"
git -C "$CHIMERA" submodule update --init extern/chimera-common-minibox
MB="$CHIMERA/extern/chimera-common-minibox"

[ -f "$MB/build/meson-linux/build.ninja" ] || meson setup "$MB/build/meson-linux" "$MB"
meson compile -C "$MB/build/meson-linux"
[ -f "$MB/build/meson-cpp/build.ninja" ] || meson setup "$MB/build/meson-cpp" "$MB" -Dguest_cpp=true
meson compile -C "$MB/build/meson-cpp"
```

The last four lines build miniBox twice, and this core needs both:
`build/meson-linux` holds the host library `run-wbx` links, `build/meson-cpp`
holds the C++ guest toolchain. The second build downloads the GCC source
matching the installed gcc (about 84 MB) to build libstdc++: it needs the
network once.

## Build

```
# the package: configures the guest if needed, builds core.wbx, checks and zips it
./waterbox/build-package.sh -m "$MB" -r "$CHIMERA"

# the native reference and the sandbox driver, which the gates need
meson setup build/meson-native -Dminibox_dir="$MB"
ninja -C build/meson-native
```

- The package lands at `$CHIMERA/build/Cores/stella.chimeraCore`.
- `meson setup` is needed once per build directory. After a source change run
  `ninja -C build/meson-native` and `ninja -C build/meson-guest`. meson
  applies `patches/` at every configure; do not run `apply-patches.sh` by hand.
- A hand-built package is stamped `<commit>+local`, or `<commit>-dirty+local`
  when the tree has changes. The applied patches count as a change, so expect
  `-dirty`. Only CI, which sets `CORE_VERSION`, builds a publishable package.

## Install the core into Chimera

Chimera ships no cores and downloads nothing. A core is a file that somebody
puts in the cores folder.

- Source checkout: the cores folder is `$CHIMERA/build/Cores/`, and
  `build-package.sh -r "$CHIMERA"` has already written the package there.
- Release bundle: copy `stella.chimeraCore` into the `Cores` folder beside
  `Chimera.exe`, or into the folder chosen in File > Core Manager >
  Change folder...
- File > Core Manager lists the cores in that folder. Refresh List rescans it.
  The same file works on Linux and on Windows.

## Test before you commit

Rebuild both flavors first. The gates build nothing and will compare stale
binaries without complaint. CI runs both gates on every push to main and every
pull request.

```
ninja -C build/meson-native && ninja -C build/meson-guest
./waterbox/run-gate.sh             # must end "<n> ok, 0 failed" (25 checks)
```

When the change touches `waterbox.config`, the keybinds, `file_slots.json` or
packaging, also run the frontend gate. It needs a built Chimera
(`docs/BUILDING.md`, "Frontend gate"):

```
./waterbox/build-package.sh -m "$MB" -r "$CHIMERA"
./waterbox/tests/run-frontend.sh --chimera-root "$CHIMERA"   # 3 checks, 0 failed
```

The native reference is where a host clock or a host thread still ticks. A
`run-native` that differs from itself between runs is a determinism bug in the
core, not a flaky test (`docs/PLAN.md`, "Sharp edges hit").

## Rules of this repository

- Upstream is a submodule pinned to unmodified upstream. Never commit inside
  `extern/stella`. A change to upstream goes into a numbered patch in
  `patches/`; keep each one a small hook. After a build `git status` shows the
  submodule as modified: that is the applied series, leave it.
- `apply-patches.sh` compares the tree with what the whole series leaves
  behind. A tree that is neither untouched nor exactly that stops the
  configure, and the message gives the way back:
  `git -C extern/stella reset --hard && git -C extern/stella clean -fd && waterbox/apply-patches.sh`.
  That discards edits in the submodule, so turn them into a patch first.
- Determinism is the product. The guest must not read host time, host
  randomness or anything else that differs between runs, and a savestate must
  round-trip. The gate checks it. A change that breaks it is a bug. Patches
  0002 and 0003 exist because upstream read the clock.
- Run the gate before committing. A new check needs a negative control: break
  the thing it checks, watch it fail, and say so in the commit.
- Never commit game files, BIOS or firmware. `tests/roms/` holds only homebrew
  that is free to distribute; local content goes in the gitignored
  `tests/roms-local/`. Never add network access.
- Do not write `version` or `versionDate` into `waterbox.config`: `build-package.sh` stamps them.
- Shell scripts stay executable (git mode 100755): CI runs them by path.
  Documentation prose is plain ASCII.
- Commit messages: the subject is a plain sentence saying what is now true,
  usually with a type and scope in front, and no final period. Example:
  `fix(patches): the series is judged as a whole, not a patch at a time`.
  Types in the log: `feat`, `fix`, `docs`, `ci`. The body is prose: what was
  wrong, what changed, how it was proved (the gate count, the negative
  control). Issues for this core are filed in the chimera repository and
  cited as `ToolAssisted-run/chimera#N`.
- Do not edit `.github/workflows` unless the task is the workflow.

## Where to read more

- `docs/BUILDING.md` - the full build, every gate, the files a user provides.
- `docs/PLAN.md` - why the integration is shaped the way it is.
- `.github/workflows/chimera.yml` - what CI does, step by step.
- In the Chimera repository (https://github.com/ToolAssisted-run/chimera):
  `docs/porting-a-core.md`, `docs/gates.md` (read it before writing a check),
  `docs/core-manager.md`.
