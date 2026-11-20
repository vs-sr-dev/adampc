#!/usr/bin/env python3
"""profsym.py - where the Z80 time goes: adampc --prof-pc output grouped by function.

    python tools/profsym.py PROFILE.pc [MAP ...] [--top N] [--locals]

PROFILE.pc  lines "PC BANK CYCLES" written by adampc --prof-pc (BANK is - outside the
            MegaCart window 0xC000-0xFFFF).
MAP         symbol files. "file.map" applies to every address outside the MegaCart window
            (and to the window too when no bank map is given); "N:file.map" applies to the
            window while bank N is selected. Two formats are understood:
              z88dk:  name = $ADDR ; addr, public, ...      (only "addr" symbols are used)
              plain:  ADDR name                              (hex address, one per line)
--locals    use assembly local labels too (default: public symbols, plus every symbol z88dk
            ties to a C function, static ones included)
--top N     how many lines to print (default 30)
"""
import bisect
import re
import sys

Z88 = re.compile(r'^(\S+)\s*=\s*\$([0-9A-Fa-f]{1,4})\s*;\s*addr,\s*(public|local)')
CFUNC = re.compile(r'\.c::([A-Za-z_]\w*)::')
PLAIN = re.compile(r'^\s*(?:0x|\$)?([0-9A-Fa-f]{1,4})h?\s+(\S+)\s*$')


def load_map(path, locals_too):
    syms = {}
    for line in open(path, errors='replace'):
        m = Z88.match(line)
        if m:
            # z88dk writes the C function a symbol belongs to ("file.c::func::..."): static
            # functions and their internal i_NNN labels are local symbols, but they belong to
            # a function all the same
            fn = CFUNC.search(line)
            if fn:
                syms.setdefault(int(m.group(2), 16), '_' + fn.group(1))
            elif m.group(3) == 'public' or locals_too:
                syms.setdefault(int(m.group(2), 16), m.group(1))
            continue
        m = PLAIN.match(line)
        if m:
            syms.setdefault(int(m.group(1), 16), m.group(2))
    addrs = sorted(syms)
    return addrs, [syms[a] for a in addrs]


def lookup(table, pc):
    if not table:
        return None
    addrs, names = table
    i = bisect.bisect_right(addrs, pc) - 1
    if i < 0:
        return None
    return names[i], addrs[i]


def main(argv):
    top, locals_too, files = 30, False, []
    i = 0
    while i < len(argv):
        if argv[i] == '--top':
            top = int(argv[i + 1]); i += 2; continue
        if argv[i] == '--locals':
            locals_too = True; i += 1; continue
        files.append(argv[i]); i += 1
    if not files:
        print(__doc__); return 2
    prof, maps = files[0], files[1:]
    fixed, banks = None, {}
    for m in maps:
        b, sep, path = m.partition(':')
        if sep and b.isdigit():
            banks[int(b)] = load_map(path, locals_too)
        else:
            fixed = load_map(m, locals_too)

    total, groups = 0, {}
    for line in open(prof):
        f = line.split()
        if len(f) != 3:
            continue
        pc, bank, cyc = int(f[0], 16), f[1], int(f[2])
        total += cyc
        table, tag = fixed, ''
        if bank != '-':
            tag = 'bank %s' % bank
            table = banks.get(int(bank), fixed if not banks else None)
        hit = lookup(table, pc)
        key = (hit[0] if hit else '?%04X' % (pc & 0xFF00), tag)
        groups[key] = groups.get(key, 0) + cyc

    if not total:
        print('empty profile'); return 1
    print('%d Z80 cycles = %.2f s of Z80 time' % (total, total / 3579545.0))
    print('%7s  %12s  %s' % ('share', 'cycles', 'function [bank]'))
    for (name, tag), cyc in sorted(groups.items(), key=lambda kv: -kv[1])[:top]:
        print('%6.1f%%  %12d  %s%s' % (cyc * 100.0 / total, cyc, name, ('  [%s]' % tag) if tag else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
