# Building

This core is built on Linux. The result, `core.wbx`, is one file that runs in
Chimera on Linux and on Windows.

## What is needed

- GCC 13 or newer, meson, ninja, git, python3.
- A checkout of Chimera with miniBox built, for the sandbox toolchain
  (Chimera's `AGENTS.md`, "Build"). Below, `<miniBox>` is its
  `extern/chimera-common-minibox` folder.

## melonDS's source

melonDS is a submodule, `extern/melonDS`, at the commit of its release 1.1:

```sh
git submodule update --init
```

A change to melonDS is kept as a numbered patch in `patches/`.
`waterbox/apply-patches.sh` lays the patches over the submodule (the build
runs it; it does nothing to a tree that has them). To make a patch, edit the
files in `extern/melonDS`, build and test, and run
`waterbox/make-patches.sh`. So far no patch has been needed.

## What is compiled

`meson.build` lists melonDS's files: the same list as its own
`src/CMakeLists.txt`, without the recompiler, the OpenGL renderers, the
network code and the debugger stub. melonDS's Qt program is not compiled.
`waterbox/platform.cpp` answers what the machine asks of its program
(melonDS's `Platform.h`), and `waterbox/cinterface.cpp` is what Chimera's
engine calls. `waterbox/config/version.h` is written by hand in place of the
one melonDS's CMake build generates.

## The two flavors

- `native`: for the host. `run-native` is the reference the tests compare
  the sandbox with.
- `guest`: for miniBox's sandbox. This is `core.wbx`, and `run-wbx` runs it.

```sh
meson setup build/meson-native -Dminibox_dir=<miniBox> && ninja -C build/meson-native
waterbox/setup-guest.sh -m <miniBox> && ninja -C build/meson-guest
```

Both runners take a work folder that holds what the sandbox would see: the
game under its name, a file `slots` (`{"cart":["game.nds"],"savedata":[]}`)
and a file `settings` (`{}` or, say, `{"machine":"dsi"}`), and the firmware
files under the names the package gives them. `waterbox/gate-harness.h`
lists the options they share (`--frames`, `--report`, `--press`, `--axis`,
`--vid-out`, `--dump-domain`, `--savedata-out`, `--list-panel`).

## The package

```sh
waterbox/build-package.sh -r <chimera checkout>
```

builds the sandbox core and writes `build/Cores/melonds.chimeraCore` in
that checkout. `tests/make-project.py` writes a project for a game, which
Chimera's `chimera-run --project` then plays.

The four declarations in `waterbox/` are written by hand:
`waterbox.config` (the two machines, their controls, the settings, the
firmware), `file_slots.json`, `default_keybinds.json` and
`package-licenses.json`. The test `package:controls` fails when the
controls in `waterbox.config` are not the ones `cinterface.cpp` has.

## The tests

```sh
CHIMERA_ROOT=<chimera checkout> waterbox/run-gate.sh
```

runs every test and says, for each, what was compared. The "nds:" tests need
nothing: `tests/make-nds-program.py` assembles a small DS program, and the
DS needs no BIOS here. The others need files that cannot be in this
repository, and say SKIP without them:

| Tests | What they need | Where the script looks | Or name it with |
|---|---|---|---|
| `game:` | a DS game, as a folder with a `.nds` file | `tests/games-local/nsmb` | `NDS_GAME` |
| `pen:` | a DS game played with the pen | `tests/games-local/kirby` | `NDS_GAME_PEN` |
| `bios:` | the DS's two BIOS files and its firmware | `tests/roms-local/bios7.bin`, `bios9.bin`, `firmware.bin` | `NDS_BIOS7`, `NDS_BIOS9`, `NDS_FIRMWARE` |
| `dsi:` | a DSi game, as a folder with a `.nds` file | `tests/games-local/dsi` | `DSI_GAME` |
| | a DSi's two BIOS files, firmware and NAND image | `tests/roms-local/bios7i.bin`, `bios9i.bin`, `dsi_firmware.bin`, `dsi_nand.bin` | `DSI_BIOS7`, `DSI_BIOS9`, `DSI_FIRMWARE`, `DSI_NAND` |

Copy the files there; do not link them from where you keep them. Without
`CHIMERA_ROOT` the tests that run the package in Chimera's engine are
skipped.

`bash tests/run-frontend.sh --chimera-root <chimera checkout>` is the window
test: the package in Chimera's own window (under Mono, on a private Xvfb
display). It takes the same files. Mono limits the length of a path it
makes under the temporary folder, so give it a short one
(`TMPDIR=$PWD/build/t`) when the default is long.

## What CI does

`.github/workflows/chimera.yml` builds the core from a fresh clone, runs
both test scripts and Chimera's checks of the package, and publishes the
package. No game and no BIOS of Nintendo's may be put on a runner, so the
tests that need one are skipped there; the "nds:" tests, with the program
the tests assemble, run in full.
