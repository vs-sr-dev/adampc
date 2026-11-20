#!/usr/bin/env python3
"""vdptest.py - VDP profiler check with a hand-assembled ColecoVision cartridge.

    python tests/tools/vdptest.py rom OUT.rom      write the test cartridge
    python tests/tools/vdptest.py check PROF.csv   check adampc --prof-csv output

The cartridge sets Graphics II with the display on and the interrupt off, puts five 16x16
sprites on the same lines (Y = 0x50, then the 0xD0 terminator), and every frame:
  - polls the status register until the frame flag is set (the idle loop),
  - waits about 80 lines, which is inside the active display of the next frame,
  - sends 256 bytes with OTIR: 21 cycles = 5.87 us apart, under the 8 us worst case of
    Graphics II during the active display (data manual Table 2-2).
Expected per frame: 255 risky transfers (the first one follows a 2 us setup), 16 lines with
more than 4 sprites, 16 sprite-lines lost, no overrun, and a busy time well under a frame.
"""
import csv
import sys


def build_rom():
    c = []
    def vreg(r, v):
        c.extend([0x3E, v, 0xD3, 0xBF, 0x3E, 0x80 | r, 0xD3, 0xBF])
    def vaddr_w(addr):
        c.extend([0x3E, addr & 0xFF, 0xD3, 0xBF, 0x3E, 0x40 | (addr >> 8), 0xD3, 0xBF])
    def data(b):
        c.extend([0x3E, b, 0xD3, 0xBE])
    c.append(0xF3)                                   # di
    vreg(0, 0x02); vreg(1, 0x82)                     # Graphics II, display off for the setup
    vreg(2, 0x06); vreg(3, 0xFF); vreg(4, 0x03); vreg(5, 0x36); vreg(6, 0x07); vreg(7, 0x04)
    vaddr_w(0x3800)                                  # sprite pattern 0: a solid 16x16 block
    for _ in range(32):
        data(0xFF)
    vaddr_w(0x1B00)                                  # sprite attributes
    for i in range(5):
        for b in (0x50, i * 24, 0, 0x0F):
            data(b)
    data(0xD0)
    vreg(1, 0xC2)                                    # display on, 16x16 sprites, no interrupt
    loop = 0x8024 + len(c)
    c.extend([0xDB, 0xBF, 0x07, 0x30, 0xFB])         # poll: in a,(bf) / rlca / jr nc,poll
    c.extend([0x0E, 7])                              # ld c,7     \  about 80 lines:
    c.extend([0x06, 0, 0x10, 0xFE])                  # ld b,0 / djnz $ (3328 cycles)
    c.extend([0x0D, 0x20, 0xF9])                     # dec c / jr nz
    vaddr_w(0x0000)
    c.extend([0x21, 0x00, 0x80, 0x06, 0x00, 0x0E, 0xBE, 0xED, 0xB3])   # ld hl,8000 / ld b,0 / ld c,be / otir
    c.extend([0xC3, loop & 0xFF, loop >> 8])         # jp poll
    rom = bytearray([0xFF] * 0x2000)
    rom[0:2] = bytes([0x55, 0xAA])
    rom[0x0A:0x0C] = bytes([0x24, 0x80])
    rom[0x24:0x24 + len(c)] = bytes(c)
    return rom


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ('rom', 'check'):
        print(__doc__); return 2
    if sys.argv[1] == 'rom':
        open(sys.argv[2], 'wb').write(build_rom()); return 0
    rows = list(csv.DictReader(open(sys.argv[2])))[-60:]     # the last second
    bad = []
    for r in rows:
        if int(r['risky']) != 255: bad.append('frame %s: %s risky' % (r['frame'], r['risky']))
        if int(r['lines_over4']) != 16 or int(r['sprites_dropped']) != 16:
            bad.append('frame %s: %s lines over 4, %s lost' % (r['frame'], r['lines_over4'], r['sprites_dropped']))
        if r['overrun'] != '0' or not 20 < float(r['busy_pct']) < 60:
            bad.append('frame %s: busy %s%% overrun %s' % (r['frame'], r['busy_pct'], r['overrun']))
    gap = min(float(r['min_gap_us']) for r in rows)
    print('last %d frames: busy %.1f%%, min gap %.2f us: %s' % (len(rows), float(rows[-1]['busy_pct']), gap,
                                                              'OK' if not bad else 'WRONG'))
    for b in bad[:5]:
        print('  ' + b)
    return 0 if not bad else 1


if __name__ == '__main__':
    sys.exit(main())
