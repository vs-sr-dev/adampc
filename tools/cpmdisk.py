#!/usr/bin/env python3
"""cpmdisk.py - files on an ADAM CP/M 2.2 disk (160K .dsk).

    python tools/cpmdisk.py dir IMAGE
    python tools/cpmdisk.py get IMAGE NAME.EXT [HOSTFILE]       (default: NAME.EXT here)
    python tools/cpmdisk.py put IMAGE HOSTFILE [NAME.EXT] [--user N]
    python tools/cpmdisk.py era IMAGE NAME.EXT
    python tools/cpmdisk.py new IMAGE                            (empty CP/M data disk)

Disk parameter block of the ADAM CP/M 2.2 BIOS for the 160K disk drive:
  SPT 8 (128-byte records per "track": one track is one 1K ADAM block)
  BSH 3, BLM 7 (1K allocation blocks), EXM 0 (16K per directory entry)
  DSM 146 (147 blocks), DRM 63 (64 directory entries in blocks 0-1, AL0 = C0)
  OFF 13 (ADAM blocks 0-12 hold the system: CP/M block n is ADAM block 13 + n)
ADAM block b is two 512-byte sectors on track b/4, picked by the disk controller ROM's
interleave (1,6,3,8,5,2,7,4), as in eosput.py. Records inside a block are in order.
"""
import sys

SKEW = [1, 6, 3, 8, 5, 2, 7, 4]
OFF, DSM, DRM, DIR_BLOCKS = 13, 146, 63, 2
SIZE = 160 * 1024


def sectors(b):
    t = b // 4
    return [(t * 8 + SKEW[(b % 4) * 2 + k] - 1) * 512 for k in range(2)]


def get_block(img, n):                       # CP/M block n
    return b''.join(bytes(img[o:o + 512]) for o in sectors(OFF + n))


def put_block(img, n, data):
    data = bytes(data).ljust(1024, b'\x1a')
    for k, o in enumerate(sectors(OFF + n)):
        img[o:o + 512] = data[k * 512:(k + 1) * 512]


def read_dir(img):
    d = b''.join(get_block(img, n) for n in range(DIR_BLOCKS))
    return [bytearray(d[i * 32:(i + 1) * 32]) for i in range(DRM + 1)]


def write_dir(img, ents):
    d = b''.join(bytes(e) for e in ents)
    for n in range(DIR_BLOCKS):
        put_block(img, n, d[n * 1024:(n + 1) * 1024])


def fcb_name(name):
    base, _, ext = name.upper().partition('.')
    if not base or len(base) > 8 or len(ext) > 3:
        sys.exit('not a CP/M file name: %s' % name)
    return (base.ljust(8) + ext.ljust(3)).encode('ascii')


def entry_name(e):
    raw = bytes(x & 0x7F for x in e[1:12]).decode('latin1')
    return raw[:8].rstrip() + '.' + raw[8:].rstrip()


def files(ents):
    """{(user, name): [entries sorted by extent]}"""
    out = {}
    for e in ents:
        if e[0] == 0xE5 or e[0] > 15:
            continue
        out.setdefault((e[0], entry_name(e)), []).append(e)
    for k in out:
        out[k].sort(key=lambda e: e[12] + 32 * e[14])
    return out


def file_data(img, exts):
    data = b''
    for e in exts:
        recs = e[15]
        blocks = [b for b in e[16:32] if b]
        chunk = b''.join(get_block(img, b) for b in blocks)
        data += chunk[:recs * 128]
    return data


def load(path):
    img = bytearray(open(path, 'rb').read())
    if len(img) < SIZE:
        sys.exit('%s: not a 160K disk image' % path)
    return img


def cmd_dir(img):
    ents = read_dir(img)
    fs = files(ents)
    used = {b for e in ents if e[0] != 0xE5 for b in e[16:32] if b}
    for (u, name), exts in sorted(fs.items()):
        recs = sum(e[15] for e in exts)
        print('%2d  %-12s %6d bytes  %2d entries' % (u, name, recs * 128, len(exts)))
    print('%d files, %dK free of %dK, %d directory entries free'
          % (len(fs), DSM + 1 - DIR_BLOCKS - len(used), DSM + 1 - DIR_BLOCKS,
             sum(1 for e in ents if e[0] == 0xE5)))


def find(img, name):
    fs = files(read_dir(img))
    want = entry_name(b'\0' + fcb_name(name))
    hits = [v for (u, n), v in fs.items() if n == want]
    if not hits:
        sys.exit('%s: not on the disk' % name)
    return hits[0]


def cmd_put(img, host, name, user):
    data = open(host, 'rb').read()
    if len(data) % 128:
        data += b'\x1a' * (128 - len(data) % 128)     # CP/M text files end with ^Z
    ents = read_dir(img)
    fcb = fcb_name(name)
    for e in ents:                                     # replace a file of the same name
        if e[0] == user and bytes(x & 0x7F for x in e[1:12]) == fcb:
            e[0] = 0xE5
    used = {b for e in ents if e[0] != 0xE5 for b in e[16:32] if b} | set(range(DIR_BLOCKS))
    free = [b for b in range(DSM + 1) if b not in used]
    nblocks = (len(data) + 1023) // 1024
    nexts = max(1, (nblocks + 15) // 16)
    slots = [i for i, e in enumerate(ents) if e[0] == 0xE5]
    if nblocks > len(free) or nexts > len(slots):
        sys.exit('disk full')
    recs_left = len(data) // 128
    for x in range(nexts):
        blocks = free[x * 16:min((x + 1) * 16, nblocks)]
        recs = min(recs_left, 128)
        e = bytearray(32)
        e[0] = user; e[1:12] = fcb; e[12] = x & 31; e[14] = x >> 5; e[15] = recs
        e[16:16 + len(blocks)] = bytes(blocks)
        ents[slots[x]] = e
        recs_left -= recs
        for k, b in enumerate(blocks):
            put_block(img, b, data[(x * 16 + k) * 1024:(x * 16 + k + 1) * 1024])
    write_dir(img, ents)


def main(argv):
    if len(argv) < 2:
        print(__doc__); return 2
    cmd, path = argv[0], argv[1]
    if cmd == 'new':
        img = bytearray(b'\xe5' * SIZE)
        open(path, 'wb').write(img); return 0
    img = load(path)
    if cmd == 'dir':
        cmd_dir(img)
    elif cmd == 'get' and len(argv) >= 3:
        data = file_data(img, find(img, argv[2]))
        open(argv[3] if len(argv) > 3 else argv[2], 'wb').write(data)
        print('%s: %d bytes' % (argv[2], len(data)))
    elif cmd == 'put' and len(argv) >= 3:
        user = 0
        if '--user' in argv:
            i = argv.index('--user'); user = int(argv[i + 1]); argv = argv[:i] + argv[i + 2:]
        host = argv[2]
        name = argv[3] if len(argv) > 3 else host.replace('\\', '/').split('/')[-1]
        cmd_put(img, host, name, user)
        open(path, 'wb').write(img)
        print('%s -> %s (user %d)' % (host, name.upper(), user))
    elif cmd == 'era' and len(argv) >= 3:
        for e in find(img, argv[2]):
            e[0] = 0xE5
        ents = read_dir(img)
        fcb = fcb_name(argv[2])
        for e in ents:
            if bytes(x & 0x7F for x in e[1:12]) == fcb:
                e[0] = 0xE5
        write_dir(img, ents)
        open(path, 'wb').write(img)
    else:
        print(__doc__); return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
