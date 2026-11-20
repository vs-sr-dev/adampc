#!/usr/bin/env python3
"""lbr.py - CP/M .LBR libraries and CrLZH-compressed members.

    python tools/lbr.py list LIBRARY.LBR
    python tools/lbr.py x LIBRARY.LBR OUTDIR [--raw]     extract; CrLZH members (.?Y?) are
                                                        decompressed unless --raw
    python tools/lbr.py unlzh FILE.?Y? [OUTFILE]          decompress one CrLZH file

LBR (Gary Novosielski): 128-byte sectors, a directory of 32-byte entries at the start (entry 0
describes the directory itself, its length in sectors at offset 14): status (0 = active),
name (8), extension (3), first sector (2), length in sectors (2), CRC, dates.

CrLZH (Roger Warren, 1989-1991; LZHREL.DOC in CRLZH20.LBR) uses Steve Greenberg's CRUNCH file
layout: 76 FD, the original NAME.EXT, an optional [comment], 00, then four bytes (generating
program revision, significant revision, check type 0 = checksum, spare 05), the coded data, and
the 16-bit sum of the decoded bytes (little-endian, on the byte boundary after the end code).
The coding is LZHUF (Okumura's LZSS + Yoshizaki's adaptive Huffman, 1988); the version 2
details were found here by decoding real files and are confirmed by that checksum:
  - ring buffer of 2048 bytes, filled with spaces, writing from position 2048 - 60
  - adaptive Huffman over 315 symbols: 0-255 bytes, 256 end of file, 257-314 copies of 3-60
  - a copy's position is 11 bits: the upper 6 with LZHUF's fixed code of 3 to 8 bits (read 8
    bits, then the rest of the code), the lower 5 bits plain; the copy starts r - position - 1
Version 1.x files (significant revision 1x) use other parameters and are not decoded here.
The tree rebuild (when the root count reaches 0x8000, i.e. after 32768 symbols) follows LZHUF
and has not met a file that long yet; a mismatch would show as a checksum error.
"""
import os
import sys

# --------------------------------------------------------------------- LBR

def members(data):
    nsec = data[14] | data[15] << 8
    out = []
    for i in range(1, nsec * 4):
        e = data[i * 32:(i + 1) * 32]
        if len(e) < 32 or e[0] != 0:
            continue
        name = bytes(c & 0x7F for c in e[1:9]).decode('latin1').strip()
        ext = bytes(c & 0x7F for c in e[9:12]).decode('latin1').strip()
        idx, ln = e[12] | e[13] << 8, e[14] | e[15] << 8
        out.append((name + ('.' + ext if ext else ''), data[idx * 128:(idx + ln) * 128]))
    return out


def kind(body):
    if body[:2] == b'\x76\xfd': return 'CrLZH'
    if body[:2] == b'\x76\xfe': return 'crunched'
    if body[:2] == b'\x76\xff': return 'squeezed'
    return ''

# --------------------------------------------------------------------- CrLZH

NSYM = 315                    # 256 bytes + end code + 58 copy lengths
EOF_CODE = 256
WINDOW, LOOKAHEAD = 2048, 60
MAXFREQ = 0x8000


def position_tables():
    """Upper 6 bits of a position: value v has a code of len_of(v) bits; the decoder looks the
    code up by the first 8 bits it reads (codes are prefix-free and in increasing order)."""
    upper, length = [0] * 256, [0] * 256
    code = v = 0
    for bits, count in ((3, 1), (4, 3), (5, 8), (6, 12), (7, 24), (8, 16)):
        for _ in range(count):
            span = 1 << (8 - bits)
            upper[code:code + span] = [v] * span
            length[code:code + span] = [bits] * span
            code += span
            v += 1
    return upper, length


POS_UPPER, POS_LEN = position_tables()


class BitReader:
    def __init__(self, data):
        self.data, self.pos, self.cur, self.left = data, 0, 0, 0

    def bit(self):
        if not self.left:
            if self.pos >= len(self.data):
                raise ValueError('coded data ends before the end code')
            self.cur = self.data[self.pos]
            self.pos += 1
            self.left = 8
        self.left -= 1
        return (self.cur >> self.left) & 1

    def bits(self, n):
        v = 0
        for _ in range(n):
            v = v << 1 | self.bit()
        return v


