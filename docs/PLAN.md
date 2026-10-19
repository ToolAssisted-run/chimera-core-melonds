# Plan and design log - melonDS core for Chimera

This is the design log of the core: what was decided, why, and what was
measured. Entries are dated. Read it before changing how the core works.

## Why this core (2026-10-13)

The owner wants every system BizHawk supports to be supported in Chimera
too. After the Mednafen core, the Nintendo DS was the largest gap, and the
owner said to go ahead with it on melonDS, the emulator BizHawk itself uses
for the DS.

## What was read (2026-10-13)

melonDS 1.1 (tag `1.1`, commit b86390e, 2025-11-18), from its source.

## The eight questions (docs/new-core.md in chimera)

1. **Is it a new core?** Yes. No core here runs the Nintendo DS.
2. **May it be distributed and modified?** Yes: GPL version 3 or later. The
   package is GPL-3.0-or-later.
3. **One thread on one clock?** Yes. The machine (`src/`, the library
   melonDS calls "core") asks its program for everything outside itself
   through one header, `Platform.h`. Its only thread is the software 3D
   renderer's, and that is optional: the renderer is made without it. Its
   time is the cycles it has run. The host's clock is read only by the
   network code, which is not built.
4. **Where does the picture come from?** The machine's own framebuffers: two
   screens of 256x192, drawn by melonDS's software renderers. Its OpenGL
   renderers are not built.
5. **Does it recompile?** It can, and here it does not: the interpreter is
   used. (BizHawk does the same.) The recompiler's fast memory path works by
   faults, which the sandbox does not have.
6. **What must the user bring?** For a DS game, only the game. melonDS
   carries a free BIOS and writes a firmware for itself, and starts a game
   directly. The DSi needs Nintendo's BIOS files, firmware and a NAND image.
7. **What can a public runner test?** A small DS program that the tests
   build themselves, since the DS needs no BIOS here.
8. **How big is it?** Small: 4 MB of main memory (16 MB on a DSi), and a
   core.wbx of 5 MB.

## Steps

| Step | What | Proof | State |
|---|---|---|---|
| 1 | Native build; a game boots | Picture, sound, a press changes the run | done 2026-10-13 |
| 2 | The same in the sandbox | Native and sandbox give the same digests; savestates | done 2026-10-13 |
| 3 | The package: controls, settings, screens | Chimera's package checks; a project in Chimera's engine and window | done 2026-10-13 except the window: see "What step 3 decided" |
| 4 | Touch screen, microphone, lid; save data; the clock | Tests for each | done 2026-10-13: see "The tests" |
| 5 | The DSi | A DSi game boots with the owner's BIOS files | done 2026-10-13: see "The DSi" |
| 6 | Tests on a self-made program, Windows, documents, CI | The test script on a runner | done 2026-10-13 except a run on a CI runner (the repository is not published): see "Windows, and what a runner will test" |
| 7 | Publishing | The owner's word | waits for the owner |

## What the first two steps found (2026-10-13)

- The machine is built from the same file list as melonDS's own
  `src/CMakeLists.txt`, without the recompiler, the OpenGL renderers, the
  network code and the debugger stub. No patch was needed.
- New Super Mario Bros. (USA) boots with the free BIOS and reaches its menu:
  1,200 frames take 3.6 seconds natively and 3.2 in the sandbox.
- Native and sandbox give the same 24 digests of RAM, picture and sound over
  1,200 frames with presses of A and Start. A state saved and loaded before
  every frame, a state moved to a new sandbox half way, and a state kept at
  frame 600 and loaded at frame 900 change nothing.
- The sound was first asked for at 48 kHz; step 3 settled the rate.

## What step 3 decided (2026-10-13)

- **System id `NDS`, "Nintendo DS".** One machine for now; the DSi will be a
  second one.
- **The sound is handed out at the DS's own rate**: one stereo pair every
  512 cycles of the second processor, 32,728.5 a second, about 547 a frame.
  melonDS is asked for exactly that rate, so it converts nothing. The 3DS
  core does the same with the same number. The machine's memory does not
  depend on the rate asked for (the same RAM digest at 48 kHz and at this
  rate).
