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
| 3 | The package: controls, settings, screens | Chimera's package checks; a project in Chimera's engine and window | |
| 4 | Touch screen, microphone, lid; save data; the clock | Tests for each | |
| 5 | The DSi | A DSi game boots with the owner's BIOS files | |
| 6 | Tests on a self-made program, Windows, documents, CI | The test script on a runner | |
| 7 | Publishing | The owner's word | |

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
- The sound is asked for at 48 kHz. The DS mixes at about 32.7 kHz and
  melonDS converts; whether to hand out the DS's own rate is open.
