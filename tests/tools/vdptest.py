#!/usr/bin/env python3
"""vdptest.py - VDP profiler check with hand-assembled ColecoVision cartridges.

    python tests/tools/vdptest.py rom VARIANT OUT.rom    write a test cartridge
    python tests/tools/vdptest.py check VARIANT PROF.csv check adampc --prof-csv output

Every variant sets Graphics II with the display on and the interrupt off, puts five 16x16
sprites on the same lines (Y = 0x50, then the 0xD0 terminator) and, every frame, polls the
status register until the frame flag is set (the idle loop), then sends VRAM data:
  otir_vblank  64 bytes with OTIR right after the flag, in the vertical border
               -> nothing lost (border slots every 4 VDP cycles)
  loop30       after ~80 lines (inside the next frame's display), 64 bytes from a 30-cycle loop
               (OUT/INC HL/DJNZ) -> nothing lost: the worst case is 28.7 cycles
  loop27       the same place, 256 bytes from the 27-cycle OUT/INC A/JR NZ loop that z88dk's
               mode 2 setup uses -> some writes come before the previous one's slot
  otir         the same place, 256 bytes with OTIR (21 cycles apart) -> writes lost for sure
Always expected: 16 lines with more than 4 sprites, 16 sprite-lines lost, no overrun.
"""
import csv
import sys

VARIANTS = ('otir_vblank', 'loop30', 'loop27', 'otir')


def build_rom(variant):
    c = []
    def vreg(r, v):
        c.extend([0x3E, v, 0xD3, 0xBF, 0x3E, 0x80 | r, 0xD3, 0xBF])
    def vaddr_w(addr):
        c.extend([0x3E, addr & 0xFF, 0xD3, 0xBF, 0x3E, 0x40 | (addr >> 8), 0xD3, 0xBF])
    def data(b):
        c.extend([0x3E, b, 0xD3, 0xBE])
    def here():
        return 0x8024 + len(c)
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
    loop = here()
    c.extend([0xDB, 0xBF, 0x07, 0x30, 0xFB])         # poll: in a,(bf) / rlca / jr nc,poll
    if variant != 'otir_vblank':
        c.extend([0x0E, 7])                          # ld c,7      about 80 lines
        c.extend([0x06, 0, 0x10, 0xFE])              # ld b,0 / djnz $
        c.extend([0x0D, 0x20, 0xF9])                 # dec c / jr nz
    vaddr_w(0x0000)
    if variant in ('otir', 'otir_vblank'):
        n = 0 if variant == 'otir' else 64
        c.extend([0x21, 0x00, 0x80, 0x06, n, 0x0E, 0xBE, 0xED, 0xB3])   # ld hl,8000 / ld b,n / ld c,be / otir
    elif variant == 'loop30':
        c.extend([0x06, 64])                         # ld b,64
        c.extend([0xD3, 0xBE, 0x23, 0x10, 0xFB])     # out (be),a / inc hl / djnz  (11 + 6 + 13)
    else:
        c.extend([0xAF])                             # xor a
        c.extend([0xD3, 0xBE, 0x3C, 0x20, 0xFB])     # out (be),a / inc a / jr nz  (11 + 4 + 12)
    c.extend([0xC3, loop & 0xFF, loop >> 8])         # jp poll
    rom = bytearray([0xFF] * 0x2000)
    rom[0:2] = bytes([0x55, 0xAA])
    rom[0x0A:0x0C] = bytes([0x24, 0x80])
    rom[0x24:0x24 + len(c)] = bytes(c)
    return rom


def check(variant, path):
    rows = list(csv.DictReader(open(path)))[-60:]    # the last second
    lost = sum(int(r['lost']) for r in rows)
    likely = sum(int(r['likely_lost']) for r in rows)
    marginal = sum(int(r['marginal']) for r in rows)
    bad = []
    if variant in ('otir_vblank', 'loop30') and lost + likely + marginal:
        bad.append('expected no early request, got %d lost %d likely %d marginal' % (lost, likely, marginal))
    if variant == 'loop27' and (lost or not likely):
        bad.append('expected likely-lost writes only, got %d lost %d likely' % (lost, likely))
    if variant == 'otir' and not lost:
        bad.append('expected lost writes')
    for r in rows:
        if int(r['lines_over4']) != 16 or int(r['sprites_dropped']) != 16:
            bad.append('frame %s: %s lines over 4, %s lost' % (r['frame'], r['lines_over4'], r['sprites_dropped']))
        if r['overrun'] != '0':
            bad.append('frame %s: overrun' % r['frame'])
    print('%s: last %d frames: %d lost, %d likely lost, %d marginal, busy %.1f%%: %s'
          % (variant, len(rows), lost, likely, marginal, float(rows[-1]['busy_pct']), 'OK' if not bad else 'WRONG'))
    for b in bad[:5]:
        print('  ' + b)
    return 0 if not bad else 1


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in ('rom', 'check') or sys.argv[2] not in VARIANTS:
        print(__doc__); return 2
    if sys.argv[1] == 'rom':
        open(sys.argv[3], 'wb').write(build_rom(sys.argv[2])); return 0
    return check(sys.argv[2], sys.argv[3])


if __name__ == '__main__':
    sys.exit(main())
