# chimera-core-melonds: melonDS's Nintendo DS in miniBox's sandbox, as a
# Chimera core.
#
# Native reference build (run-native + run-wbx under the build directory):
#     meson setup build/meson-native -Dminibox_dir=<miniBox> && ninja -C build/meson-native
# Guest (core.wbx) build via the cross file waterbox/setup-guest.sh writes
# (needs miniBox's C++ guest toolchain, build/meson-cpp):
#     sh waterbox/setup-guest.sh -m <miniBox> && ninja -C build/meson-guest core.wbx
#
# melonDS builds with CMake: a library of the machine ("core", src/) and a Qt
# program around it. Only the machine is built here, from the same list of
# files as src/CMakeLists.txt, without its recompiler, its OpenGL renderers
# and its network code. waterbox/ is the program around it: what the machine
# asks of its host (waterbox/platform.cpp) and the functions Chimera calls
# (waterbox/cinterface.cpp). Both flavours compile the SAME sources; only
# emulibc.h differs (waterbox/native-shim for the reference).
project('chimera-core-melonds', ['c', 'cpp'],
  default_options: ['c_std=gnu11', 'cpp_std=c++17', 'optimization=2', 'debug=false', 'warning_level=0'])

md = 'extern/melonDS/src'

# the submodule pin is upstream as it is; the patches are laid over it
# (waterbox/apply-patches.sh does nothing to a tree that has them)
run_command(files('waterbox/apply-patches.sh'), check: true)

mb = get_option('minibox_dir')
if mb == ''
  mb_candidate = meson.current_source_dir() / '../chimera/extern/chimera-common-minibox'
  if import('fs').is_dir(mb_candidate)
    mb = mb_candidate
  else
    error('pass -Dminibox_dir=<miniBox checkout> (or keep one at ../chimera/extern/chimera-common-minibox)')
  endif
endif

defs = [
  '-DNDEBUG',
  # what the patches test for
  '-DCHIMERA_CORE',
]
# melonDS's own choice (src/CMakeLists.txt): signed overflow wraps. And one
# answer from a floating-point expression on every machine: no contraction
# into fused multiply-adds, which a compiler may otherwise choose where the
# host has them.
flags = [
  '-fwrapv',
  '-fno-strict-aliasing',
  '-ffp-contract=off',
  '-Wno-invalid-offsetof',
]
add_project_arguments(defs + flags, language: ['c', 'cpp'])

inc = include_directories(
  'waterbox',
  'waterbox/config',
  md,
  md / 'teakra/include',
  md / 'teakra/src',
)

core_names = [
  'ARCodeFile.cpp', 'ARDatabaseDAT.cpp', 'AREngine.cpp',
  'ARM.cpp', 'ARMInterpreter.cpp', 'ARMInterpreter_ALU.cpp', 'ARMInterpreter_Branch.cpp', 'ARMInterpreter_LoadStore.cpp',
  'CP15.cpp', 'CRC32.cpp', 'DMA.cpp', 'DMA_Timings.cpp',
  'DSi.cpp', 'DSi_AES.cpp', 'DSi_Camera.cpp', 'DSi_DSP.cpp', 'DSi_I2C.cpp', 'DSi_I2S.cpp', 'DSi_NAND.cpp',
  'DSi_NDMA.cpp', 'DSi_NWifi.cpp', 'DSi_SD.cpp', 'DSi_SPI_TSC.cpp',
  'FATIO.cpp', 'FATStorage.cpp', 'GBACart.cpp', 'GBACartMotionPak.cpp',
  'GPU.cpp', 'GPU2D.cpp', 'GPU2D_Soft.cpp', 'GPU3D.cpp', 'GPU3D_Soft.cpp', 'GPU3D_Texcache.cpp',
  'Mic.cpp', 'NDS.cpp', 'NDSCart.cpp', 'NDSCartR4.cpp', 'ROMList.cpp', 'FreeBIOS.cpp', 'RTC.cpp', 'Savestate.cpp',
  'SPI.cpp', 'SPI_Firmware.cpp', 'SPU.cpp', 'Utils.cpp', 'Wifi.cpp', 'WifiAP.cpp',
  'DSP_HLE/UcodeBase.cpp', 'DSP_HLE/AACUcode.cpp', 'DSP_HLE/G711Ucode.cpp', 'DSP_HLE/GraphicsUcode.cpp',
  'fatfs/ff.c', 'fatfs/ffsystem.c', 'fatfs/ffunicode.c',
  'sha1/sha1.c', 'tiny-AES-c/aes.c', 'xxhash/xxhash.c', 'blip-buf/blip_buf.c',
  # Teakra, the DSi's sound processor (src/teakra/src/CMakeLists.txt)
  'teakra/src/ahbm.cpp', 'teakra/src/apbp.cpp', 'teakra/src/btdmp.cpp', 'teakra/src/disassembler.cpp',
  'teakra/src/dma.cpp', 'teakra/src/timer.cpp', 'teakra/src/memory_interface.cpp', 'teakra/src/mmio.cpp',
  'teakra/src/parser.cpp', 'teakra/src/processor.cpp', 'teakra/src/teakra.cpp',
]
core_sources = files('waterbox/platform.cpp', 'waterbox/cinterface.cpp')
foreach n : core_names
  core_sources += files(md / n)
endforeach

if meson.is_cross_build()
  # ---- the guest: core.wbx --------------------------------------------------
  # miniBox's musl and libstdc++ arrive through the cross file. The -u lines
  # name the pthread functions libgcc's unwinder refers to weakly: a weak
  # reference is not enough to pull them out of a static libc, and it would
  # call address zero.
  mbdir = meson.get_external_property('minibox_dir')
  mbuild = meson.get_external_property('minibox_build')
  sr = meson.get_external_property('guest_sysroot')

  executable('core', core_sources + files('waterbox/guest-syscalls.cpp'),
    name_suffix: 'wbx',
    include_directories: inc,
    link_args: [
      '-static', '-no-pie',
      '-Wl,--eh-frame-hdr', '-Wl,-O2', '-Wl,--no-relax', '-Wl,-z,stack-size=8388608',
      '-T', mbdir / 'source/guest/linkscript.T',
      '-Wl,-u,pthread_once', '-Wl,-u,pthread_cond_wait',
      '-Wl,-u,pthread_cond_broadcast', '-Wl,-u,pthread_key_create',
      '-Wl,-u,pthread_mutexattr_init', '-Wl,-u,pthread_mutexattr_settype',
      '-Wl,-u,pthread_mutexattr_destroy',
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
    link_args: [mbhost / 'libminiboxhost.so'],
    build_rpath: mbhost)
endif
