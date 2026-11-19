import sys
# Z80 code at 7C00 (free RAM between SmartBASIC, which ends at 7B6B, and 8000), buffer 7C80.
# usage: mkfmt.py OUT BLOCKS FORMATBLOCK_HEX NAME  -> SmartBASIC listing that formats + inits d6
out, nblk, fb, name = sys.argv[1], int(sys.argv[2]), int(sys.argv[3], 16), sys.argv[4]
code = [0x3E,0x05, 0x01,0x00,0x00, 0x11,fb&255,fb>>8, 0x21,0x80,0x7C, 0xCD,0xF6,0xFC,
        0x32,0x70,0x7C, 0xF5,0xC1,0x79, 0x32,0x71,0x7C,
        0x3E,0x05, 0x0E,0x01, 0x11,nblk&255,nblk>>8, 0x21,0x40,0x7C, 0xCD,0xBD,0xFC,
        0x32,0x72,0x7C, 0xF5,0xC1,0x79, 0x32,0x73,0x7C, 0xC9]
nm = [ord(c) for c in name] + [3]
lines = ['10 LOMEM :33280',
         '20 FOR i=0 TO %d: READ b: POKE 31744+i,b: NEXT' % (len(code)-1),
         '25 FOR i=0 TO %d: READ b: POKE 31808+i,b: NEXT' % (len(nm)-1),
         '27 FOR i=31872 TO 32895: POKE i,0: NEXT',
         '30 CALL 31744',
         '40 PRINT "RESULT";PEEK(31856);PEEK(31857);PEEK(31858);PEEK(31859)']
for k in range(0, len(code), 12):
    lines.append('%d DATA %s' % (100 + k, ','.join(str(x) for x in code[k:k+12])))
lines.append('200 DATA ' + ','.join(str(x) for x in nm))
lines.append('RUN')
open(out, 'w', newline='').write('\r\n'.join(lines) + '\r\n')
