# chimera-core-melonds

A core for [Chimera](https://github.com/ToolAssisted-run/chimera), the
frontend for tool-assisted speedruns, for the **Nintendo DS** and the
**Nintendo DSi**. It is built on [melonDS](https://melonds.kuribo64.net/)
1.1 and produces one file, `melonds.chimeraCore`, that goes in Chimera's
`cores` folder.

## Status

New, and not published yet.

Both machines have run games, and for each the sandboxed core gives the same
results as a plain native build: picture, sound, buttons, the pen, lag
frames and savestates.

- **Nintendo DS**: New Super Mario Bros. (USA) and Kirby: Canvas Curse (USA),
  with the free BIOS and with the console's own. Also save data, the
  console's menu, and the clock.
- **Nintendo DSi**: Dragon's Lair (USA), with a DSi's BIOS files, firmware
  and NAND image. Also the DSi's own system from the NAND.
- On Windows, Chimera's headless runner plays a movie of a game of each
  machine and ends on the same memory and the same picture as on Linux.
  Chimera's window was tried on Linux only.
- Not there: games sold only in the DSi's shop (DSiWare), the DSi's cameras
  and SD card, the GBA slot, and wireless play.

## What you bring

- **Nintendo DS**: a game as a `.nds` file, and nothing else. melonDS
  carries a free replacement BIOS and most games run with it. For the games
  that do not, or to start from the console's menu, set BIOS to `original`
  and supply the console's `bios7.bin`, `bios9.bin` and `firmware.bin`.
- **Nintendo DSi**: a game as a `.nds` file, and from one DSi its two BIOS
  files (`bios7i.bin`, `bios9i.bin`), its firmware and its NAND image. The
  core works on a copy of the NAND in memory and never changes your file.

## Buttons and settings

- **Buttons**: Up, Down, Left, Right, Start, Select, B, A, Y, X, L and R;
  Touch, the pen held on the lower screen; and Lid Closed, the lid shut for
  as long as it is held.
- **The pen's place** is Touch X and Touch Y, each a fraction of the whole
  picture from 0 to 65535 (0 is the left or the top edge): clicking the
  lower screen in Chimera's window puts the pen where the mouse is, whatever
  the screen layout. Held anywhere else, the pen touches nothing.
  **Mic Volume**, 0 to 100, is how loud a noise the
  microphone hears, for the games that are blown or shouted at.
- **The two screens are one picture.** Screen Layout puts them one above the
  other (the default), side by side, or shows one alone; Swap Screens,
  Screen Gap and Rotation do what they say. They change the picture only.
- **Machine** (DS or DSi), **BIOS**, **Start With** (the game, or the
  console's menu), **Language** and **Starting Clock** are part of the
  machine: a movie made with one value does not play back with another.

RAM Watch and RAM Search read Main RAM (4 MB, 16 MB on a DSi), Shared WRAM
and ARM7 WRAM.

Save data stays inside the machine, where a savestate holds it. Emulator >
Export Save Data... writes the cartridge's save memory to `nds.sav`, and a
project's Save data slot takes a `.sav` file to start from.

The sound is the DS's own mix at the rate the console makes it, 32,728
stereo pairs a second.

## Building and testing

[docs/BUILDING.md](docs/BUILDING.md) has every step. [docs/PLAN.md](docs/PLAN.md)
is the design log: what was decided, why, and what was measured.

## Licence

This repository's own code is MIT. melonDS is under the GNU General Public
License version 3 or later, and so is the program in the built package. The
package carries the licence texts of everything in it. It contains no game
and no BIOS or firmware of Nintendo's. See [LICENSE](LICENSE).