- **Controls**: Up, Down, Left, Right, Start, Select, B, A, Y, X, L, R,
  Touch and Lid Closed, and three axes. Touch X and Touch Y are a fraction
  from 0 to 65535, as every pointer in Chimera's newer cores is (first of
  the lower screen; of the whole picture since 2026-10-14, see "The pen
  follows the picture"). Mic Volume
  is 0 to 100: the microphone hears noise that loud, from a generator kept
  in the machine's memory, and silence at 0.
- **The two screens are one picture**, laid out by settings that change the
  picture only: Screen Layout (stacked, side-by-side, top, bottom), Swap
  Screens, Screen Gap (0 to 192 pixels) and Rotation (0, 90, 180, 270). The
  machine's memory is the same under every layout.
- **BIOS: free by default, the console's own as a setting.** With "original"
  the project needs bios7.bin, bios9.bin (both known by their SHA-1) and
  firmware.bin, and "Start With" can then be the console's menu. With the
  free BIOS a game is started directly.
- **Language** is a setting, written into the firmware's user settings,
  also over a firmware file's own. The touch screen's calibration needs no
  care: melonDS overwrites it with its own in any firmware.
- **The clock** starts at a time the project sets (Starting Clock), the
  first of January 2000 by default.
- **Save data**: the cartridge's save memory goes out as `nds.sav` and a
  project's Save data slot takes a `.sav` file.

What was measured: Chimera's package checks pass (18 of 18). A project of
New Super Mario Bros. plays a 1,200-frame movie with presses in Chimera's
headless runner in 3.2 seconds and leaves the same Main RAM as the sandbox
runner given the same presses (a movie's row N is the test runner's frame
N+1), and another RAM than the idle movie. Kirby: Canvas Curse plays its
opening. With the owner's BIOS and firmware files the console's menu comes
up, with the game in it and the clock on the first of January 2000. Side by
side with a gap of 32 the picture is 544x192; turned a quarter with the
screens swapped it is 384x256.

`tests/make-project.py` kept one press for each control (the last one
given). It now keeps them all. The copy in the Mednafen core has the same
fault.

## The tests (2026-10-13)

**A program of the tests' own.** No game may be put in the repository or on
a CI runner, and the DS needs no BIOS here, so `tests/make-nds-program.py`
assembles a small DS program from a handful of ARM instructions written out
in the script. Its main processor copies the buttons to memory, counts, and
paints both screens; its second processor starts a tone, copies the second
button register (X, Y, the pen, the lid), and asks the touch screen's chip
for the pen's place and the microphone's level. So every control is tested
with no game: each of the twelve buttons clears its own bit and no other,
the pen held at three quarters across and a quarter down reads as pixel
192,48, the lid and the pen show in the register, and the microphone reads
silence at volume 0 and noise at 100.

**The test script**, `waterbox/run-gate.sh`: 34 tests with every file and
Chimera's engine, 34 passed, in about a minute and a half. 18 of them need
no file of anyone's. With New Super Mario Bros.: the game runs, native and
sandbox give the same digests with presses, the three savestate tests, lag
frames (226 of 1,500), the cartridge's save memory out and in, the Language
setting, the console's own BIOS, the console's menu, and the Starting Clock
(the menu shows the date). With Kirby: Canvas Curse: the pen changes the
run, and the pen in another place changes it again. In Chimera's engine a
project of the program, of the game, and of the game started from the
console's menu each leave the Main RAM the sandbox runner leaves.

**The window test**, `tests/run-frontend.sh`: 5 passed. In Chimera's window
the program and the game leave the headless runner's Main RAM, the keys are
taken up, a project that needs the console's files is refused without them
(a file is named), and opens with them from Chimera's firmware.

