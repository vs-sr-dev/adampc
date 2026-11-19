#!/usr/bin/env python3
"""Build a test data pack image (.ddp, 256 x 1K blocks).

Block 0 is a boot block (EOS loads it at 0xC800 and jumps there): it switches the VDP to
text mode, loads a tiny font and prints a message. Blocks 1..255 are filled with their own
block number, so a read test can check what came back.

usage: mktesttape.py OUT.ddp [MESSAGE]
       mktesttape.py --rw OUT.ddp      boot block writes block 5 through EOS, reads it
                                       back, compares and prints the outcome
"""
import sys

GLYPHS = {
    ' ': [0, 0, 0, 0, 0, 0, 0],
    'A': [0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001],
    'B': [0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110],
    'I': [0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110],
    'R': [0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001],
    'W': [0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b11011, 0b10001],
    'D': [0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110],
    'E': [0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111],
    'K': [0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001],
    'M': [0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001],
    'O': [0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110],
    'P': [0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000],
    'T': [0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100],
}


class Asm:
    """just enough of a Z80 assembler: raw bytes, labels, relative jumps, 16-bit refs"""
    def __init__(self, org):
        self.org, self.code, self.labels, self.fix = org, bytearray(), {}, []

    def label(self, name): self.labels[name] = self.org + len(self.code)
    def db(self, *b): self.code += bytes(b)
    def rel(self, op, name): self.code.append(op); self.fix.append(('r', len(self.code), name)); self.code.append(0)
    def abs16(self, op, name): self.code.append(op); self.fix.append(('a', len(self.code), name)); self.code += b'\0\0'

    def link(self):
        for kind, at, name in self.fix:
            t = self.labels[name]
            if kind == 'r':
                d = t - (self.org + at + 1)
                assert -128 <= d < 128, name
                self.code[at] = d & 0xFF
            else:
                self.code[at] = t & 0xFF
                self.code[at + 1] = t >> 8
        return bytes(self.code)


