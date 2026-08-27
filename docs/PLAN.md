# Stella as a Chimera core: plan and log

## What this is

An Atari 2600 for chimera, built from upstream `stella-emu/stella` (submodule,
pinned) plus three small patches, with the integration descended from the
author's own BizHawk Stella port.

## The decision that shaped it

BizHawk's port drives `OSystem` directly through its own `BizhawkInterface.cxx`
and a `port/` directory of SDL replacements (OSystem, framebuffer, sound and
event-handler shims), against a `TASEmulators/stella` fork. That fork is small
(15 files against upstream) but most of it exists to make Stella headless.

Upstream has since grown its own libretro front end, and `StellaLIBRETRO` does
exactly that job: `create()` from a rom in memory, `runFrame()`, `getVideoBuffer()`,
`getAudioBuffer()`, `saveState()`/`loadState()`, and input as `Event` types.
Building on THAT rather than on the fork means:

- no OSystem/framebuffer/sound/FSNode surgery at all - `-D__LIB_RETRO__`
  selects upstream's own headless backend;
- savestates for free, through upstream's serializer;
- three patches, each a weak hook a build without a host does not notice:
  - `M6532::peek` calls `chimera_input_was_read()` for SWCHA and SWCHB, the two
    addresses a 2600 program reads its controllers and console switches
    through. That is lag detection, and upstream has no hook for it.
  - `Console` and `OSystem` ask `chimera_pinned_random_seed()` before seeding
    the machine's randomness off the wall clock (power-up RAM, the CPU
    registers, where the RIOT timer sits).
  - `EventHandler::reset` asks `chimera_frame_driven()` before arming its
    500 ms "clear held events" timeout, which is a wall clock on a background
    thread.

  The last two are the same point: a machine is a function of its PROJECT, not
  of the moment it was started.

What `libretro.cxx` would have provided, `waterbox/cinterface.cpp` provides
instead: a logger, a message sink, `update_input()`, and a file interface. The
file interface is real - it is stdio over the sandbox's own mounted files -
because upstream's file layer asks whether the rom node IS a file before it
looks at the bytes, and answering that honestly beats patching the question
away.

## Milestones

- **M1 DONE** (2026-08-27): the core builds for both flavors, boots a
  cartridge, and the gates are green.
  - `waterbox/run-gate.sh`: 19/19. Six configurations (a recorded movie, two
    roms, PAL, an unplugged port, a driving controller) each proving native ==
    waterboxed over 300-1034 frames, that the input schedule shaped the machine, and that a
    whole-machine savestate round-trip around every frame is lossless; plus a
    format leg (NTSC 60/1 vs PAL 50/1, and different machines).
  - `waterbox/tests/run-frontend.sh`: 3/3. The frontend builds the same machine
    (128 bytes of RAM identical to the native reference), a format setting
    reaches the guest through the package (PAL draws 274 lines against NTSC's
    228), and the package's keybinds become the frontend's defaults.

## Sharp edges hit

- **Two input channels.** A controller of 64 buttons or fewer arrives from the
  frontend as a PACKED mask in `FrameAdvance`; the gate harness (and any wide
  controller) drives `SetButton`. Unpacking the mask over the button array
  wiped what `SetButton` had set, so the exercise input changed nothing and the
  gate said so. A frame is now the UNION of the two channels, each keeping its
  own state.
- **The input window.** Upstream calls `update_input()` from its event
  handler's `pollEvent()`, which only runs inside `pollInput()` - the frame
  loop has to open that window explicitly, or the controllers replay the
  previous frame's input forever.
- **The rom must be a file.** Upstream's newest file layer routes `isFile()`
  through the VFS, so with no VFS a rom node is not a file and Stella refuses
  the cartridge as an "Unrecognized ROM file type" before reading a byte.
- **A machine that listened to the clock.** The gate's native reference is not
  only a check that the sandbox behaves - it is the only place a host-side
  clock or thread still ticks, and it caught two things the sandbox could never
  show. Power-up state (`plr.ramrandom`, `plr.cpurandom`) is seeded from
  `TimerManager::getTicks()`, so every boot was a different machine; and
  `EventHandler::reset` arms a 500 ms timeout, on a background thread, that
  clears every event once emulation starts - which lands on whatever frame the
  run happened to reach, and wipes the console switches between a poll and the
  program's read of them. The symptom was a single RAM byte differing across
  runs at identical cycle counts: same code, same timing, one different read.
  Both are now pinned by patch (see above).
- **C++23.** Upstream uses `#elifdef`, so the guest toolchain has to be asked
  for C++23; meson's `default_options` only apply at first configure, which
  costs a `--reconfigure` when it changes.

## What remains

- **Memory domains beyond RAM.** BizHawk exposes TIA, PIA and the system bus
  through peek/poke delegates. Chimera's ABI hands over pointers, so those
  three cannot travel as they are; either the ABI grows a callback-backed
  domain or they stay out.
- **Paddles, the Keyboard controller, the CompuMate, QuadTari.** Upstream
  supports them; the wire here is joystick/driving/none, the same set the
  author's BizHawk port shipped.
- **SaveKey/AtariVox EEPROM.** A 2600 cartridge has no save data, but those
  controllers write to one. Nothing is wired, and the guest prints a harmless
  complaint when its EEPROM write is refused at shutdown.
- **Aspect ratio per format.** The package declares one virtual size; a PAL
  machine's picture is taller. The frontend now supports several MACHINES in
  one package (see chimera's docs/project.md), which is where this belongs.
- **More movie legs.** One `.sol` playaround now replays in the gate; the other
  legs still exercise input on a schedule. More recorded runs, over more roms,
  would test more of the machine.
- **Commercial roms.** `tests/roms-local` (gitignored) is where a manifest
  replay over real cartridges would go; only homebrew ships in the repository.