**Broken on purpose once**: with the pen's place ignored, the lid ignored,
no noise from the microphone, every frame said to have read the buttons,
the gap ignored, the language ignored, the save file not loaded, A and B
exchanged and the clock setting ignored, ten tests failed: `nds:buttons`,
`nds:pen-and-lid`, `nds:microphone`, `nds:lag`, `nds:layouts`, `game:lag`,
`game:save-data`, `game:language`, `bios:clock` and `pen:game`.

A trap for whoever writes the next test: the program keeps no trace of a
press that is over, so a test that compares memory at the end has to hold
the button to the end.

## The DSi (2026-10-13)

The DSi is a second machine of the package: the Machine setting is `nds` or
`dsi`, and the system ids are `NDS` and `DSI`. The controls are the same.

**What it needs**: the DSi's two BIOS files (`bios7i.bin`, `bios9i.bin`,
64 KB each), its firmware (`dsi_firmware.bin`, 128 KB) and a NAND image
(`dsi_nand.bin`, 240 MB), all as the project's firmware. The key the NAND
is locked with is in the second processor's BIOS, so the files have to come
from one console. The DS side of a DSi starts with the free BIOS or the DS's
own, as the BIOS setting says.

**The NAND stays in memory.** A DSi reads and writes its NAND while it runs,
and melonDS does that through an open file. The sandbox keeps no file's
contents or position in a savestate, and a core writes nothing to the user's
files. So the core reads the NAND whole into the machine's memory when the
machine is built, and `waterbox/platform.cpp` reads and writes it there. It
is in the baseline every savestate starts from, so it adds to a state only
the pages the machine changed. The user's file is never written to
(`dsi:nand-untouched`).

**What the core writes into its copy** when the machine is built, as
melonDS's own program does to the file it is pointed at: the Language
setting, and the touch screen's calibration that melonDS works with. It
then reads them back and refuses to start if they did not take. That check
exists because a test showed the gap: with the machine's writes to the NAND
dropped on purpose, no test failed.

**Start With**: `game` starts the cartridge at once, in DSi mode when the
cartridge is a DSi one. `firmware` starts the DSi's own system from the
NAND. The owner's NAND image was never set up, so its system asks for its
settings first.

**The sound processor** of the DSi is run by melonDS's own code for its
known programs (melonDS's default), not by the Teakra interpreter, which is
linked all the same.

What was measured, with the owner's files and Dragon's Lair (USA): the game
starts in 1,200 frames (5.9 seconds natively, 6.6 in the sandbox, most of it
reading the NAND). Native and sandbox give the same 24 digests with presses;
a state saved and loaded every frame (14 seconds for 1,200 frames), a state
moved to a new sandbox, and a rewind change nothing. Started from the DSi's
system, native and sandbox agree as well. In Chimera's engine a DSi project
with the four files as firmware leaves the sandbox runner's Main RAM
(16 MB), and Chimera's window opens it from Chimera's firmware settings.

Counts: the test script 44 passed, 0 failed, 0 skipped with every file and
Chimera's engine (2 minutes 43 seconds); the window test 6 passed; Chimera's
package checks 18 of 18.

**Not done for the DSi**: games sold only in the DSi's shop (DSiWare), which
have to be installed into the NAND; the cameras (they show black); the SD
card; and the language and calibration written into the NAND have only the
read-back check, not a game that shows them.

## Windows, and what a runner will test (2026-10-13)

**Windows.** Chimera's headless runner, built for Windows, played three
movies with presses: the test program (300 frames), New Super Mario Bros.
(1,200 frames) and Dragon's Lair on the DSi with the four DSi files as
firmware (900 frames). Each left the Main RAM the Linux runner leaves, byte
for byte, and the two games' last pictures are the same. Chimera's window
on Windows was not tried.

**A CI runner** has no game and no file of Nintendo's. The workflow builds
from a fresh clone and asks by name for the tests that need neither: the
source and the package's controls, the sixteen "nds:" and "refused:" tests
with the program the tests assemble, that program in Chimera's engine, and
in the window `window:nds` and `window:keys`. It has not run yet.

## The pen follows the picture (2026-10-14)

**What was wrong.** The owner clicked the lower screen in Chimera's window
and the game felt the pen somewhere else. Chimera turns the mouse's place
into a fraction of the whole picture (both screens, as the layout shows
them), and the core read that fraction as one of the lower screen alone.
Stacked, a click in the middle of the lower screen (row 288 of 384) reached
the game as row 144 of the lower screen's 192: the lower half of the screen
could not be reached at all. The 3DS core already reads its pen as a
fraction of the whole picture.

**What changed.** Touch X and Touch Y are a fraction of the whole picture.
The core turns that place back through the Rotation, takes away the lower
screen's corner (which the Screen Layout, Swap Screens and Screen Gap
decide), and hands the machine that pixel. Held over the upper screen, the
gap, or with the lower screen not shown, the pen touches nothing.