class AdaptiveHuffman:
    """The LZHUF tree: leaves are symbols, internal nodes kept sorted by frequency; after
    each symbol its leaf's count goes up and nodes are swapped to keep the order."""
    def __init__(self, n):
        self.n = n
        T = self.T = 2 * n - 1
        self.root = T - 1
        self.freq = [0] * (T + 1)
        self.son = [0] * T
        self.parent = [0] * (T + n)
        for i in range(n):
            self.freq[i] = 1
            self.son[i] = i + T
            self.parent[i + T] = i
        i = 0
        for j in range(n, T):
            self.freq[j] = self.freq[i] + self.freq[i + 1]
            self.son[j] = i
            self.parent[i] = self.parent[i + 1] = j
            i += 2
        self.freq[T] = 0xFFFF                     # sentinel
        self.parent[self.root] = 0

    def rebuild(self):
        """Halve the counts and rebuild the tree when the root count reaches MAXFREQ."""
        T, n = self.T, self.n
        j = 0
        for i in range(T):
            if self.son[i] >= T:
                self.freq[j] = (self.freq[i] + 1) // 2
                self.son[j] = self.son[i]
                j += 1
        i = 0
        for j in range(n, T):
            f = self.freq[i] + self.freq[i + 1]
            k = j - 1
            while f < self.freq[k]:
                k -= 1
            k += 1
            self.freq[k + 1:j + 1] = self.freq[k:j]
            self.freq[k] = f
            self.son[k + 1:j + 1] = self.son[k:j]
            self.son[k] = i
            i += 2
        for i in range(T):
            k = self.son[i]
            self.parent[k] = i
            if k < T:
                self.parent[k + 1] = i

    def update(self, sym):
        if self.freq[self.root] == MAXFREQ:
            self.rebuild()
        c = self.parent[sym + self.T]
        while True:
            self.freq[c] += 1
            f = self.freq[c]
            l = c + 1
            if f > self.freq[l]:
                while f > self.freq[l + 1]:
                    l += 1
                self.freq[c], self.freq[l] = self.freq[l], f
                i = self.son[c]
                self.parent[i] = l
                if i < self.T:
                    self.parent[i + 1] = l
                j = self.son[l]
                self.son[l] = i
                self.parent[j] = c
                if j < self.T:
                    self.parent[j + 1] = c
                self.son[c] = j
                c = l
            c = self.parent[c]
            if c == 0:
                break

    def decode(self, rd):
        c = self.son[self.root]
        while c < self.T:
            c = self.son[c + rd.bit()]
        sym = c - self.T
        self.update(sym)
        return sym


def unlzh(raw):
    """CrLZH file -> (original name, data). Raises ValueError on a bad file."""
    if raw[:2] != b'\x76\xfd':
        raise ValueError('not a CrLZH file')
    z = raw.index(0, 2)
    head = bytes(c & 0x7F for c in raw[2:z]).decode('latin1')   # high bits: CP/M attributes
    name = head.split('[')[0].strip()
    rev, sig, check = raw[z + 1], raw[z + 2], raw[z + 3]
    if sig >> 4 != 2:
        raise ValueError('LZH significant revision %X.%X: only version 2 is decoded' % (sig >> 4, sig & 15))
    rd = BitReader(raw[z + 5:])
    tree = AdaptiveHuffman(NSYM)
    ring = bytearray(b' ' * WINDOW)
    r = WINDOW - LOOKAHEAD
    out = bytearray()
    while True:
        sym = tree.decode(rd)
        if sym < 256:
            out.append(sym)
            ring[r] = sym
            r = (r + 1) & (WINDOW - 1)
            continue
        if sym == EOF_CODE:
            break
        first = rd.bits(8)
        extra = rd.bits(POS_LEN[first] - 3)
        low = ((first << (POS_LEN[first] - 3)) | extra) & 31
        pos = POS_UPPER[first] << 5 | low
        src = (r - pos - 1) & (WINDOW - 1)
        for k in range(sym - 254):
            ch = ring[(src + k) & (WINDOW - 1)]
            out.append(ch)
            ring[r] = ch
            r = (r + 1) & (WINDOW - 1)
    tail = raw[z + 5 + rd.pos:z + 5 + rd.pos + 2]
    if check == 0 and len(tail) == 2 and (tail[0] | tail[1] << 8) != sum(out) & 0xFFFF:
        raise ValueError('checksum mismatch: file %04X, data %04X' % (tail[0] | tail[1] << 8, sum(out) & 0xFFFF))
    return name, bytes(out)

# --------------------------------------------------------------------- commands

def main(argv):
    if len(argv) < 2 or argv[0] not in ('list', 'x', 'unlzh'):
        print(__doc__); return 2
    data = open(argv[1], 'rb').read()
    if argv[0] == 'unlzh':
        name, out = unlzh(data)
        dest = argv[2] if len(argv) > 2 else name
        open(dest, 'wb').write(out)
        print('%s -> %s, %d bytes, checksum OK' % (argv[1], dest, len(out)))
        return 0
    ms = members(data)
    if argv[0] == 'list':
        for name, body in ms:
            k = kind(body)
            if k == 'CrLZH':
                z = body.index(0, 2)
                k += ' (%s)' % body[2:z].decode('latin1').split('[')[0].strip()
            print('%-14s %6d bytes  %s' % (name, len(body), k))
        return 0
    out = argv[2] if len(argv) > 2 and not argv[2].startswith('--') else '.'
    raw = '--raw' in argv
    os.makedirs(out, exist_ok=True)
    status = 0
    for name, body in ms:
        note = ''
        if not raw and kind(body) == 'CrLZH':
            try:
                name, body = unlzh(body)
                note = 'decompressed, checksum OK'
            except ValueError as e:
                note = 'left as it is: %s' % e
                status = 1
        safe = name.replace('/', '_').replace('\\', '_').rstrip('.')   # NAME. (no type) -> NAME
        open(os.path.join(out, safe), 'wb').write(body)
        print('%-14s %6d bytes  %s' % (name, len(body), note))
    return status


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
