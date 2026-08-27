# chimera-core-stella: Stella in miniBox's sandbox, as a Chimera core.
#
# Native reference build (run-native + run-wbx under the build dir):
#     meson setup build/meson-native && ninja -C build/meson-native
# Guest (core.wbx) build via the cross file waterbox/setup-guest.sh writes
# (needs miniBox's C++ guest toolchain, build/meson-cpp):
#     sh waterbox/setup-guest.sh && ninja -C build/meson-guest core.wbx
#
# THE SOURCE LIST IS UPSTREAM'S OWN. Stella carries a libretro front end, and
# its Makefile.common names exactly the files a headless Stella needs; this is
# that list, minus libretro.cxx - the libretro API shim, which waterbox/
# cinterface.cpp replaces. Both flavors compile the SAME cinterface.cpp; only
# emulibc.h differs (waterbox/native-shim for the reference build).
project('chimera-core-stella', ['c', 'cpp'],
  default_options: ['c_std=gnu11', 'cpp_std=c++23', 'optimization=2', 'debug=false', 'warning_level=0'])

st = 'extern/stella/src'

# the submodule pin is pristine upstream; overlay the chimera patch set
# (idempotent, one hook - see docs/PLAN.md)
run_command(files('waterbox/apply-patches.sh'), check: true)

mb = get_option('minibox_dir')
if mb == ''
  mb_candidate = meson.current_source_dir() / '../chimera/extern/tools/chimera-common-minibox'
  if import('fs').is_dir(mb_candidate)
    mb = mb_candidate
  else
    error('pass -Dminibox_dir=<miniBox checkout> (or keep one at ../chimera/extern/tools/chimera-common-minibox)')
  endif
endif

# __LIB_RETRO__ is how upstream selects its headless OSystem, framebuffer,
# sound and event handler - the same backend this core drives.
defs = [
  '-DNDEBUG',
  '-D__LIB_RETRO__',
  '-DSOUND_SUPPORT',
]
quiet = [
  '-fno-strict-aliasing',
  '-Wall',
  '-Wfatal-errors',
  '-Wno-multichar',
  '-fomit-frame-pointer',
]
add_project_arguments(defs + quiet, language: ['c', 'cpp'])

inc = include_directories(
  'waterbox',
  'waterbox/vendored/blip_buf',
  st,
  st / 'os/libretro',
  st / 'emucore',
  st / 'emucore/elf',
  st / 'emucore/tia',
  st / 'common',
  st / 'common/audio',
  st / 'common/tv_filters',
  st / 'common/repository/sqlite',
  st / 'lib',
  st / 'lib/sqlite',
)

