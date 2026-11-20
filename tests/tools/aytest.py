#!/usr/bin/env python3
"""aytest.py - Super Game Module AY-3-8910 check, no third-party software involved.

    python tests/tools/aytest.py rom OUT.rom      write the test cartridge
    python tests/tools/aytest.py check OUT.wav    measure the WAV adampc recorded from it

The cartridge (hand-assembled here) programs the AY through ports 0x50/0x51:
  1. tone A, period 254, volume 15, mixer 0x3E   -> 1789772 / (16 * 254) = 440.4 Hz
  2. after ~2 s: tone off, channel A on the envelope, shape 0x0E (triangle, rising first),
     EP = 256                                      -> one ramp = 256 * 256 / 1789772 s,
                                                      triangle = 2 ramps = 13.65 Hz
The check counts rising zero crossings over a window of each part.
"""
import array
import sys

AY_CLOCK = 3579545 / 2


def build_rom():
    code = []
    def ay(reg, val):
        code.extend([0x3E, reg, 0xD3, 0x50, 0x3E, val, 0xD3, 0x51])   # ld a,r / out (50),a / ld a,v / out (51),a
    code.append(0xF3)                       # di
    ay(0, 254); ay(1, 0); ay(7, 0x3E); ay(8, 0x0F)
    # wait ~2 s: three nested loops (B, C, D)
    code.extend([0x16, 8])                  # ld d,8 (about 1.9 s)
    wait = len(code)
    code.extend([0x0E, 0])                  # ld c,0
    code.extend([0x06, 0])                  # ld b,0
    code.extend([0x10, 0xFE])               # djnz $
    code.extend([0x0D, 0x20, 0xF9])         # dec c / jr nz,(ld b,0)
    code.extend([0x15, 0x20, 0xF4])         # dec d / jr nz,(ld c,0)
    ay(7, 0x3F); ay(11, 0); ay(12, 1); ay(8, 0x10); ay(13, 0x0E)
    code.extend([0x18, 0xFE])               # jr $
    rom = bytearray([0xFF] * 0x2000)
    rom[0:2] = bytes([0x55, 0xAA])          # 55AA: start without the title screen
    start = 0x8024
    rom[0x0A:0x0C] = bytes([start & 0xFF, start >> 8])
    rom[0x24:0x24 + len(code)] = bytes(code)
    return rom, wait


def crossings(s, a, b):
    n, prev = 0, s[a]
    for v in s[a + 1:b]:
        if prev < 0 <= v:
            n += 1
        prev = v
    return n * 44100.0 / (b - a)


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ('rom', 'check'):
        print(__doc__); return 2
    if sys.argv[1] == 'rom':
        open(sys.argv[2], 'wb').write(build_rom()[0]); return 0
    s = array.array('h'); s.frombytes(open(sys.argv[2], 'rb').read()[44:])
    tone = crossings(s, int(0.5 * 44100), int(1.5 * 44100))
    env = crossings(s, len(s) - 4 * 44100, len(s))
    want_tone, want_env = AY_CLOCK / (16 * 254), AY_CLOCK / (256 * 256) / 2
    ok = abs(tone - want_tone) < 2 and abs(env - want_env) < 0.6
    print('tone %.1f Hz (want %.1f), envelope triangle %.2f Hz (want %.2f): %s'
          % (tone, want_tone, env, want_env, 'OK' if ok else 'WRONG'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
