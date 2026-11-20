#!/usr/bin/env python3
"""vdpslots.py - read the TMS9918 VRAM access slots off the openMSX timing diagram.

    python tools/vdpslots.py vdp-timing-v2.png > src/vdpslots.h

The diagram (https://openmsx.org/vdp-vram-timing/vdp-timing-v2.png, from the article
https://openmsx.org/vdp-vram-timing/vdp-timing-2.html, Wouter Vermaelen 2014) draws one
display line of every VDP mode as a row of boxes, one box per memory access (2 VDP cycles).
Its TMS9918 rows were pieced together from TI's hand-drawn 1978 "9918 Master Timing" chart
and the TMS9918 application manual. The bottom three rows are the TMS9918 ones: "Refresh"
(vertical border, and display off), "Character" (Graphics I/II; Multicolor per the article
differs only in using no colour table) and "Text". Box j (j = 1..171) starts at VDP cycle
2j - 1; in the character modes the 256 visible pixels are cycles 63-318.

Needs Pillow. Writes a C header with one character per box:
  C = CPU access slot, r = refresh, s = sprite, n = name, p = pattern, c = colour.
"""
import sys
from PIL import Image

X0, X_END, BOXES = 430.0, 9991.0, 172          # first separator (cycle 341 = -1), last, boxes drawn
ROWS = (('border', 735), ('character', 784), ('text', 832))


def kind(c):
    r, g, b = c
    if abs(r - 173) < 25 and abs(g - 216) < 25 and abs(b - 230) < 25: return 'C'
    if r > 220 and g < 140 and b < 140: return 'r'
    if r > 220 and g > 220 and b < 160: return 's'
    if r < 90 and g < 90 and b > 180: return 'p'
    if r < 160 and g > 220 and b < 160: return 'n'
    if r > 160 and g < 120 and b > 160: return 'c'
    return '?'


def main():
    im = Image.open(sys.argv[1]).convert('RGB')
    px = im.load()
    w = (X_END - X0) / BOXES
    print('/* vdpslots.h - TMS9918 VRAM access slots, one character per memory access (2 VDP cycles):')
    print(' * entry j (0..170) starts at VDP cycle 2j + 1. C = CPU slot, r = refresh, s = sprite,')
    print(' * n = name, p = pattern, c = colour. Read by tools/vdpslots.py off the openMSX timing')
    print(' * diagram (vdp-timing-v2.png, W. Vermaelen 2014), whose TMS9918 rows come from TI\'s 1978')
    print(' * "9918 Master Timing" chart and the TMS9918 application manual. */')
    print('#ifndef VDPSLOTS_H\n#define VDPSLOTS_H')
    for name, y in ROWS:
        s = ''.join(kind(px[int(X0 + j * w + 7), y]) for j in range(1, 172))
        if '?' in s or len(s) != 171:
            sys.exit('%s: unrecognised boxes: %s' % (name, s))
        print('static const char vdp_slots_%s[172] =' % name)
        for i in range(0, 171, 57):
            print('    "%s"%s' % (s[i:i + 57], ';' if i + 57 >= 171 else ''))
    print('#endif')


if __name__ == '__main__':
    main()
