#!/bin/bash
# tests/regress.sh - end-to-end regression run of adamPC against real ADAM software.
#
# Needs build/adampc and the ROMs in roms/. Several tests also need software images in media/
# (not in the repository, bring your own; a test whose files are missing prints SKIP):
#   media/SmartBasic v1.2 (1983)(Coleco - LMI)/...dsk          SmartBASIC 1.2 disk
#   media/Towers of Hanoi (198x)(Matz, Phil)/...bas             a SmartBASIC listing
#   media/CPM-Smartwriter Filter Utility (1985)(Chesware)/...bas
#   media/CP-M 2.2 & Assembler (1984) (Coleco)/...dsk           CP/M 2.2 system disk
#   media/Log Maker (19xx)(-)/...lbr                            Log Maker for CP/M
#   media/ColecoFF slice78/slice78_mc512.rom                    coleco-ff Final Fantasy build
#   roms/adam_fdc/*.u10                                         the alternative disk ROMs
# Every test runs headless on copies under build/regress/ and checks something that does not
# need eyes: printed text, EOS directories, where blocks land on the disk, the device list
# EOS built. Tests run in parallel; the run prints PASS/FAIL/SKIP per test and exits 1 on failure.
#
#   tests/regress.sh            all tests
#   tests/regress.sh NAME...    only these (see the list at the bottom)

cd "$(dirname "$0")/.." || exit 1
EMU=build/adampc
OUT=build/regress
PY="python -I"
SB="media/SmartBasic v1.2 (1983)(Coleco - LMI)/SmartBasic v1.2 (1983)(Coleco - LMI).dsk"
mkdir -p "$OUT"
[ -x "$EMU" ] || [ -x "$EMU.exe" ] || { echo "build $EMU first (see README)"; exit 1; }
[ -f roms/adam/os7.u2 ] || { echo "no ADAM ROMs in roms/ (see README, Bring your own files)"; exit 1; }

HANOI="media/Towers of Hanoi (198x)(Matz, Phil)/Towers of Hanoi (198x)(Matz, Phil).bas"
FILTER="media/CPM-Smartwriter Filter Utility (1985)(Chesware)/CPM-Smartwriter Filter Utility (1985)(Chesware).bas"
FF="media/ColecoFF slice78/slice78_mc512.rom"
LM="media/Log Maker (19xx)(-)/Log Maker (19xx)(-).lbr"
CPM="media/CP-M 2.2 & Assembler (1984) (Coleco)/CP-M 2.2 & Assembler (1984) (Coleco).dsk"

pass() { echo "PASS $1"; }
fail() { echo "FAIL $1: $2"; }
need() {   # TEST FILE...: SKIP the test unless every file is there
    local t=$1; shift
    for f in "$@"; do [ -f "$f" ] || { echo "SKIP $t: no $f"; return 1; }; done
}

