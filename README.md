# chimera-core-stella

[Stella](https://github.com/stella-emu/stella) as a [Chimera](https://github.com/ToolAssisted-run/chimera)
core: an Atari 2600 in miniBox's sandbox, packaged as `stella.chimeraCore`.

**Built on upstream's own headless port.** Stella carries a libretro front end,
and `StellaLIBRETRO` is already the facade a sandboxed core needs - it makes a
console from a rom in memory, runs a frame, and hands back video, audio and
savestates. This repository drives that rather than reaching into `OSystem`, so
the patch set is four small hooks (`patches/`): a call from `M6532::peek`
telling chimera that the machine looked at its input, which is what lag
detection is; a pinned random seed; a frame-driven event handler; and a turbo
switch. Everything else -
the file layer, the console switches, the event-driven controllers - is
upstream's, used as upstream intends.

The integration descends from the author's own BizHawk Stella port: the console
switches as inputs rather than menu state, the memory domain choice, and the
blip resampling of the TIA's ~31.4kHz audio to the 44100 the frontend expects,
at the same rates with the same resampler.

## What it is

- **One machine, three televisions.** The cartridge's own format is taken by
  default (`AUTO`); NTSC, PAL and SECAM can be forced, and that changes the
  frame rate and the picture's height, so it is part of the machine and lives
  in the project.
- **Both controller ports**, each a joystick, a driving controller, or nothing.
- **The console's switches are inputs**: Reset, Select, and the difficulty and
  TV-type switches, which are LEVELS rather than presses - held is A / colour.
- **Main RAM** (all 128 bytes of it) is the memory domain. Stella exposes the
  TIA, the PIA and the system bus through functions rather than buffers, so
  they cannot be handed over as pointers; that is a chimera ABI question rather
  than a Stella one.

## Using it in Chimera

Chimera ships no cores and downloads none. Download
`stella-<version>.chimeraCore` from this repository's
[Releases](https://github.com/ToolAssisted-run/chimera-core-stella/releases)
page (`dev` follows main, `nightly-YYYY-MM-DD` builds are dated), or build it,
and put the file in the `Cores` folder beside `Chimera.exe`. File > Core
Manager lists the cores in that folder. The same file works on Linux and on
Windows. Game files are not included: you provide them.

## Building

`<chimera>` is a Chimera checkout and `<minibox>` is its
`extern/chimera-common-minibox` submodule, built twice: as `build/meson-linux`
(the host library) and as `build/meson-cpp` with `-Dguest_cpp=true` (the C++
guest toolchain).

```
git submodule update --init --recursive
sh waterbox/setup-guest.sh -m <minibox> && ninja -C build/meson-guest core.wbx          # the guest
meson setup build/meson-native -Dminibox_dir=<minibox> && ninja -C build/meson-native   # the reference
./waterbox/build-package.sh -m <minibox> -r <chimera>                                   # stella.chimeraCore
```

The package is written to `<chimera>/build/Cores/stella.chimeraCore`. The full
instructions, from a fresh clone to the gates, are in
[docs/BUILDING.md](docs/BUILDING.md). An AI coding agent should start with
[AGENTS.md](AGENTS.md).

## Gates

```
./waterbox/run-gate.sh                  # native == sandbox == savestate round-trip
./waterbox/tests/run-frontend.sh        # the same machine through Chimera itself
```

The roms under `tests/roms` are homebrew and free to distribute. Commercial
cartridges go in `tests/roms-local` (gitignored).

Status and plan: `docs/PLAN.md`.
