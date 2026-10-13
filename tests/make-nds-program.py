#!/usr/bin/env python3
"""Writes a small Nintendo DS program for the tests, as a .nds file.

    tests/make-nds-program.py <out.nds> [nopad]

No game may be put in this repository or on a CI runner, and the DS needs no
BIOS of Nintendo's here, so the tests bring their own program. It is
assembled by this script from the instructions below; nothing of it comes
from anybody's game or development kit.

What the program does:

  main processor (ARM9), for ever:
    - copies the buttons (KEYINPUT) to main memory at 0x02100000, unless the
      program was made with "nopad", which never reads them;
    - counts its turns at 0x02100004;
    - paints the upper screen with the buttons' value as a colour, and the
      lower screen with a colour that moves on as the count grows.
  second processor (ARM7): starts a steady tone on a sound channel, then for
    ever:
    - copies the second button register (X, Y, the pen, the lid) to
      0x02100008;
    - asks the touch screen's chip where the pen is, and writes the two
      answers to 0x0210000C (across) and 0x02100010 (down): sixteen times
      the pixel;
    - asks the same chip for the microphone's level, writes it to
      0x02100014, and gathers at 0x02100018 every bit in which a level
      differed from silence (0 for as long as the microphone hears nothing).

So a test can see: the processors run (the count), a button reaches the
program (the copy), the program's reads are noticed (lag frames), both
screens are drawn, there is sound, the pen's place arrives, and the
microphone's noise arrives.
"""
import struct
import sys


class Program:
    """A few ARM instructions, enough for the loops above, with literals
    placed after the code."""

    def __init__(self, base):
        self.base = base
        self.words = []
        self.fixups = []     # (index, value) for "load a 32-bit value"
        self.branches = []   # (index, label): filled in when every label is known
        self.labels = {}

    def here(self):
        return self.base + 4 * len(self.words)

    def label(self, name):
        self.labels[name] = self.here()

    def load(self, rd, value):            # ldr rd, =value
        self.fixups.append((len(self.words), value))
        self.words.append(0xE59F0000 | rd << 12)

    def ldr(self, rd, rn, off=0):
        self.words.append(0xE5900000 | rn << 16 | rd << 12 | off)

    def str_(self, rd, rn, off=0):
        self.words.append(0xE5800000 | rn << 16 | rd << 12 | off)

    def ldrh(self, rd, rn, off=0):
        self.words.append(0xE1D000B0 | rn << 16 | rd << 12 | (off >> 4) << 8 | (off & 15))

    def strh(self, rd, rn, off=0):
        self.words.append(0xE1C000B0 | rn << 16 | rd << 12 | (off >> 4) << 8 | (off & 15))

    def add_imm(self, rd, rn, imm):
        self.words.append(0xE2800000 | rn << 16 | rd << 12 | imm)

    def mov_lsr(self, rd, rm, n):
        self.words.append(0xE1A00020 | rd << 12 | n << 7 | rm)

    def mov_lsl(self, rd, rm, n):
        self.words.append(0xE1A00000 | rd << 12 | n << 7 | rm)

    def mov_imm(self, rd, imm):
        self.words.append(0xE3A00000 | rd << 12 | imm)

    def orr_lsl(self, rd, rn, rm, n):     # rd = rn | (rm << n)
        self.words.append(0xE1800000 | rn << 16 | rd << 12 | n << 7 | rm)

    def orr(self, rd, rn, rm):
        self.words.append(0xE1800000 | rn << 16 | rd << 12 | rm)

    def eor_imm(self, rd, rn, imm, ror=0):    # imm rotated right by 2*ror
        self.words.append(0xE2200000 | rn << 16 | rd << 12 | ror << 8 | imm)

    def tst_imm(self, rn, imm):
        self.words.append(0xE3100000 | rn << 16 | imm)

    def mov(self, rd, rm):
        self.words.append(0xE1A00000 | rd << 12 | rm)

    def bx_lr(self):
        self.words.append(0xE12FFF1E)

    def _branch(self, opcode, name):
        self.branches.append((len(self.words), name))
        self.words.append(opcode)

    def b(self, name):
        self._branch(0xEA000000, name)

    def bne(self, name):
        self._branch(0x1A000000, name)

    def bl(self, name):
        self._branch(0xEB000000, name)

    def binary(self):
        words = list(self.words)
        for index, name in self.branches:
            words[index] |= ((self.labels[name] - (self.base + 4 * index + 8)) >> 2) & 0xFFFFFF
        for index, value in self.fixups:
            literal = self.base + 4 * len(words)
            pc = self.base + 4 * index + 8
            words[index] |= literal - pc
            words.append(value)
        return b"".join(struct.pack("<I", w) for w in words)


