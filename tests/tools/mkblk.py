import sys
# usage: mkblk.py OUT DEVICE BLOCK... -> SmartBASIC listing: write marker 'B' lo hi '!' to each block, read back
out, dev, blocks = sys.argv[1], int(sys.argv[2]), [int(x) for x in sys.argv[3:]]
w = [0x3E,dev, 0x01,0,0, 0xED,0x5B,0x74,0x7C, 0x21,0x80,0x7C, 0xCD,0xF6,0xFC, 0x32,0x70,0x7C, 0xC9]
r = [0x3E,dev, 0x01,0,0, 0xED,0x5B,0x74,0x7C, 0x21,0x80,0x7C, 0xCD,0xF3,0xFC, 0x32,0x71,0x7C, 0xC9]
L = ['10 LOMEM :33280',
     '20 FOR i=0 TO 18: READ b: POKE 31744+i,b: NEXT',
     '25 FOR i=0 TO 18: READ b: POKE 31776+i,b: NEXT',
     '30 FOR k=1 TO %d: READ b: POKE 31860,b-INT(b/256)*256: POKE 31861,INT(b/256)' % len(blocks),
     '40 POKE 31872,66: POKE 31873,b-INT(b/256)*256: POKE 31874,INT(b/256): POKE 31875,33',
     '50 CALL 31744',
     '60 POKE 31872,0: POKE 31873,0: POKE 31874,0: CALL 31776',
     '70 PRINT b;" W";PEEK(31856);" R";PEEK(31857);" ";PEEK(31872);" ";PEEK(31873)+256*PEEK(31874)',
     '80 NEXT',
     '100 DATA ' + ','.join(map(str, w)),
     '110 DATA ' + ','.join(map(str, r)),
     '120 DATA ' + ','.join(map(str, blocks)),
     'RUN']
open(out, 'w', newline='').write('\r\n'.join(L) + '\r\n')
