# Building the Stella core

This repository builds Stella as a sandboxed guest (`core.wbx`) and packages
it as one file, `stella.chimeraCore`, which Chimera loads as an Atari 2600.
The steps below are the ones `.github/workflows/chimera.yml` runs from a fresh
clone on a public Ubuntu runner, where they pass.

Two placeholders are used throughout:

- `<chimera>` is the absolute path of a Chimera checkout
  (https://github.com/ToolAssisted-run/chimera).
- `<minibox>` is `<chimera>/extern/chimera-common-minibox`, the miniBox
  submodule of that checkout. miniBox is the sandbox host and the guest
  toolchain.

## Requirements

- Linux on x86_64. CI uses GitHub's `ubuntu-latest` runner. Cores are built on
  Linux; the package that comes out also runs on Windows.
- git.
- For the core, the package and the core gate:

  ```
  sudo apt-get update
  sudo apt-get install -y --no-install-recommends meson ninja-build build-essential python3
  ```

  The compilers are the distribution's gcc and g++ from `build-essential`. The
  workflow pins no compiler version. `meson.build` asks for C++23, so the g++
  must support it. The guest is compiled by the same gcc and g++ through
  miniBox's musl specs file, so there is no separate cross compiler to
  install.
- For the frontend gate only, Chimera built with its window. Chimera's
  `tools/ci-chimera.sh` installs what that needs (Qt 6.12, from Qt's own
  archives with aqt) and builds it; the workflow keeps Qt in `~/qt` between
  runs.

- One download happens during the build, and miniBox does it itself. Stella is
  C++, so the guest needs a C++ standard library built for the sandbox.
  miniBox's C++ guest toolchain fetches the GCC source that matches the
  installed gcc (about 84 MB, with `curl`, from the GNU mirrors) and builds
  libstdc++ from it. The machine needs network access for that one step. The
  tarball is kept in the miniBox build directory, so a rebuild does not fetch
  it again.

## Get the sources

This repository, with its submodule. The workflow uses `actions/checkout@v6`
with `submodules: true`, which is:

```
git clone https://github.com/ToolAssisted-run/chimera-core-stella.git
cd chimera-core-stella
git submodule update --init
```

The submodule is `extern/stella`, pinned to an unmodified upstream commit.

A Chimera checkout, for miniBox. The workflow checks out Chimera's `main`
branch and then only the miniBox submodule:

```
git clone https://github.com/ToolAssisted-run/chimera.git <chimera>
git -C <chimera> submodule update --init extern/chimera-common-minibox
```

The frontend gate builds Chimera itself and needs all of its submodules
(`submodules: recursive` in the workflow):

```
git -C <chimera> submodule update --init --recursive
```

Where the scripts look when no path is given:

- `waterbox/build-package.sh` and `waterbox/tests/run-frontend.sh` look for
  Chimera at `../chimera` beside this repository, then at `$HOME/chimera`.
- `meson.build` (the native build) looks for miniBox at
  `../chimera/extern/chimera-common-minibox`.
- `waterbox/setup-guest.sh` (the guest build) looks for miniBox at
  `$HOME/chimera/extern/chimera-common-minibox`.

These defaults differ from each other. Pass the paths explicitly, as CI does
and as every command below does.

## Build miniBox

This core needs two miniBox build directories, and the workflow builds both:

```
meson setup <minibox>/build/meson-linux <minibox>
meson compile -C <minibox>/build/meson-linux
meson setup <minibox>/build/meson-cpp <minibox> -Dguest_cpp=true
meson compile -C <minibox>/build/meson-cpp
```

- `build/meson-linux` gives the sandbox host library,
  `source/host/libminiboxhost.so`. This repository's `meson.build` links the
  `run-wbx` test driver against the library in that directory.
- `build/meson-cpp` gives the C++ guest toolchain: `guest-sysroot/` (musl and
  libstdc++) and the objects the guest links. `waterbox/setup-guest.sh` looks
  for the guest toolchain in that directory and nowhere else.

The workflow keeps both directories in a cache (`actions/cache@v4`) and skips
each `meson setup` when its `build.ninja` is already there. By hand, keep the
directories and run only `meson compile` the next time.

## Build the core

### Patches

The changes to Stella are four patches in `patches/`:

- `0001-chimera-input-read-hook.patch`: `M6532.cxx` reports that the program
  read its input. That is lag detection.
- `0002-chimera-pinned-random-seed.patch`: `Console.cxx` and `OSystem.cxx`
  take the power-on seed from the project instead of the clock.
- `0003-chimera-frame-driven.patch`: `EventHandler.cxx` does not arm its
  wall-clock timeout.
- `0004-chimera-turbo.patch`: `tia/TIA.cxx` can skip drawing, for turbo.

`waterbox/apply-patches.sh` applies them to the working tree of
`extern/stella`. `meson.build` runs that script at every configure, so there
is no need to run it by hand.

The script judges the series as a whole. On a scratch copy it works out what
the touched files look like with every patch applied, and compares the
working tree with that:

- Untouched tree: it applies every patch and prints `applied: <name>` for
  each.
- Tree that carries the whole series: it prints
  `already applied: all 4 patches` and changes nothing.
- Anything else: it names each file that is wrong, prints how to start again
  from the submodule's commit, and fails. The configure stops with it.

It also fails when the submodule is not checked out, and when the series does
not apply to the submodule's commit in order. `STELLA_TREE` points the script
at another checkout, which is how the script itself is tested.

After the first configure `git status` shows the submodule as modified. That
is the applied series, and it is expected.

### The native reference and the sandbox driver

```
meson setup build/meson-native -Dminibox_dir=<minibox>
ninja -C build/meson-native
```

This builds two programs in `build/meson-native`:

- `run-native` is the reference: the same `waterbox/cinterface.cpp` and the
  same Stella sources, compiled for the host with no sandbox. The gates
  compare the sandboxed core against it.
- `run-wbx` runs `core.wbx` through the miniBox host library and prints the
  same digests as `run-native`.

`ninja -C build/meson-native run-native` builds the reference alone. That is
enough for the frontend gate.

### The guest core

```
MINIBOX_DIR=<minibox> sh waterbox/setup-guest.sh -- -Dminibox_dir=<minibox>
ninja -C build/meson-guest
```

`setup-guest.sh` writes the meson cross file `build/guest-cross.ini` (paths of
this machine; do not commit it) and configures `build/meson-guest`. It takes
the miniBox path from `-m <miniBox dir>` or from `MINIBOX_DIR`. Arguments
after `--` go to `meson setup`. The result is `build/meson-guest/core.wbx`.

## Build the package

```
./waterbox/build-package.sh -m <minibox> -r <chimera>
```

Options:

- `-m <miniBox dir>`: the miniBox checkout. Default: `MINIBOX_DIR`, then
  `<chimera root>/extern/chimera-common-minibox`.
- `-r <chimera root>`: the Chimera checkout the package is written into.
  Default: `../chimera`, then `$HOME/chimera`.

There is no option for another output directory.

What the script does:

1. Runs `waterbox/setup-guest.sh` if `build/meson-guest` is not configured,
   then builds `core.wbx`.
2. Runs miniBox's `source/guest/check-wbx.sh` on `core.wbx`.
3. Stages `core.wbx`, `waterbox.config`, `default_keybinds.json`,
   `file_slots.json`, the licence texts and `build.json` (what built the
   package) under `build/package-staging`.
4. Stamps `version` and `versionDate` into the staged `waterbox.config`.
5. Writes the package twice, compares the SHA-1 of both and prints it.
6. Removes `<chimera>/build/CoreCache/stella-*`.

The file lands at `<chimera>/build/Cores/stella.chimeraCore`.

The version is the commit the package was built from:

- CI sets `CORE_VERSION` to the full commit SHA, and the package carries that.
- Without `CORE_VERSION` the script stamps the first 12 digits of `HEAD` and
  `+local`, with `-dirty` before it when `git diff --quiet HEAD` reports
  changes: `0123456789ab+local` or `0123456789ab-dirty+local`.
- `versionDate` is the date of that commit in UTC, never the build's date.

A package built by hand is for testing. Chimera's publishing script refuses a
version that carries `+local` or `-dirty`.

In CI the frontend gate job builds the package with `CORE_VERSION` set, runs
the frontend gate on it and uploads it as the artifact `stella-<commit SHA>`
(`actions/upload-artifact@v7`). The `publish` job hands that artifact to
Chimera's reusable workflow `publish-core.yml`. Only the scheduled run and a
run started by hand publish; a push or a pull request publishes nothing.

## Install it into Chimera

Chimera ships no cores and downloads nothing: it has no network code. A core
gets into Chimera because somebody puts its file in the cores folder.

- In a Chimera source checkout the cores folder is `<chimera>/build/Cores/`.
  `build-package.sh -r <chimera>` has already written the package there.
- In a release bundle the cores folder is `cores` beside Chimera's program, or
  another folder chosen in Tools > Core Manager... > Change folder... Copy
  `stella.chimeraCore` into it.

Tools > Core Manager... lists what is in that folder. Refresh List rescans it, so
a package copied in while Chimera runs is found without a restart.

The same package file works on Linux and on Windows: Chimera's sandbox,
miniBox, runs the guest inside it on either.

You do not have to build it. CI publishes the package on this repository's
Releases page
(https://github.com/ToolAssisted-run/chimera-core-stella/releases) as
`stella-<version>.chimeraCore`:

- `nightly-YYYY-MM-DD`: dated, from the scheduled run (04:00 UTC) or a run
  started by hand, and only when main moved since the last one.

To use the core, start Chimera (`build/meson-linux/source/ui/chimera` on Linux in a source checkout), choose File > New Project... and pick the core. To play a ROM with no project, pass
`--core=<package> <rom>` on the command line.

## Run the gates

CI runs both and both must pass before anything is published.

### Core gate

```
./waterbox/run-gate.sh
```

It needs `build/meson-native/run-native`, `build/meson-native/run-wbx` and
`build/meson-guest/core.wbx`. It needs no Chimera build and no display. `-n <native build dir>` and `-g <guest build dir>` point it at
other build directories. It does not rebuild anything: build both flavors
first.

It runs over the two homebrew ROMs in `tests/roms/` and the movie in
`tests/movies/`, so no check is skipped for lack of content. Six
configurations are run: a recorded movie, a pad exercise on each ROM, a PAL
machine, an unplugged second port and a driving controller. Each prints four
checks:

- `<name>:equivalence`: the sandboxed core and the native reference give the
  same frame count, vsync rate, video hash, audio hash, lag count and memory
  domain digests.
- `<name>:input-shaped`: the run differs from an idle run of the same length,
  so the input reached the machine.
- `<name>:turbo`: with drawing switched off for the first half of the run,
  the machine, the audio, the lag count and the second half's pictures are
  unchanged, and the whole-run video hash differs, which shows that frames
  really went undrawn.
- `<name>:savestate`: saving and loading the whole machine around every frame
  changes nothing.

One more check follows:

- `settings:format`: the same cartridge runs at 60/1 as NTSC and 50/1 as PAL,
  and the two machines differ.

The last line is `<n> ok, <m> failed`. A complete run has 25 checks. The exit
status is non-zero when any check fails.

There is no manifest replay script in this repository. `tests/roms-local/` is
gitignored and is the place for cartridges that may not be distributed, but
no gate reads it.

### Frontend gate

This gate runs the package inside Chimera, headless, with Chimera's window. Build Chimera with its window first, as the workflow does:

```
<chimera>/tools/ci-chimera.sh
```

Then, in this repository, with the package built and `run-native` built:

```
./waterbox/tests/run-frontend.sh --chimera-root <chimera>
```

It needs `<chimera>/build/meson-linux/source/ui/chimera`,
`<chimera>/build/Cores/stella.chimeraCore`, `build/meson-native/run-native`,
and python3. No display is needed: the window runs headless. `--frames N` changes the run
length (default 300). Its checks:

- `cart:frontend`: after 300 idle frames of a homebrew cartridge, the Main RAM
  inside Chimera (all 128 bytes) is byte-identical to the native reference.
- `settings:format`: `format=PAL`, set through the project, matches its
  own native reference and draws more lines than the NTSC machine.
- `keybinds`: the package's `default_keybinds.json` becomes Chimera's default
  bindings for the Atari 2600 controller.

Its logs and dumps stay in `waterbox/tests/work/` (gitignored). CI uploads
that directory when the gate fails.

## Files the core needs at run time

Game files are never in this repository or in the package. The user provides
them. A project is one cartridge ROM, `.a26` or `.bin`. Stella works out the
mapper from the bytes.

`waterbox/waterbox.config` declares no firmware: this core needs no BIOS.

Four settings shape the machine: `port1` and `port2` (`joystick`, `none`,
`driving`), `format` (`AUTO`, `NTSC`, `PAL`, `SECAM`, `NTSC50`, `PAL60`,
`SECAM60`; `AUTO` takes the cartridge's own format) and `randomSeed`, which
pins what the machine powers on with.

## Troubleshooting

- `pass -Dminibox_dir=<miniBox checkout>` from `meson setup`: the native build
  did not find miniBox at `../chimera/extern/chimera-common-minibox`. Pass
  `-Dminibox_dir=<minibox>`.
- `miniBox C++ guest toolchain missing under <minibox>/build/meson-cpp.` from
  `setup-guest.sh`: miniBox is not built with `-Dguest_cpp=true` in
  `build/meson-cpp`. Run the commands of "Build miniBox".
- `run-wbx` does not link: it takes `libminiboxhost.so` from
  `<minibox>/build/meson-linux/source/host`. Build miniBox in
  `build/meson-linux` as well as in `build/meson-cpp`.
- `could not download the GCC <version> source from any mirror` while building
  miniBox: the C++ guest toolchain needs the network once.
- `extern/stella is not checked out` from `apply-patches.sh`: the submodule is
  missing. The message gives the command:
  `git -C <repository> submodule update --init --recursive extern/stella`.
- `extern/stella is partly patched` from `apply-patches.sh`: a touched file is
  neither as upstream has it nor as the series leaves it. The message names
  the files and the way back:

  ```
  git -C extern/stella reset --hard && git -C extern/stella clean -fd && waterbox/apply-patches.sh
  ```

  That discards every edit made in the submodule. Turn edits you want into a
  patch first.
- `the series does not apply to the submodule's HEAD` from
  `apply-patches.sh`: the submodule was moved to another commit and the
  patches were not rebased onto it.
- A change to `default_options` in `meson.build` (the C++ standard, for
  example) has no effect on a build directory that is already configured:
  meson applies them at first configure only. `docs/PLAN.md` records that the
  move to C++23 cost a `--reconfigure`.
- `chimera checkout not found; pass -r <path>` from `build-package.sh`: pass
  `-r <chimera>`.
- `native build missing` or `guest build missing` from `run-gate.sh`: the gate
  builds nothing. Build both flavors first.
- `Chimera not built`, `package not installed` or `native reference not built`
  from `run-frontend.sh`: build Chimera, run `build-package.sh`, and build
  `run-native`, in that order.
- A hand-built package is stamped `-dirty+local` even with nothing edited. The
  applied patches make the submodule's working tree differ from its pin, and
  `git diff --quiet HEAD` counts that as a change.
- `setup-guest.sh` runs `meson setup` with its error output hidden and, when
  that fails, runs `meson setup --reconfigure`. The error you see comes from
  the second command.
- `packaging is not deterministic` from `build-package.sh`: the package was
  written twice and the two files differ. The package's SHA-1 is the core's
  identity, so the script stops.
- The guest prints a complaint about a refused EEPROM write at shutdown. It is
  harmless: `docs/PLAN.md` records that the SaveKey and AtariVox EEPROM is not
  wired.

## Two CPUs

CI builds and tests the core twice, once for each CPU, on a runner of that
CPU (x86_64 on ubuntu-24.04, aarch64 on ubuntu-24.04-arm): a package is
machine code for one CPU, and the guest is built for the CPU it is built on.
A nightly carries both packages, `<id>-<version>-x86_64.chimeraCore` and
`<id>-<version>-aarch64.chimeraCore`. On aarch64 the linked `core.wbx` goes
through miniBox's `wbx-tp-rewrite.py`, which points its thread-pointer reads
at the machine's slot.
