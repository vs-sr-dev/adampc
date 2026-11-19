import sys
# usage: findmk.py IMAGE SECTORS SIDES : where are the 'B' lo hi '!' markers (cyl, side, sector ID)
d = open(sys.argv[1], 'rb').read(); spt = int(sys.argv[2]); sides = int(sys.argv[3])
for o in range(0, len(d), 512):
    if d[o] == 66 and d[o+3] == 33:
        s = o // 512; sec = s % spt + 1; t = s // spt
        print('block', d[o+1] | d[o+2] << 8, '-> cyl', t // sides, 'side', t % sides, 'sector ID', sec)
