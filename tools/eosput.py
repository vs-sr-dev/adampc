#!/usr/bin/env python3
"""Copy a host file into an EOS volume (ADAM disk .dsk 160K, or data pack .ddp/.img).

usage: eosput.py IMAGE HOSTFILE NAME [--type A] [--attr 10] [--basic]

--basic: HOSTFILE is a SmartBASIC listing in plain text. SmartBASIC 1.2 keeps its programs as
text: a file saved with SAVE is the program's lines exactly as typed (line number, text, CR),
no tokens. So a listing only needs CR line ends (CR LF or LF are converted), no blank lines and
no trailing ^Z; the lines are checked for a leading line number. LOAD NAME then reads it.

The file goes at the start of the free area (the BLOCKS LEFT entry), which shrinks; the
new directory entry takes the BLOCKS LEFT slot and BLOCKS LEFT moves down one entry.
Directory entry (26 bytes): name + type letter + 0x03 (12), attribute, start block (4),
blocks allocated (2), blocks used (2), bytes in the last block (2), date (3).
Typical types: A = SmartBASIC program or data file, H = SmartWriter document.
"""
import sys

SKEW = [1, 6, 3, 8, 5, 2, 7, 4]


def is_disk(path, img):
    return not path.lower().endswith(('.ddp', '.img')) and len(img) != 262144


def sector_offsets(b, disk):
    if not disk:
        return [(b * 1024, 1024)]
    t = b // 4
    return [((t * 8 + SKEW[(b % 4) * 2 + k] - 1) * 512, 512) for k in range(2)]


def get_block(img, b, disk):
    return b''.join(bytes(img[o:o + n]) for o, n in sector_offsets(b, disk))


def put_block(img, b, data, disk):
    data = data.ljust(1024, b'\0')
    pos = 0
    for o, n in sector_offsets(b, disk):
        if o + n > len(img):
            sys.exit(f'block {b} is past the end of the image')
        img[o:o + n] = data[pos:pos + n]
        pos += n


def basic_text(data, host):
    """A plain-text listing -> SmartBASIC's program file: lines ending in CR."""
    text = data.split(b'\x1a')[0].replace(b'\r\n', b'\n').replace(b'\r', b'\n')
    lines = text.split(b'\n')          # trailing spaces stay: they can be inside a string
    lines = [ln for ln in lines if ln.strip()]
    for n, ln in enumerate(lines, 1):
        digits = ln[:len(ln) - len(ln.lstrip(b'0123456789'))]
        if not digits:
            sys.exit(f'{host}: line {n} has no line number: {ln[:40]!r}')
        if any(c > 127 for c in ln):
            sys.exit(f'{host}: line {n} has non-ASCII characters')
    return b''.join(ln + b'\r' for ln in lines)


def main():
    args = [a for a in sys.argv[1:]]
    basic = '--basic' in args
    if basic:
        args.remove('--basic')
    opts = {'--type': 'A', '--attr': '10'}
    for k in list(opts):
        if k in args:
            i = args.index(k)
            opts[k] = args[i + 1]
            del args[i:i + 2]
    if len(args) != 3:
        sys.exit(__doc__)
    path, host, name = args
    img = bytearray(open(path, 'rb').read())
    disk = is_disk(path, img)
    data = open(host, 'rb').read()
    if basic:
        data = basic_text(data, host)
    nblk = max(1, (len(data) + 1023) // 1024)
    last = len(data) - (nblk - 1) * 1024

    d1 = bytearray(get_block(img, 1, disk))
    if d1[13:17] != b'\x55\xaa\x00\xff':
        sys.exit('no EOS directory in block 1')
    dirsize = d1[12] & 0x7F
    blocks = [bytearray(get_block(img, 1 + i, disk)) for i in range(dirsize)]
    slots = [(bi, e) for bi in range(dirsize) for e in range(39) if not (bi == 0 and e == 0)]

    def entry(s):
        bi, e = s
        return blocks[bi][e * 26:(e + 1) * 26]

    left = None
    for k, s in enumerate(slots):
        r = entry(s)
        if r[0:11] == b'BLOCKS LEFT':
            left = k
            break
    if left is None or left + 1 >= len(slots):
        sys.exit('no BLOCKS LEFT entry, or the directory is full')
    bl = bytearray(entry(slots[left]))
    start = int.from_bytes(bl[13:17], 'little')
    free = int.from_bytes(bl[17:19], 'little')
    if nblk > free:
        sys.exit(f'{nblk} blocks needed, {free} free')
    nm = (name.encode('latin1')[:10] + opts['--type'].encode('latin1')[:1] + b'\x03').ljust(12, b'\x03')
    ent = bytearray(26)
    ent[0:12] = nm
    ent[12] = int(opts['--attr'], 16)
    ent[13:17] = start.to_bytes(4, 'little')
    ent[17:19] = nblk.to_bytes(2, 'little')
    ent[19:21] = nblk.to_bytes(2, 'little')
    ent[21:23] = last.to_bytes(2, 'little')
    bl[13:17] = (start + nblk).to_bytes(4, 'little')
    bl[17:19] = (free - nblk).to_bytes(2, 'little')
    for k, row in ((left, ent), (left + 1, bl)):
        bi, e = slots[k]
        blocks[bi][e * 26:(e + 1) * 26] = row
    for i in range(nblk):
        put_block(img, start + i, data[i * 1024:(i + 1) * 1024], disk)
    for i in range(dirsize):
        put_block(img, 1 + i, bytes(blocks[i]), disk)
    open(path, 'wb').write(img)
    print(f'{name} ({opts["--type"]}): {len(data)} bytes in blocks {start}-{start + nblk - 1}, {free - nblk} blocks left')


main()
