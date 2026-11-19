#!/usr/bin/env python3
"""Read files from an Apple II DOS 3.3 disk image (.dsk/.do, 35 x 16 x 256, DOS sector order).

usage: a2dos.py IMAGE                 catalog
       a2dos.py IMAGE NAME [OUT]      extract NAME: Applesoft (A) files are detokenized to a
                                      listing, text (T) files have the high bit stripped,
                                      binary (B) files are written raw (with load address/length)

DOS 3.3 layout: VTOC at track 17 sector 0 (+1/+2 = first catalog sector). Catalog sectors
hold 7 entries of 35 bytes from offset 0x0B: track/sector of the file's track/sector list,
type (bit 7 = locked; 0 T, 1 I, 2 A, 4 B), name (30 bytes, high bit set), length in sectors.
A track/sector list holds the next list's T/S at +1/+2 and up to 122 data T/S pairs from +0x0C.
Applesoft file: 2-byte length, then lines: link (2), line number (2), bytes, 0. Bytes >= 0x80
are tokens (0x80 END ... 0xEA MID$).
"""
import sys

TOKENS = ('END FOR NEXT DATA INPUT DEL DIM READ GR TEXT PR# IN# CALL PLOT HLIN VLIN HGR2 HGR '
          'HCOLOR= HPLOT DRAW XDRAW HTAB HOME ROT= SCALE= SHLOAD TRACE NOTRACE NORMAL INVERSE '
          'FLASH COLOR= POP VTAB HIMEM: LOMEM: ONERR RESUME RECALL STORE SPEED= LET GOTO RUN IF '
          'RESTORE & GOSUB RETURN REM STOP ON WAIT LOAD SAVE DEF POKE PRINT CONT LIST CLEAR GET '
          'NEW TAB( TO FN SPC( THEN AT NOT STEP + - * / ^ AND OR > = < SGN INT ABS USR FRE SCRN( '
          'PDL POS SQR RND LOG EXP COS SIN TAN ATN PEEK LEN STR$ VAL ASC CHR$ LEFT$ RIGHT$ MID$').split()
assert len(TOKENS) == 0xEB - 0x80
TYPES = {0: 'T', 1: 'I', 2: 'A', 4: 'B'}


class Disk:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        if len(self.d) != 143360:
            sys.exit('not a 140K DOS-order image')

    def sec(self, t, s):
        o = (t * 16 + s) * 256
        return self.d[o:o + 256]

    def catalog(self):
        v = self.sec(17, 0)
        t, s = v[1], v[2]
        while t:
            c = self.sec(t, s)
            for e in range(7):
                r = c[0x0B + e * 35:0x0B + (e + 1) * 35]
                if r[0] in (0, 0xFF):
                    continue
                name = bytes(x & 0x7F for x in r[3:33]).decode('ascii').rstrip()
                yield name, TYPES.get(r[2] & 0x7F, '?'), r[33] | r[34] << 8, (r[0], r[1])
            t, s = c[1], c[2]

    def read(self, ts):
        out = b''
        t, s = ts
        while t:
            lst = self.sec(t, s)
            for i in range(122):
                dt, ds = lst[0x0C + 2 * i], lst[0x0D + 2 * i]
                if dt == 0 and ds == 0:
                    continue          # sparse sector (or end of file)
                out += self.sec(dt, ds)
            t, s = lst[1], lst[2]
        return out


def detokenize(data):
    n = data[0] | data[1] << 8
    p, end, lines = 2, 2 + n, []
    while p + 4 <= end:
        link = data[p] | data[p + 1] << 8
        if link == 0:
            break
        num = data[p + 2] | data[p + 3] << 8
        p += 4
        txt = ''
        while data[p]:
            b = data[p]
            if 0x80 <= b <= 0xEA:
                tok = TOKENS[b - 0x80]
                # words get spaces around them; operators stay glued (">" "=" -> ">="),
                # as other BASICs (SmartBASIC) do not read "> =" as one operator
                txt += tok if tok in '+-*/^<>=' else ' ' + tok + ' '
            else:
                txt += chr(b)
            p += 1
        p += 1
        lines.append(f'{num} {txt.strip()}')
    return '\r\n'.join(lines) + '\r\n'


def main():
    disk = Disk(sys.argv[1])
    if len(sys.argv) == 2:
        for name, typ, n, _ in disk.catalog():
            print(f'{typ} {n:4d} {name}')
        return
    want = sys.argv[2]
    for name, typ, n, ts in disk.catalog():
        if name != want:
            continue
        data = disk.read(ts)
        if typ == 'A':
            out = detokenize(data).encode('ascii')
        elif typ == 'T':
            out = bytes(x & 0x7F for x in data).split(b'\0')[0].replace(b'\r', b'\r\n')
        else:
            out = data[4:4 + (data[2] | data[3] << 8)]
            print(f'binary: load address {data[0] | data[1] << 8:04X}, {len(out)} bytes', file=sys.stderr)
        if len(sys.argv) > 3:
            open(sys.argv[3], 'wb').write(out)
        else:
            sys.stdout.write(out.decode('latin1'))
        return
    sys.exit(f'{want}: not found')


main()