def arm9(read_pad):
    p = Program(0x02000000)
    p.load(0, 0x04000000)       # the main processor's registers
    p.load(1, 0x02100000)       # where the program keeps its three numbers
    p.mov_imm(2, 0)             # the count
    p.load(3, 0x00008203)       # the screens and both picture engines on, the first engine on the upper screen
    p.str_(3, 0, 0x304)
    p.load(3, 0x00010000)       # each engine: show its picture, no layers: the backdrop colour
    p.str_(3, 0, 0)
    p.load(4, 0x04001000)
    p.str_(3, 4, 0)
    p.load(5, 0x05000000)       # the first engine's backdrop colour
    p.load(9, 0x05000400)       # the second engine's
    p.load(7, 0x04000130)       # the buttons
    p.load(6, 0x000003FF)       # what the buttons read with none held
    p.label("loop")
    if read_pad:
        p.ldrh(6, 7, 0)
    p.str_(6, 1, 0)
    p.add_imm(2, 2, 1)
    p.str_(2, 1, 4)
    p.strh(6, 5, 0)
    p.mov_lsr(8, 2, 12)
    p.strh(8, 9, 0)
    p.b("loop")
    return p.binary()


def arm7():
    p = Program(0x02380000)
    p.load(1, 0x02100000)
    p.load(2, 0x04000500)       # the sound's master control: on, full volume
    p.load(3, 0x0000807F)
    p.strh(3, 2, 0)
    p.load(4, 0x04000480)       # channel 8, a square wave
    p.load(3, 0x0000F000)       # its pitch
    p.strh(3, 4, 8)
    p.load(3, 0xE340007F)       # start, square wave, half duty, centre, full volume
    p.str_(3, 4, 0)
    p.load(6, 0x04000136)       # X, Y, the pen and the lid
    p.load(10, 0x040001C0)      # the serial bus the touch screen's chip is on
    p.mov_imm(11, 0)            # the microphone's gathered bits
    p.label("loop")
    p.ldrh(7, 6, 0)
    p.str_(7, 1, 8)
    p.mov_imm(0, 0xD0)          # the pen, across
    p.bl("ask")
    p.str_(0, 1, 0xC)
    p.mov_imm(0, 0x90)          # the pen, down
    p.bl("ask")
    p.str_(0, 1, 0x10)
    p.mov_imm(0, 0xE0)          # the microphone
    p.bl("ask")
    p.str_(0, 1, 0x14)
    p.eor_imm(0, 0, 0x02, 11)   # silence reads 0x800 (2 rotated right by 22 bits)
    p.orr(11, 11, 0)
    p.str_(11, 1, 0x18)
    p.b("loop")

    # ask: r0 = the question for the chip; answers r0 = its 12-bit reply
    p.label("ask")
    p.load(8, 0x00008A00)       # the bus on, the chip chosen and kept chosen
    p.strh(8, 10, 0)
    p.strh(0, 10, 2)            # the question
    p.label("wait1")
    p.ldrh(9, 10, 0)
    p.tst_imm(9, 0x80)
    p.bne("wait1")
    p.mov_imm(0, 0)
    p.strh(0, 10, 2)            # the reply's high part comes back on the next byte
    p.label("wait2")
    p.ldrh(9, 10, 0)
    p.tst_imm(9, 0x80)
    p.bne("wait2")
    p.ldrh(12, 10, 2)
    p.load(8, 0x00008200)       # let go of the chip after the last byte
    p.strh(8, 10, 0)
    p.strh(0, 10, 2)
    p.label("wait3")
    p.ldrh(9, 10, 0)
    p.tst_imm(9, 0x80)
    p.bne("wait3")
    p.ldrh(9, 10, 2)
    p.mov_lsr(9, 9, 3)
    p.orr_lsl(0, 9, 12, 5)      # high part * 32 + low part / 8
    p.mov_lsl(0, 0, 20)         # twelve bits of it
    p.mov_lsr(0, 0, 20)
    p.bx_lr()
    return p.binary()


def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    read_pad = not (len(sys.argv) > 2 and sys.argv[2] == "nopad")
    code9, code7 = arm9(read_pad), arm7()
    off9 = 0x200
    off7 = (off9 + len(code9) + 0x1FF) & ~0x1FF
    used = off7 + len(code7)
    header = bytearray(0x200)
    header[0x000:0x00C] = b"CHIMERATEST\0"
    header[0x00C:0x010] = b"####"                 # the code of a home-made program
    header[0x010:0x012] = b"00"
    struct.pack_into("<IIII", header, 0x020, off9, 0x02000000, 0x02000000, len(code9))
    struct.pack_into("<IIII", header, 0x030, off7, 0x02380000, 0x02380000, len(code7))
    struct.pack_into("<II", header, 0x080, used, 0x200)
    struct.pack_into("<H", header, 0x15E, crc16(header[:0x15E]))
    rom = bytearray(header) + bytearray(0x20000 - 0x200)   # 128 KB, the smallest cartridge
    rom[off9:off9 + len(code9)] = code9
    rom[off7:off7 + len(code7)] = code7
    with open(sys.argv[1], "wb") as f:
        f.write(rom)
    print("%s: %d bytes of code for the main processor, %d for the second" % (sys.argv[1], len(code9), len(code7)))


if __name__ == "__main__":
    main()
