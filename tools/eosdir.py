#!/usr/bin/env python3
"""List the EOS directory of an ADAM disk (.dsk, 160K) or data pack (.ddp) image.

usage: eosdir.py IMAGE [--dump BLOCK]
Disk blocks: block b = track b//4, logical sectors (b%4)*2+1 and +2, mapped to sector IDs
through the interleave table of the disk controller ROM (1,6,3,8,5,2,7,4).
"""
import sys

SKEW = [1, 6, 3, 8, 5, 2, 7, 4]


def block(img, b, disk):
    if not disk:
        return img[b * 1024:(b + 1) * 1024]
    t = b // 4
    out = b''
    for k in range(2):
        sid = SKEW[(b % 4) * 2 + k]
        off = (t * 8 + sid - 1) * 512
        out += img[off:off + 512]
    return out


def main():
    path = sys.argv[1]
    img = open(path, 'rb').read()
    disk = not path.lower().endswith(('.ddp', '.img')) and len(img) != 262144
    if '--dump' in sys.argv:
        b = int(sys.argv[sys.argv.index('--dump') + 1])
        d = block(img, b, disk)
        for i in range(0, 1024, 16):
            row = d[i:i + 16]
            print(f'{i:03X}: ' + ' '.join(f'{x:02X}' for x in row) + '  ' + ''.join(chr(x) if 32 <= x < 127 else '.' for x in row))
        return
    d = block(img, 1, disk)
    name = d[0:12]
    print('volume:', name.split(b'\x03')[0].decode('latin1'), ' dirsize', d[12] & 0x7F, ' check', d[13:17].hex())
    nblocks = d[12] & 0x7F
    for bi in range(nblocks):
        d = block(img, 1 + bi, disk)
        for e in range(39):
            r = d[e * 26:(e + 1) * 26]
            if bi == 0 and e == 0:
                continue
            if r[0] == 0:
                continue
            nm = r[0:12]
            n = nm.split(b'\x03')[0]
            typ = chr(n[-1]) if n else '?'
            attr = r[12]
            start = int.from_bytes(r[13:17], 'little')
            alloc = int.from_bytes(r[17:19], 'little')
            used = int.from_bytes(r[19:21], 'little')
            last = int.from_bytes(r[21:23], 'little')
            print(f'{n[:-1].decode("latin1"):12s} type {typ}  attr {attr:02X}  start {start:4d}  alloc {alloc:3d}  used {used:3d}  last {last:4d}')


main()
