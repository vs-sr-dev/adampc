#!/usr/bin/env python3
"""Check findmk.py output against the block mapping each disk controller ROM implements
(docs/RECON.md, "ROM alternative del controller").

usage: expect_map.py LAYOUT < findmk-output     LAYOUT: std | 320 | 720 | 1440
Prints FAIL lines for mismatches, OK n otherwise; exit status 1 on any mismatch.
"""
import re
import sys

SKEW8 = [1, 6, 3, 8, 5, 2, 7, 4]
T9 = [1, 5, 9, 4, 8, 3, 7, 2, 6]
T18 = [1, 5, 9, 13, 17, 3, 7, 11, 15, 2, 6, 10, 14, 18, 4, 8, 12, 16]


def expect(layout, b):
    if layout == 'std':
        return b // 4, 0, SKEW8[(b % 4) * 2]
    if layout == '320':
        idx = b // 4
        return (idx - 40, 1, SKEW8[(b % 4) * 2]) if idx >= 40 else (idx, 0, SKEW8[(b % 4) * 2])
    s = 2 * b
    spt, table = (9, T9) if layout == '720' else (18, T18)
    return s // (2 * spt), (s // spt) & 1, table[s % spt]


def main():
    layout = sys.argv[1]
    n = bad = 0
    for line in sys.stdin:
        m = re.match(r'block (\d+) -> cyl (\d+) side (\d+) sector ID (\d+)', line)
        if not m:
            continue
        b, got = int(m.group(1)), tuple(int(x) for x in m.groups()[1:])
        n += 1
        if got != expect(layout, b):
            bad += 1
            print(f'FAIL block {b}: at {got}, expected {expect(layout, b)}')
    if n == 0:
        print('FAIL no markers found')
        sys.exit(1)
    print(f'OK {n} blocks')
    sys.exit(1 if bad else 0)


main()
