# AGENTS.md - melonDS core for Chimera

This repository builds melonDS (https://melonds.kuribo64.net/) - the
Nintendo DS and the Nintendo DSi - as a core for Chimera
(https://github.com/ToolAssisted-run/chimera), a frontend for tool-assisted
speedruns. It produces one file, `melonds.chimeraCore`: melonDS's machine
compiled to run inside Chimera's sandbox (miniBox), a program around it, and
the declarations Chimera reads (buttons, settings, file types, firmware).
melonDS's own program - its window, sound output and menus - is not built.

Read [docs/BUILDING.md](docs/BUILDING.md) before building and
[docs/PLAN.md](docs/PLAN.md) before changing how the core works: it is the
design log, with what was decided, why, and what was measured.

## Layout

- `extern/melonDS` - melonDS at its release 1.1, a submodule. Never edited
  in place: a change is a numbered patch in `patches/`, laid over the
  submodule by `waterbox/apply-patches.sh` and written by
  `waterbox/make-patches.sh`. One so far: 0001 runs the ARM recompiler
  with no fast memory (no shared memory file, no fault handler).
- `meson.build` - the build of both flavors, with melonDS's files listed by
  hand from its own `src/CMakeLists.txt`.
- `waterbox/cinterface.cpp` - the functions Chimera's engine calls, over
  melonDS's `NDS` and `DSi` classes. The same file is compiled for the
  sandbox and for the host.
- `waterbox/platform.cpp` - what the machine asks of its program (melonDS's
  `Platform.h`): files, threads, time, the microphone, the cameras.
  `waterbox/frontend.h` is what it asks of `cinterface.cpp`.
- `waterbox/waterbox.config`, `file_slots.json`, `default_keybinds.json`,
  `package-licenses.json` - what the package declares, written by hand.
- `waterbox/run-native.c`, `waterbox/run-wbx.c` - the native runner and the
  sandbox runner. They take the same options and print the same lines
  (`waterbox/gate-harness.h` is the part they share).
- `waterbox/*.sh` - `apply-patches.sh`, `make-patches.sh`, `setup-guest.sh`
  (configures the sandbox build), `build-package.sh`, `run-gate.sh` (the
  test script).
- `tests/` - `make-nds-program.py` (assembles the DS program the tests
  run), `run-frontend.sh` (the window test, with its helpers),
  `make-project.py` (writes a project for a game).
- `build/` - every build output. `tests/roms-local/` - BIOS and firmware
  files, and `tests/games-local/` - games, for the tests that need them.
  All three are ignored by git.

## Build and test

```sh
git submodule update --init
meson setup build/meson-native -Dminibox_dir=<miniBox> && ninja -C build/meson-native
waterbox/setup-guest.sh -m <miniBox> && ninja -C build/meson-guest
waterbox/build-package.sh -r <chimera checkout>
CHIMERA_ROOT=<chimera checkout> waterbox/run-gate.sh
TMPDIR=$PWD/build/t bash tests/run-frontend.sh --chimera-root <chimera checkout>
```

Run both test scripts before every commit that touches what they cover, and
say in the commit what ran, with the numbers. A new test is broken on
purpose once and seen to fail.

## Rules of the repository

- **One thread.** A core runs on one thread on one clock. The 3D renderer
  is made without its thread; `Platform::Thread_Create` stops the program.
- **No host clock.** The machine's time is the cycles it has run, and its
  calendar clock starts at the Starting Clock setting.
- **The machine's state is its memory.** A savestate is the sandbox's copy
  of the core's memory, so nothing a game can change may live in a file. A
  save stays in the cartridge's memory. The DSi's NAND, which the machine
  writes while it runs, is read whole into memory and read and written
  there (`platform.cpp`, the files kept in memory).
- **The controls are in two places** that must agree: `kButtonTable` and
  `kAxisNames` in `cinterface.cpp`, and both machines' `input` in
  `waterbox.config`. The test `package:controls` compares them.
- **The picture's layout never reaches the machine.** The test
  `nds:layouts` holds that.
- **The pen's place is a fraction of the whole picture**, because that is
  what Chimera makes of the mouse's place. The core turns it back through
  the layout, the gap, the exchange and the turn into a pixel of the lower
  screen (`pen_on_lower_screen`). The test `nds:pen-layouts` holds that.
- **The test program keeps no trace of a press that is over.** A test that
  compares memory at the end holds the button to the end. A movie's row N
  is the test runner's frame N + 1.
- **No BIOS, no firmware, no NAND image, no game** is committed, uploaded
  to CI or attached to an issue. Test content stays under
  `tests/roms-local` and `tests/games-local`, and is copied there, never
  linked from where the owner keeps it.
- **Say what was not tried.** A machine that built and never ran a game has
  not been shown to work.
- **Write in plain language**: commit messages, documents, comments and
  setting descriptions are read by people who have never seen this
  repository. `.md` files are ASCII.
- **Commits**: `type(scope): what changed`; a short body with what changed,
  why, and how it was tested, with the numbers. Stage explicit paths. No
  amending, no force push.

## What is the owner's to decide

Publishing, the roster row in Chimera, a new melonDS version, and anything
a licence does not plainly allow.