core_sources = files(
  st / 'common/AudioQueue.cxx',
  st / 'common/AudioSettings.cxx',
  st / 'common/Base.cxx',
  st / 'common/Bezel.cxx',
  st / 'common/DevSettingsHandler.cxx',
  st / 'common/FSNodeZIP.cxx',
  st / 'common/FpsMeter.cxx',
  st / 'common/JoyMap.cxx',
  st / 'common/KeyMap.cxx',
  st / 'common/Logger.cxx',
  st / 'common/MouseControl.cxx',
  st / 'common/PJoystickHandler.cxx',
  st / 'common/PKeyboardHandler.cxx',
  st / 'common/PaletteHandler.cxx',
  st / 'common/PhosphorHandler.cxx',
  st / 'common/PhysicalJoystick.cxx',
  st / 'common/RewindManager.cxx',
  st / 'common/StaggeredLogger.cxx',
  st / 'common/StateManager.cxx',
  st / 'common/TimerManager.cxx',
  st / 'common/VideoModeHandler.cxx',
  st / 'common/repository/CompositeKVRJsonAdapter.cxx',
  st / 'common/repository/CompositeKeyValueRepository.cxx',
  st / 'common/repository/KeyValueRepositoryConfigfile.cxx',
  st / 'common/repository/KeyValueRepositoryJsonFile.cxx',
  st / 'common/repository/KeyValueRepositoryPropertyFile.cxx',
  st / 'common/tv_filters/AtariNTSC.cxx',
  st / 'common/tv_filters/NTSCFilter.cxx',
  st / 'emucore/AtariVox.cxx',
  st / 'emucore/Booster.cxx',
  st / 'emucore/Cart.cxx',
  st / 'emucore/Cart03E0.cxx',
  st / 'emucore/Cart0840.cxx',
  st / 'emucore/Cart0FA0.cxx',
  st / 'emucore/Cart2K.cxx',
  st / 'emucore/Cart3E.cxx',
  st / 'emucore/Cart3EPlus.cxx',
  st / 'emucore/Cart3EX.cxx',
  st / 'emucore/Cart3F.cxx',
  st / 'emucore/Cart4A50.cxx',
  st / 'emucore/Cart4K.cxx',
  st / 'emucore/Cart4KSC.cxx',
  st / 'emucore/CartAR.cxx',
  st / 'emucore/CartARM.cxx',
  st / 'emucore/CartBF.cxx',
  st / 'emucore/CartBFSC.cxx',
  st / 'emucore/CartBUS.cxx',
  st / 'emucore/CartCDF.cxx',
  st / 'emucore/CartCM.cxx',
  st / 'emucore/CartCTY.cxx',
  st / 'emucore/CartCV.cxx',
  st / 'emucore/CartCreator.cxx',
  st / 'emucore/CartDF.cxx',
  st / 'emucore/CartDFSC.cxx',
  st / 'emucore/CartDPC.cxx',
  st / 'emucore/CartDPCPlus.cxx',
  st / 'emucore/CartDetector.cxx',
  st / 'emucore/CartDevCard.cxx',
  st / 'emucore/CartE0.cxx',
  st / 'emucore/CartE7.cxx',
  st / 'emucore/CartEF.cxx',
  st / 'emucore/CartEFF.cxx',
  st / 'emucore/CartEFSC.cxx',
  st / 'emucore/CartELF.cxx',
  st / 'emucore/CartEnhanced.cxx',
  st / 'emucore/CartF0.cxx',
  st / 'emucore/CartF4.cxx',
  st / 'emucore/CartF4SC.cxx',
  st / 'emucore/CartF6.cxx',
  st / 'emucore/CartF6SC.cxx',
  st / 'emucore/CartF8.cxx',
  st / 'emucore/CartF8SC.cxx',
  st / 'emucore/CartFA.cxx',
  st / 'emucore/CartFA2.cxx',
  st / 'emucore/CartFC.cxx',
  st / 'emucore/CartFE.cxx',
  st / 'emucore/CartGL.cxx',
  st / 'emucore/CartJANE.cxx',
  st / 'emucore/CartMDM.cxx',
  st / 'emucore/CartMVC.cxx',
  st / 'emucore/CartSB.cxx',
  st / 'emucore/CartTVBoy.cxx',
  st / 'emucore/CartUA.cxx',
  st / 'emucore/CartWD.cxx',
  st / 'emucore/CartWF8.cxx',
  st / 'emucore/CartX07.cxx',
  st / 'emucore/CompuMate.cxx',
  st / 'emucore/CompuMateCassette.cxx',
  st / 'emucore/Console.cxx',
  st / 'emucore/Control.cxx',
  st / 'emucore/ControllerDetector.cxx',
  st / 'emucore/CortexM0.cxx',
  st / 'emucore/DispatchResult.cxx',
  st / 'emucore/Driving.cxx',
  st / 'emucore/EmulationTiming.cxx',
  st / 'emucore/EmulationWorker.cxx',
  st / 'emucore/EventHandler.cxx',
  st / 'emucore/FBMessageHandler.cxx',
  st / 'emucore/FBSurface.cxx',
  st / 'emucore/FSNode.cxx',
  st / 'emucore/FrameBuffer.cxx',
  st / 'emucore/Genesis.cxx',
  st / 'emucore/GlobalKeyHandler.cxx',
  st / 'emucore/Joy2BPlus.cxx',
  st / 'emucore/Joystick.cxx',
  st / 'emucore/Keyboard.cxx',
  st / 'emucore/KidVid.cxx',
  st / 'emucore/Lightgun.cxx',
  st / 'emucore/M6502.cxx',
  st / 'emucore/M6532.cxx',
  st / 'emucore/MD5.cxx',
  st / 'emucore/MindLink.cxx',
  st / 'emucore/OSystem.cxx',
  st / 'emucore/Paddles.cxx',
  st / 'emucore/PlusROM.cxx',
  st / 'emucore/PointingDevice.cxx',
  st / 'emucore/Props.cxx',
  st / 'emucore/PropsSet.cxx',
  st / 'emucore/QuadTari.cxx',
  st / 'emucore/SaveKey.cxx',
  st / 'emucore/Serializer.cxx',
  st / 'emucore/Settings.cxx',
  st / 'emucore/Switches.cxx',
  st / 'emucore/System.cxx',
  st / 'emucore/TIASurface.cxx',
  st / 'emucore/Thumbulator.cxx',
  st / 'emucore/elf/BusTransactionQueue.cxx',
  st / 'emucore/elf/ElfEnvironment.cxx',
  st / 'emucore/elf/ElfLinker.cxx',
  st / 'emucore/elf/ElfParser.cxx',
  st / 'emucore/elf/ElfUtil.cxx',
  st / 'emucore/elf/VcsLib.cxx',
  st / 'emucore/tia/AnalogReadout.cxx',
  st / 'emucore/tia/Audio.cxx',
  st / 'emucore/tia/AudioChannel.cxx',
  st / 'emucore/tia/Background.cxx',
  st / 'emucore/tia/Ball.cxx',
  st / 'emucore/tia/DrawCounterDecodes.cxx',
  st / 'emucore/tia/LatchedInput.cxx',
  st / 'emucore/tia/Missile.cxx',
  st / 'emucore/tia/Player.cxx',
  st / 'emucore/tia/Playfield.cxx',
  st / 'emucore/tia/TIA.cxx',
  st / 'emucore/tia/frame-manager/AbstractFrameManager.cxx',
  st / 'emucore/tia/frame-manager/FrameLayoutDetector.cxx',
  st / 'emucore/tia/frame-manager/FrameManager.cxx',
  st / 'emucore/tia/frame-manager/JitterEmulation.cxx',
  st / 'os/libretro/FSNodeLIBRETRO.cxx',
  st / 'os/libretro/StellaLIBRETRO.cxx',
  'waterbox/vendored/blip_buf/blip_buf.c',
  'waterbox/cinterface.cpp',
)