def boot_block(message, rw=False):
    msgs = {'ok': 'TAPE WRITE OK', 'bad': 'TAPE WRITE BAD', 'werr': 'WRITE ERR', 'rerr': 'READ ERR'} if rw else {'m': message}
    chars = sorted(set(''.join(msgs.values())) | {' '})
    index = {c: i for i, c in enumerate(chars)}
    a = Asm(0xC800)
    if rw:
        a.db(0x78); a.abs16(0x32, 'dev')                     # ld a,b / ld (dev),a
        a.db(0x21, 0x00, 0xD0, 0x01, 0x00, 0x04, 0x1E, 0x00)  # ld hl,D000h / ld bc,400h / ld e,0
        a.label('fill')
        a.db(0x7B, 0xEE, 0x5A, 0x77, 0x23, 0x1C, 0x0B, 0x78, 0xB1)  # ld a,e xor 5Ah ld (hl),a inc hl inc e dec bc ld a,b or c
        a.rel(0x20, 'fill')
        a.abs16(0x3A, 'dev')                                  # ld a,(dev)
        a.db(0x01, 0, 0, 0x11, 5, 0, 0x21, 0x00, 0xD0)        # ld bc,0 / ld de,5 / ld hl,D000h
        a.db(0xCD, 0xF6, 0xFC)                                # call WRITE_1_BLOCK
        a.rel(0x20, 'werr')
        a.db(0x21, 0x00, 0xD4, 0x01, 0x00, 0x04)              # ld hl,D400h / ld bc,400h
        a.label('clr')
        a.db(0x36, 0x00, 0x23, 0x0B, 0x78, 0xB1)              # ld (hl),0 inc hl dec bc ld a,b or c
        a.rel(0x20, 'clr')
        a.abs16(0x3A, 'dev')
        a.db(0x01, 0, 0, 0x11, 5, 0, 0x21, 0x00, 0xD4)
        a.db(0xCD, 0xF3, 0xFC)                                # call READ_1_BLOCK
        a.rel(0x20, 'rerr')
        a.db(0x21, 0x00, 0xD0, 0x11, 0x00, 0xD4, 0x01, 0x00, 0x04)
        a.label('cmp')
        a.db(0x1A, 0xBE)                                      # ld a,(de) / cp (hl)
        a.rel(0x20, 'bad')
        a.db(0x23, 0x13, 0x0B, 0x78, 0xB1)                    # inc hl inc de dec bc ld a,b or c
        a.rel(0x20, 'cmp')
        a.abs16(0x21, 'msg_ok'); a.rel(0x18, 'show')
        a.label('bad'); a.abs16(0x21, 'msg_bad'); a.rel(0x18, 'show')
        a.label('werr'); a.abs16(0x21, 'msg_werr'); a.rel(0x18, 'show')
        a.label('rerr'); a.abs16(0x21, 'msg_rerr')
    else:
        a.abs16(0x21, 'msg_m')
    a.label('show')
    a.db(0xE5)                                   # push hl (message)
    a.db(0xF3)                                   # di
    a.abs16(0x21, 'regs')                        # ld hl,regs
    a.db(0x0E, 0xBF, 0x06, 0x80)                 # ld c,0BFh / ld b,80h
    a.label('rl')
    a.db(0x7E, 0xED, 0x79, 0xED, 0x41, 0x23, 0x04, 0x78, 0xFE, 0x88)  # ld a,(hl) out (c),a out (c),b inc hl inc b ld a,b cp 88h
    a.rel(0x20, 'rl')                            # jr nz,rl
    a.db(0xAF, 0xED, 0x79, 0x3E, 0x40, 0xED, 0x79)  # VRAM write address 0x0000
    a.db(0x11, 0xC0, 0x03)                       # ld de,960
    a.label('cl')
    a.db(0xAF, 0xD3, 0xBE, 0x1B, 0x7A, 0xB3)     # xor a / out (0BEh),a / dec de / ld a,d / or e
    a.rel(0x20, 'cl')
    a.db(0xAF, 0xED, 0x79, 0x3E, 0x48, 0xED, 0x79)  # VRAM write address 0x0800 (patterns)
    a.abs16(0x21, 'font')
    a.db(0x11, (len(chars) * 8) & 0xFF, (len(chars) * 8) >> 8)
    a.label('fl')
    a.db(0x7E, 0xD3, 0xBE, 0x23, 0x1B, 0x7A, 0xB3)  # ld a,(hl) / out (0BEh),a / inc hl / dec de / ld a,d / or e
    a.rel(0x20, 'fl')
    pos = 11 * 40 + 13
    a.db(0x3E, pos & 0xFF, 0xED, 0x79, 0x3E, 0x40 | (pos >> 8), 0xED, 0x79)
    a.db(0xE1)                                   # pop hl (message)
    a.label('tl')
    a.db(0x7E, 0xFE, 0xFF)                       # ld a,(hl) / cp 0FFh
    a.rel(0x28, 'done')
    a.db(0xD3, 0xBE, 0x23)                       # out (0BEh),a / inc hl
    a.rel(0x18, 'tl')
    a.label('done')
    a.rel(0x18, 'done')
    a.label('regs')                              # text mode, name 0x0000, patterns 0x0800, white on dark blue
    a.db(0x00, 0xD0, 0x00, 0x00, 0x01, 0x00, 0x00, 0xF4)
    a.label('font')
    for c in chars:
        a.db(*[(r << 3) & 0xFF for r in GLYPHS[c]], 0)
    for k, m in msgs.items():
        a.label('msg_' + k)
        a.db(*[index[c] for c in m], 0xFF)
    a.label('dev')
    a.db(0)
    code = a.link()
    assert len(code) <= 1024
    return code + bytes(1024 - len(code))


def main():
    rw = sys.argv[1] == '--rw'
    args = sys.argv[2:] if rw else sys.argv[1:]
    out = args[0]
    msg = args[1] if len(args) > 1 else 'ADAM TAPE BOOT OK'
    img = bytearray(boot_block(msg, rw))
    for b in range(1, 256):
        img += bytes([b]) * 1024
    open(out, 'wb').write(img)


main()
