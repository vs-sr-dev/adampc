#!/usr/bin/env python3
"""Port Chris Torrence's Applesoft Oregon Trail (1975 MECC version) to ADAM SmartBASIC.

usage: oregon2adam.py APPLE_LISTING OUT_LISTING [COUNTS_PER_SECOND]

APPLE_LISTING comes from tools/a2dos.py (the OREGON1975 file of OREGON1975APPLE.DSK).
Changes, everything else is left as it is (SmartBASIC follows Applesoft):
- TYPETIME, the 6502 ampersand routine that times typing "BANG", becomes a Z80 routine at
  0x7C00 (free RAM between the SmartBASIC interpreter, which ends at 0x7B6B, and 0x8000;
  LOMEM is raised above it). Same algorithm: a 24-bit counter runs while the keyboard is
  polled through EOS (FCA8 starts a 1-key read, FC4B checks it), wrong keys are ignored,
  RETURN ends, RETURN before the whole word = maximum time. Lower case counts as upper case.
- The two inventory tables were laid out for 40 columns; SmartBASIC's text screen has 31.
"""
import sys

CODE, WORD, CNT, IDX = 0x7C00, 0x7C80, 0x7C90, 0x7C93
FCA8, FC4B = 0xFCA8, 0xFC4B


def assemble():
    lo, hi = lambda v: v & 0xFF, lambda v: v >> 8
    prog = [
        ('CALL', FCA8),                     # start a keyboard read
        [0xAF], ('ST', CNT), ('ST', CNT + 1), ('ST', CNT + 2), ('ST', IDX),
        'LOOP', [0x21, lo(CNT), hi(CNT)],   # LD HL,CNT : 24-bit increment
        [0x34], ('JR', 0x20, 'POLL'),       # INC (HL) / JR NZ
        [0x23, 0x34], ('JR', 0x20, 'POLL'),
        [0x23, 0x34], ('JR', 0x28, 'FAIL'), # counter wrapped: too slow
        'POLL', ('CALL', FC4B),
        ('JR', 0x30, 'LOOP'),               # NC: read still pending
        ('JR', 0x20, 'ERR'),                # NZ: error
        [0xF5], ('CALL', FCA8), [0xF1],     # got a key: start the next read
        [0xFE, 0x0D], ('JR', 0x28, 'DONE'), # RETURN
        [0xFE, ord('a')], ('JR', 0x38, 'CMP'),
        [0xFE, ord('z') + 1], ('JR', 0x30, 'CMP'),
        [0xD6, 0x20],                       # to upper case
        'CMP', [0x4F], ('LD', IDX), [0x5F, 0x16, 0x00, 0x21, lo(WORD), hi(WORD), 0x19, 0x7E, 0xB9],
        ('JR', 0x20, 'LOOP'),               # wrong key: ignored
        ('LD', IDX), [0x3C], ('ST', IDX), ('JR', 0x18, 'LOOP'),
        'ERR', ('CALL', FCA8), ('JR', 0x18, 'LOOP'),
        'DONE', ('LD', IDX), [0x5F, 0x16, 0x00, 0x21, lo(WORD), hi(WORD), 0x19, 0x7E, 0xFE, 0x0D],
        ('JR', 0x28, 'OK'),
        'FAIL', [0x3E, 0xFF], ('ST', CNT), ('ST', CNT + 1), ('ST', CNT + 2),
        'OK', [0xC9],
    ]
    labels, out = {}, []
    for pass_ in range(2):
        out, pc = [], CODE
        for it in prog:
            if isinstance(it, str):
                labels[it] = pc
                continue
            if isinstance(it, list):
                b = it
            elif it[0] == 'CALL':
                b = [0xCD, lo(it[1]), hi(it[1])]
            elif it[0] == 'ST':
                b = [0x32, lo(it[1]), hi(it[1])]
            elif it[0] == 'LD':
                b = [0x3A, lo(it[1]), hi(it[1])]
            else:
                rel = labels.get(it[2], pc) - (pc + 2)
                assert pass_ == 0 or -128 <= rel <= 127
                b = [it[1], rel & 0xFF]
            out += b
            pc += len(b)
    assert len(out) <= WORD - CODE
    return out


def main():
    src, dst = sys.argv[1], sys.argv[2]
    rate = float(sys.argv[3]) if len(sys.argv) > 3 else 0
    lines = {}
    for ln in open(src, encoding='ascii').read().splitlines():
        if ln.strip():
            n, rest = ln.split(' ', 1)
            lines[int(n)] = rest
    code = assemble()
    for n in [k for k in lines if 4801 <= k <= 4818]:
        del lines[n]                         # the 6502 bytes
    for i in range(0, len(code), 10):
        lines[4801 + i // 10] = 'DATA ' + ','.join(map(str, code[i:i + 10]))
    lines[1] = 'LOMEM : 32256: REM  [ADAM] TYPETIME (Z80) LIVES AT 31744-32255'
    lines[4799] = 'REM  [ADAM] LOAD THE Z80 TYPETIME AT 7C00H'
    lines[4830] = f'FOR D1 = 0 TO {len(code) - 1}: READ D: POKE {CODE} + D1,D: NEXT'
    lines[4840] = 'REM  [ADAM] NO AMPERSAND VECTOR: CALL 31744'
    lines[4860] = 'REM'
    del lines[4870]
    word = 'BANG'
    lines[4510] = (f'FOR I = 1 TO {len(word)}: POKE {WORD - 1} + I, ASC ( MID$ ("{word}",I,1)): NEXT : '
                   f'POKE {WORD + len(word)},13: CALL {CODE}: PRINT')
    scale = f'/ {rate:.0f}' if rate else '* 0'
    lines[4520] = (f'B1 = (PEEK ({CNT}) + 256 * PEEK ({CNT + 1}) + 65536 * PEEK ({CNT + 2})) {scale}'
                   ': REM  [ADAM] COUNTS TO SECONDS')
    for h in (1160, 4220):
        lines[h] = 'PRINT "FOOD  BULLT CLOTH SUPPL CASH"'
    for a, b in ((1165, 1166), (4285, 4286)):
        lines[a] = 'PRINT INT (F);: HTAB 7: PRINT INT (B);: HTAB 13: PRINT INT (C);'
        lines[b] = 'HTAB 19: PRINT INT (M1);: HTAB 25: PRINT INT (T): PRINT'
    out = ''.join(f'{n} {lines[n]}\r\n' for n in sorted(lines))
    open(dst, 'w', newline='').write(out)
    print(f'{len(lines)} lines, TYPETIME {len(code)} bytes', file=sys.stderr)


main()