if meson.is_cross_build()
  # ---- the guest: core.wbx --------------------------------------------------
  # miniBox's musl/libstdc++ toolchain arrives through the cross file; the link
  # recipe is the C++ guest kit's (cxxglue for the unwinder, weak pthread pulls
  # for libgcc_eh) - the chimera-core-dosbox-x recipe.
  mbdir = meson.get_external_property('minibox_dir')
  mbuild = meson.get_external_property('minibox_build')
  sr = meson.get_external_property('guest_sysroot')

  executable('core', core_sources,
    name_suffix: 'wbx',
    include_directories: inc,
    link_args: [
      '-static', '-no-pie',
      '-Wl,--eh-frame-hdr', '-Wl,-O2', '-Wl,--no-relax', '-Wl,-z,stack-size=8388608',
      '-T', mbdir / 'source/guest/linkscript.T',
      '-Wl,-u,pthread_once', '-Wl,-u,pthread_cond_wait',
      '-Wl,-u,pthread_cond_broadcast', '-Wl,-u,pthread_key_create',
      mbuild / 'source/guest/cxxglue.c.o',
      mbuild / 'source/guest/emulibc.c.o',
      '-L' + sr / 'lib', '-lstdc++', '-lgcc', '-lgcc_eh', '-lc'])
else
  # ---- the native reference -------------------------------------------------
  m = meson.get_compiler('c').find_library('m', required: false)
  executable('run-native', core_sources + files('waterbox/run-native.c'),
    include_directories: [include_directories('waterbox/native-shim'), inc],
    c_args: ['-I' + mb / 'source/guest/include', '-I' + mb / 'extern/jsmn'],
    cpp_args: ['-I' + mb / 'source/guest/include', '-I' + mb / 'extern/jsmn'],
    dependencies: [m])

  mbhost = mb / 'build/meson-linux/source/host'
  executable('run-wbx', files('waterbox/run-wbx.c'),
    c_args: ['-I' + mb / 'source/host'],
    link_args: [mbhost / 'libminiboxhost.so', '-Wl,-rpath,' + mbhost])
endif