# SmartBASIC builds its device list at boot and again from the HELLO menu: the keyboard
# (node 1) must be in it, or no key is ever read (DMA timing matters here, see RECON)
t_sb_devices() {
    need sb_devices "$SB" || return
    local o=$OUT/sb_devices; mkdir -p $o; cp "$SB" $o/sb.dsk
    $EMU --headless --disk $o/sb.dsk --frames 3000 --dump-ram $o/ram.bin >/dev/null 2>&1
    local nodes
    nodes=$($PY -c "
import sys
d=open(sys.argv[1],'rb').read(); p=d[0xFD70]|d[0xFD71]<<8
print(' '.join(str(d[p+4+i*21+16]) for i in range(d[p+3])))" $o/ram.bin)
    [ "$nodes" = "1 2 4 8" ] && pass sb_devices || fail sb_devices "DCB nodes '$nodes', expected '1 2 4 8'"
}

# typewriter: typed line comes out of the printer
t_sw_print() {
    local o=$OUT/sw_print; mkdir -p $o; rm -f $o/page.txt
    $EMU --headless --printer $o/page.txt --frames 1500 --type-at 300 --type "The quick brown fox~0D" >/dev/null 2>&1
    grep -q "The quick brown fox" $o/page.txt 2>/dev/null && [ -s $o/page.png ] && pass sw_print || fail sw_print "printout: $(cat $o/page.txt 2>/dev/null)"
}

# boot from a data pack, and a block written and read back through EOS: test tapes built by
# tools/mktesttape.py (own boot code); the message is found in VRAM through the tape's font
vram_has() {   # VRAMFILE "WANTED" "ALL MESSAGES|..."
    $PY -c "
import sys
msgs = sys.argv[3].split('|'); chars = sorted(set(''.join(msgs)) | {' '})
sys.exit(0 if bytes(chars.index(c) for c in sys.argv[2]) in open(sys.argv[1], 'rb').read()[:0x4000] else 1)" "$@"
}
t_tape_boot() {
    local o=$OUT/tape_boot; mkdir -p $o
    $PY tools/mktesttape.py $o/t.ddp
    $EMU --headless --ddp $o/t.ddp --frames 2400 --dump-vram $o/v.bin >/dev/null 2>&1
    vram_has $o/v.bin "ADAM TAPE BOOT OK" "ADAM TAPE BOOT OK" && pass tape_boot || fail tape_boot "no boot message"
}
t_tape_rw() {
    local o=$OUT/tape_rw; mkdir -p $o
    $PY tools/mktesttape.py --rw $o/t.ddp
    $EMU --headless --ddp $o/t.ddp --frames 2400 --dump-vram $o/v.bin >/dev/null 2>&1
    vram_has $o/v.bin "TAPE WRITE OK" "TAPE WRITE OK|TAPE WRITE BAD|WRITE ERR|READ ERR" && pass tape_rw || fail tape_rw "no TAPE WRITE OK"
}

# SmartWriter STORE to a new data pack
t_tape_store() {
    local o=$OUT/tape_store; mkdir -p $o; rm -f $o/t.ddp
    $EMU --headless --ddp $o/t.ddp --ddp-new --frames 9000 --type-at 2400 \
         --type "~1B||HELLO TAPE FROM SMARTWRITER|~93||~85||~83||MYDOC|~86" >/dev/null 2>&1
    $PY tools/eosdir.py $o/t.ddp | grep -q "^MYDOC .*type H" && pass tape_store || fail tape_store "no MYDOC on tape"
}

# a typed-in listing saved by SmartBASIC
t_sb_typein() {
    need sb_typein "$SB" "$HANOI" || return
    local o=$OUT/sb_typein; mkdir -p $o; cp "$SB" $o/sb.dsk
    $EMU --headless --disk $o/sb.dsk --frames 45000 --type-at 3400 --type "~03|NEW~0D||" \
         --type-file "$HANOI" \
         --type-after "||SAVE HANOI~0D" >/dev/null 2>&1
    $PY tools/eosdir.py $o/sb.dsk | grep -q "^HANOI .*type A" && pass sb_typein || fail sb_typein "HANOI not saved"
}

# a listing put on the disk as text (eosput --basic): SmartBASIC LOADs it and SAVEs it back
# under another name byte for byte (SmartBASIC keeps programs as text, no tokens)
t_sb_basic() {
    need sb_basic "$SB" "$HANOI" || return
    local o=$OUT/sb_basic; mkdir -p $o; cp "$SB" $o/sb.dsk
    $PY tools/eosput.py $o/sb.dsk "$HANOI" HANOI --basic >/dev/null
    $EMU --headless --disk $o/sb.dsk --frames 8000 --type-at 3400 \
         --type "~03|NEW~0D||LOAD HANOI~0D||||||SAVE HANOI2~0D" >/dev/null 2>&1
    $PY tools/eosdir.py $o/sb.dsk --get HANOI $o/a.bin >/dev/null
    $PY tools/eosdir.py $o/sb.dsk --get HANOI2 $o/b.bin >/dev/null 2>&1
    cmp -s $o/a.bin $o/b.bin && pass sb_basic || fail sb_basic "HANOI2 missing or different"
}

# CPM-Smartwriter Filter Utility: two disk drives, EOS file in, converted file out
t_filter() {
    need filter "$SB" "$FILTER" || return
    local o=$OUT/filter; mkdir -p $o; cp "$SB" $o/d5.dsk; rm -f $o/d6.dsk
    printf 'CP/M TEXT FILE\r\nSECOND LINE\twith a tab\r\n\x1a' > $o/cpm.txt
    $PY tools/eosput.py $o/d5.dsk $o/cpm.txt CPMTEXT >/dev/null
    { cat "$FILTER"; printf 'RUN\r\n'; } > $o/run.txt
    $EMU --headless --disk $o/d5.dsk --disk2 $o/d6.dsk --disk-new --frames 45000 --type-at 3400 --type "~03|NEW~0D||" \
         --type-file $o/run.txt --type-after "|||x|||||CPMTEXT~0D|||SWTEXT~0D" >/dev/null 2>&1
    $PY tools/eosdir.py $o/d6.dsk --dump 2 | head -2 | grep -q "SECOND LINE with" && pass filter || fail filter "SWTEXT missing or wrong"
}

# a disk controller ROM: physical format (block FACE/FACF), EOS directory, marker blocks
# written through EOS and found where the ROM's mapping puts them
floppy() {   # name rom bytes blocks fmtblock spt layout
    local name=$1 rom=$2 size=$3 nb=$4 fb=$5 spt=$6 layout=$7
    need fdc_$name "$SB" "$rom" || return
    local o=$OUT/fdc_$name; mkdir -p $o; cp "$SB" $o/sb.dsk
    head -c $size /dev/zero > $o/d2.dsk
    $PY tests/tools/mkfmt.py $o/fmt.txt $nb $fb T$name
    local mid=$((nb / 2)) last=$((nb - 1))
    $PY tests/tools/mkblk.py $o/blk.txt 5 2 9 17 $((mid - 1)) $mid $((mid + 7)) $last
    $EMU --headless --disk $o/sb.dsk --disk2 $o/d2.dsk --fdc-rom2 "$rom" --frames 26000 --type-at 3400 \
         --type "~03|NEW~0D||" --type-file $o/fmt.txt >/dev/null 2>&1
    $EMU --headless --disk $o/sb.dsk --disk2 $o/d2.dsk --fdc-rom2 "$rom" --frames 30000 --type-at 3400 \
         --type "~03|NEW~0D||" --type-file $o/blk.txt >/dev/null 2>&1
    local sides=2; [ $layout = std ] && sides=1
    local r
    r=$($PY tests/tools/findmk.py $o/d2.dsk $spt $sides | $PY tests/tools/expect_map.py $layout)
    [ $? = 0 ] && pass fdc_$name || fail fdc_$name "$r"
}
# Super Game Module AY-3-8910: tone and envelope frequencies from a hand-assembled cartridge
t_sgm_ay() {
    local o=$OUT/sgm_ay; mkdir -p $o
    $PY tests/tools/aytest.py rom $o/ay.rom
    $EMU --headless --coleco --sgm --cart $o/ay.rom --frames 480 --wav $o/ay.wav >/dev/null 2>&1
    local r; r=$($PY tests/tools/aytest.py check $o/ay.wav)
    [ $? = 0 ] && pass sgm_ay || fail sgm_ay "$r"
}

# VDP profiler, slot model: OTIR in the vertical border, a 30-cycle loop, the 27-cycle z88dk
# loop and OTIR in the active display, five sprites on a line, idle loop (see the script)
t_vdp_prof() {
    local o=$OUT/vdp_prof; mkdir -p $o
    local v r msg="" ok=1
    for v in otir_vblank loop30 loop27 otir; do
        $PY tests/tools/vdptest.py rom $v $o/$v.rom
        $EMU --headless --coleco --cart $o/$v.rom --frames 300 --prof-csv $o/$v.csv >/dev/null 2>&1
        r=$($PY tests/tools/vdptest.py check $v $o/$v.csv) || { ok=0; msg="$msg $r"; }
    done
    [ $ok = 1 ] && pass vdp_prof || fail vdp_prof "$msg"
}

# Final Fantasy on ColecoVision + SGM + MegaCart 512K (the user's own build of the coleco-ff
# port, BYOA): through the legend, class select and menus to the overworld, on a ColecoVision
# and on an ADAM; the party (4 layered sprites) must stand at the same place in both
t_ff_sgm() {
    need ff_sgm "$FF" || return
    local o=$OUT/ff_sgm; mkdir -p $o
    $EMU --headless --coleco --sgm --cart "$FF" --joy tests/media/ff_overworld.joy --frames 2800 --dump-vram $o/cv.bin >/dev/null 2>&1
    $EMU --headless --sgm --cart "$FF" --joy tests/media/ff_overworld.joy --frames 2800 --dump-vram $o/adam.bin >/dev/null 2>&1
    local r
    r=$($PY -c "
import sys
for f in sys.argv[1:]:
    d=open(f,'rb').read(); sat=(d[0x4005]&0x7F)<<7
    party=[d[sat+4*i] for i in range(4)]
    if party!=[0x57]*4: print(f, 'party sprites at', party); sys.exit(1)
" $o/cv.bin $o/adam.bin)
    [ $? = 0 ] && pass ff_sgm || fail ff_sgm "$r"
}

# Log Maker (CP/M, the user's .lbr): LBR + CrLZH decompression (checksums), the files copied
# to a CP/M data disk, LOGMAKER.COM run under CP/M 2.2 from drive B, the chat script it
# writes read back from the disk
t_logmaker() {
    need logmaker "$LM" "$CPM" || return
    local o=$OUT/logmaker; rm -rf $o; mkdir -p $o
    $PY tools/lbr.py x "$LM" $o/x | grep -q "LOGMAKER.COM .*checksum OK" || { fail logmaker "decompression"; return; }
    cp "$CPM" $o/a.dsk; $PY tools/cpmdisk.py new $o/b.dsk
    for f in LOGMAKER.COM LOGMAKER.HLP MODINIT; do $PY tools/cpmdisk.py put $o/b.dsk $o/x/$f >/dev/null; done
    $EMU --headless --disk $o/a.dsk --disk2 $o/b.dsk --frames 7000 --type-at 700 \
         --type "B:~0D|||LOGMAKER ELEPHANT 619-270-3148~0D|||||||||||||||Y||||||||||N" >/dev/null 2>&1
    $PY tools/cpmdisk.py get $o/b.dsk ELEP. $o/ELEP.txt >/dev/null 2>&1
    grep -q "atdt6192703148" $o/ELEP.txt 2>/dev/null && pass logmaker || fail logmaker "no chat script on drive B"
}

R=roms/adam_fdc
t_fdc_std()     { floppy std     "$R/adam disk u10 ad 31 rev a 09-27-84.u10" 163840  160  FACE 8  std; }
t_fdc_320ta()   { floppy 320ta   "$R/320ta.u10"                            327680  320  FACE 8  320; }
t_fdc_dbl()     { floppy dbl     "$R/dbl2-4.u10"                           327680  320  FACE 8  320; }
t_fdc_pmhd()    { floppy pmhd    "$R/pmhdfdc.u10"                          327680  320  FACE 8  320; }
t_fdc_fast()    { floppy fast    "$R/fastpack 720a,t.u10"                  737280  720  FACE 9  720; }
t_fdc_a720()    { floppy a720    "$R/a720dipi 7607 mmsg =c= 1988.u10"      737280  720  FACE 9  720; }
t_fdc_1440hd()  { floppy 1440hd  "$R/1440k micro innovations hd-dd.u10"    1474560 1440 FACE 18 1440; }
t_fdc_1440dd()  { floppy 1440dd  "$R/1440k micro innovations hd-dd.u10"    737280  720  FACF 9  720; }

ALL="sgm_ay vdp_prof ff_sgm logmaker tape_boot tape_rw sb_devices sw_print tape_store sb_typein sb_basic filter fdc_std fdc_320ta fdc_dbl fdc_pmhd fdc_fast fdc_a720 fdc_1440hd fdc_1440dd"
[ $# -gt 0 ] && ALL="$*"
for t in $ALL; do t_$t > $OUT/$t.result 2>&1 & done
wait
status=0
for t in $ALL; do cat $OUT/$t.result; grep -q "^PASS\|^SKIP" $OUT/$t.result || status=1; done
echo "$(cat $(for t in $ALL; do echo $OUT/$t.result; done) | grep -c '^PASS') passed, $(cat $(for t in $ALL; do echo $OUT/$t.result; done) | grep -c '^SKIP') skipped"
exit $status