**What was measured.** The owner's test cartridge, checknds.nds (it prints
where it feels the pen), reads (128,096) with the pen at the middle of the
lower screen of the stacked picture. The test program reads pixel 192,48 of
the lower screen from the place where each layout shows that pixel:
stacked, side by side with a gap, exchanged and turned a quarter, and
turned a half and three quarters (by hand). Over the upper screen, and with
the upper screen alone, it reads the pen up. The test `nds:pen-layouts`
holds the side-by-side, turned, upper-screen and upper-screen-alone cases;
`nds:pen-and-lid` now points at the lower screen of the stacked picture.
`nds:layouts` leaves the pen out: the same place on another picture is
another pixel of the lower screen, so the machine's memory differs there by
design.

Broken on purpose (the quarter turn not turned back): `nds:pen-layouts`
failed, reading the pen up where the pixel 192,48 should be. Counts after
the change: the test script 45 passed, 0 failed, 0 skipped with every file
and Chimera's engine; the window test 6 passed; Chimera's package checks 18
of 18.

Movies made with the earlier build put the pen elsewhere; none was
published.

## The recompiler, by default (user-decided, 2026-10-19, chimera#316)

The owner decided melonDS's ARM recompiler (its JIT) is built and is the
default. A new setting, CPU (`cpu`), chooses `jit` (the default) or
`interpreter`. The two count the machine's time differently - per block of
code against per instruction - so they are two machines, and a movie needs
the one it was made with. A project made before this setting existed was
made with the interpreter and lacks the setting, so it now takes the
recompiler and may not play back as it did; it plays back with
`interpreter` chosen. The core's own fallback, for the test runners, is the
recompiler too.

What the sandbox needed (patch 0001): no fast memory. melonDS maps one
shared memory file at several places and takes the faults of the program's
own memory accesses; a sandbox has neither, and a machine must not depend on
them. With `CHIMERA_NO_FASTMEM` the recompiler's memory is a plain
allocation, no fault handler is installed, and every access goes through
melonDS's handlers. Its page size, which only the fast-memory check set,
is set to 4 KiB, as code invalidation counts in it. The recompiler's code
buffer is made executable by mprotect, which the sandbox allows. The
x86-64 emitter asks the processor what it has and may use different
instructions on different machines; the results are the same.

Measured: the test program runs native == sandbox, saved and loaded every
frame, moved to a new sandbox and rewound (the nds: and dsi: tests, now on
the recompiler); nds:cpu holds that the interpreter is another run and still
native == sandbox. New Super Mario Bros., its first 1800 frames in the
sandbox: 4.9 s on the recompiler, 7.5 s on the interpreter. The DSi's own
system reaches its menu some 20 frames sooner on the recompiler, which made
dsi:menu sample too coarsely; it reports every 10 frames now.

## Not done, and why

- **The GBA slot** (a GBA cartridge beside the DS game, the rumble and
  motion packs): not asked for; melonDS has it.
- **Wireless play** between consoles: the core is one machine.
- **DSiWare, the DSi's cameras and SD card**: see "The DSi".
- **The 3D renderer's own settings** (melonDS's software renderer has
  none that change a game; its OpenGL renderers are not built).
- **Chimera's window on Windows.**
