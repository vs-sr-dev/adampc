#!/usr/bin/env python3
"""Minimal MC6801/6803 disassembler with recursive-descent code discovery.

usage: dis6801.py ROM BASE_HEX [extra entry points hex...]
Vectors at FFF0-FFFF are followed automatically when the ROM covers them.
"""
import sys

INH, IMM8, IMM16, DIR, IDX, EXT, REL = range(7)

OPS = {}
def op(code, name, mode): OPS[code] = (name, mode)

for c, n in {0x01:'NOP',0x04:'LSRD',0x05:'ASLD',0x06:'TAP',0x07:'TPA',0x08:'INX',0x09:'DEX',
             0x0A:'CLV',0x0B:'SEV',0x0C:'CLC',0x0D:'SEC',0x0E:'CLI',0x0F:'SEI',0x10:'SBA',
             0x11:'CBA',0x16:'TAB',0x17:'TBA',0x19:'DAA',0x1B:'ABA',0x30:'TSX',0x31:'INS',
             0x32:'PULA',0x33:'PULB',0x34:'DES',0x35:'TXS',0x36:'PSHA',0x37:'PSHB',0x38:'PULX',
             0x39:'RTS',0x3A:'ABX',0x3B:'RTI',0x3C:'PSHX',0x3D:'MUL',0x3E:'WAI',0x3F:'SWI'}.items():
    op(c, n, INH)
for i, n in enumerate(['BRA','BRN','BHI','BLS','BCC','BCS','BNE','BEQ','BVC','BVS','BPL','BMI','BGE','BLT','BGT','BLE']):
    op(0x20 + i, n, REL)
RMW = {0:'NEG',3:'COM',4:'LSR',6:'ROR',7:'ASR',8:'ASL',9:'ROL',0xA:'DEC',0xC:'INC',0xD:'TST',0xF:'CLR'}
for lo, n in RMW.items():
    op(0x40 + lo, n + 'A', INH); op(0x50 + lo, n + 'B', INH)
    op(0x60 + lo, n, IDX); op(0x70 + lo, n, EXT)
op(0x6E, 'JMP', IDX); op(0x7E, 'JMP', EXT)
ALU = {0:'SUB',1:'CMP',2:'SBC',4:'AND',5:'BIT',6:'LDA',7:'STA',8:'EOR',9:'ADC',0xA:'ORA',0xB:'ADD'}
for base, reg in ((0x80, 'A'), (0xC0, 'B')):
    for k, mode in enumerate((IMM8, DIR, IDX, EXT)):
        for lo, n in ALU.items():
            if lo == 7 and k == 0: continue
            op(base + k*16 + lo, n + reg, mode)
for k, mode in enumerate((IMM16, DIR, IDX, EXT)):
    op(0x83 + k*16, 'SUBD', mode); op(0x8C + k*16, 'CPX', mode); op(0x8E + k*16, 'LDS', mode)
    op(0xC3 + k*16, 'ADDD', mode); op(0xCC + k*16, 'LDD', mode); op(0xCE + k*16, 'LDX', mode)
    if k:
        op(0x8F + k*16, 'STS', mode); op(0xCD + k*16, 'STD', mode); op(0xCF + k*16, 'STX', mode)
op(0x8D, 'BSR', REL)
op(0x9D, 'JSR', DIR); op(0xAD, 'JSR', IDX); op(0xBD, 'JSR', EXT)

REGS = {0x00:'P1DDR',0x01:'P2DDR',0x02:'P1',0x03:'P2',0x04:'P3DDR',0x05:'P4DDR',0x06:'P3',0x07:'P4',
        0x08:'TCSR',0x09:'FRCH',0x0A:'FRCL',0x0B:'OCRH',0x0C:'OCRL',0x0D:'ICRH',0x0E:'ICRL',
        0x0F:'P3CSR',0x10:'RMCR',0x11:'TRCSR',0x12:'RDR',0x13:'TDR',0x14:'RAMCR'}
LEN = {INH:1, IMM8:2, IMM16:3, DIR:2, IDX:2, EXT:3, REL:2}

def main():
    data = open(sys.argv[1], 'rb').read()
    base = int(sys.argv[2], 16)
    end = base + len(data)
    rd = lambda a: data[a - base]
    entries = [int(x, 16) for x in sys.argv[3:]]
    if base <= 0xFFF0 and end > 0xFFFF:
        names = ['SCI','TOF','OCF','ICF','IRQ1','SWI','NMI','RESET']
        for i, n in enumerate(names):
            v = rd(0xFFF0 + i*2) << 8 | rd(0xFFF1 + i*2)
            print(f'; vector {n:5s} -> {v:04X}')
            if base <= v < end: entries.append(v)
    code = {}
    labels = set(entries)
    todo = list(entries)
    while todo:
        pc = todo.pop()
        while base <= pc < end and pc not in code:
            o = rd(pc)
            if o not in OPS:
                code[pc] = (1, f'DB ${o:02X}  ; ???'); break
            n, m = OPS[o]
            ln = LEN[m]
            b = [rd(pc + i) if pc + i < end else 0 for i in range(ln)]
            tgt = None
            if m == INH: s = n
            elif m == IMM8: s = f'{n} #${b[1]:02X}'
            elif m == IMM16: s = f'{n} #${b[1]<<8|b[2]:04X}'
            elif m == DIR:
                r = REGS.get(b[1]); s = f'{n} <${b[1]:02X}' + (f'  ; {r}' if r else '')
                if n == 'JSR': tgt = b[1]
            elif m == IDX: s = f'{n} ${b[1]:02X},X'
            elif m == EXT:
                a = b[1] << 8 | b[2]; s = f'{n} ${a:04X}'
                if n in ('JSR', 'JMP'): tgt = a
            elif m == REL:
                a = (pc + 2 + (b[1] - 256 if b[1] > 127 else b[1])) & 0xFFFF
                s = f'{n} L{a:04X}'; tgt = a
            code[pc] = (ln, s)
            if tgt is not None and base <= tgt < end:
                labels.add(tgt); todo.append(tgt)
            if n in ('RTS', 'RTI', 'JMP', 'BRA', 'SWI'):
                break
            pc += ln
    pc = base
    while pc < end:
        if pc in code:
            ln, s = code[pc]
            hx = ' '.join(f'{rd(pc+i):02X}' for i in range(ln))
            if pc in labels: print(f'L{pc:04X}:')
            print(f'  {pc:04X}  {hx:9s} {s}')
            pc += ln
        else:
            st = pc
            while pc < end and pc not in code: pc += 1
            chunk = data[st - base:pc - base]
            if all(x == 0xFF for x in chunk): print(f'  {st:04X}  ; {pc-st} x FF')
            else:
                for i in range(0, len(chunk), 16):
                    print(f'  {st+i:04X}  DB ' + ','.join(f'${x:02X}' for x in chunk[i:i+16]))

main()
