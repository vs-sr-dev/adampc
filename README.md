# adamPC

A low-level emulator of the **Coleco ADAM** (1983), written from scratch in C from public
documentation and from the machine's own firmware. Every microcontroller on AdamNet runs its
original ROM: the AdamNet protocol is not reimplemented, it emerges from the 6801s talking
to each other over an emulated serial wire.

**Work in progress.** All the ADAM hardware is emulated: the machine boots into SmartWriter,
the keyboard types through the full AdamNet chain, and tape, disk and printer work through
their own 6801 firmware. It also runs as a plain **ColecoVision**, with the **Super Game
Module** and **MegaCart** cartridges, and has a **VDP and frame-time profiler** meant for
people writing ColecoVision/ADAM software (see "Profiling").

## What is emulated

| Part | Chip | Status |
|---|---|---|
| CPU | Z80A @ 3.579545 MHz | complete, passes zexdoc and zexall (undocumented flags, MEMPTR, Q) |
| Video | TMS9928A | Graphics I/II, Multicolor, Text, sprites (4/line, 5th flag, collision), NMI |
| Sound | SN76489A | 3 tones + noise, write-timed rendering |
| Memory | MIOC | port 0x7F map (SmartWriter/EOS, RAM, OS-7, cartridge, expansion), port 0x3F EOS enable / net reset |
| AdamNet master | MC6801 (single-chip) + its ROM | DMA into Z80 RAM through ports 1/3/4 strobes |
| Keyboard | MC6801 + its ROM | 13x8 matrix |
| AdamNet | 62.5 kbit/s half-duplex wire | frame-level model with echo and SCI wake-up |
| Digital Data Pack | MC6801 (expanded mode) + its ROM, 1K buffer | two drives; tape modelled as bi-phase flux cells with motor, speed, brake and end-of-tape; reads and writes `.ddp` images |
| Disk drives | MC6801 + 4K ROM + WD2793 (one per drive) | `.dsk` images; rotation, MFM byte timing, head stepping, INTRQ -> NMI, format; the alternative controller ROMs for 320K, 720K and 1.44M drives (side and data rate from the 6801's port 1) |
| Printer | MC6801 + its ROM | daisy wheel, carriage, platen and hammer mechanics; the petal facing the paper when the hammer falls goes to a text file and a page image (`--printer FILE` writes FILE and FILE.png; `adampc_printout.txt` in the window); the mechanism is audible |
| Super Game Module | AY-3-8910 @ 1.789772 MHz + 32K RAM | `--sgm`: AY at ports 0x50/0x51/0x52 (tones, noise, envelope); on a ColecoVision also 24K at 0x2000-0x7FFF (port 0x53 bit 0) and 8K over the BIOS (port 0x7F bit 1 = 0) |
| Cartridges | ColecoVision, MegaCart | up to 1 MB; more than 32K = MegaCart: last 16K bank fixed at 0x8000, reading 0xFFC0+N maps bank N at 0xC000 |
| ColecoVision | the same Z80, VDP and PSG | `--coleco`: no ADAM, 1K of RAM mirrored at 0x6000-0x7FFF, BIOS = OS-7 |

Runs: SmartWriter (typewriter and word processor, printing, STORE/GET on tape), SmartLOGO
(tape), SmartBASIC 1.2 (disk and tape, SAVE/LOAD, typed-in listings), CP/M 2.2 + WordStar,
Adam Links Golf, Market Monitor, Log Maker (CP/M); the coleco-ff port of Final Fantasy
(SGM + 512K MegaCart) on both the ColecoVision and the ADAM.

## Bring your own files

The repository holds only code written for this project, tools and documentation. Nothing that
belongs to someone else is in it: no ROMs, no software images, not even disassemblies of the
firmware (they would be the firmware itself). You supply them:

| What | Where | Used for |
|---|---|---|
| The ADAM ROM set (MAME layout) | `roms/` (see "Use") | everything |
| Disk, tape and cartridge images | anywhere, e.g. `media/` | the software you want to run |
| Firmware listings | `tools/gendis.sh` writes them to `docs/dis/` from your ROMs | following docs/RECON.md |
| zexdoc / zexall (Frank Cringle's Z80 instruction exercisers, `.com`) | `tests/zex/` | the CPU test |
| The software the end-to-end tests run (list at the top of `tests/regress.sh`) | `media/` | `tests/regress.sh` |

Media are written back when the emulated machine changes them: work on copies.

## Build

MSYS2 MinGW64 with CMake, Ninja and SDL2:

    cmake -S . -B build -G Ninja
    cmake --build build

Without SDL2 the build is headless only.

## Use

ROMs go in `roms/` (the layout of the MAME sets, unzipped): `roms/adam/` (os7.u2, the three
SmartWriter `alf` chips, the EOS chip, the master 6801), `roms/adam_kb/keyboard.u2`,
`roms/adam_ddp/tape rev a 8865.u24`, `roms/adam_prn/printer.u2`,
`roms/adam_fdc/adam disk u10 ad 31 rev a 09-27-84.u10`.

Debugging: `--trace-net` (AdamNet bytes), `--trace-ddp`/`--trace-ddp2` (tape), `--trace-wd`
(disk controller), `--trace-prn` (printer strikes), `--trace-eos` (calls into the EOS jump
table), `--trace-z80 FROM TO` (frames), `--watch ADDR` (writes to a RAM byte by the Z80 and
the AdamNet master), `--trace-ay` (Super Game Module sound), `--dump-ram FILE`,
`--dump-vram FILE` (16K of VRAM, then the 8 VDP registers).

    build/adampc                      # window
    build/adampc --cart game.col      # ColecoVision cartridge (game reset)
    build/adampc --headless --frames 900 --type "Hello ADAM" --png shot.png --wav sound.wav
    build/adampc --headless --trace-net 2> net.log   # every byte on AdamNet
    build/adampc --ddp tape.ddp       # data pack in drive 1 (--ddp2 for drive 2)
    build/adampc --disk cpm.dsk --disk2 ws.dsk   # disk drives 1 and 2 (drive 2 only if given)
    build/adampc --headless --disk sb.dsk --type "~03|NEW~0D" --png s --shots 600
                                      # typing: ~XX = ADAM key code in hex, | = 1 s pause;
                                      # --shots N saves s.<frame>.png every N frames
    build/adampc --headless --disk sb.dsk --type-at 3000 --type "~03|NEW~0D||" \
                 --type-file prog.bas --type-after "||RUN~0D"
                                      # type a BASIC listing (plain text, CR/LF lines), then more keys
    build/adampc --disk sb.dsk --disk2 big.dsk --fdc-rom2 "roms/adam_fdc/1440k micro innovations hd-dd.u10"
                                      # drive 2 with another controller ROM (image size = geometry:
                                      # 160K, 320K, 720K, 1.44M)
    build/adampc --xram 128           # memory expander (64K, or 64K banks through port 0x42)
    build/adampc --ddp new.ddp --ddp-new   # missing image: created as an empty EOS volume
                                      # (--disk-new for disks), as an initialised data pack

Disk images are written back when changed. Drop a `.dsk` on the window to put it in drive 1.
`tools/eosdir.py IMAGE` lists the EOS directory of a disk or tape image (standard 160K layout),
`tools/eosput.py IMAGE HOSTFILE NAME` copies a file into one, `tools/eosdir.py IMAGE --get NAME
HOSTFILE` copies one out. SmartBASIC 1.2 keeps programs as plain text (a saved program is its
lines, number first, ending in CR; `LOAD` reads them as if typed), so a BASIC listing goes on a
disk or data pack directly: `tools/eosput.py IMAGE listing.bas NAME --basic`, then `LOAD NAME`.

CP/M disks: `tools/cpmdisk.py dir|get|put|era|new IMAGE ...` reads and writes files on an ADAM
CP/M 2.2 disk (160K; the disk parameter block of the ADAM BIOS). `tools/lbr.py list|x LIB.LBR`
opens CP/M libraries and decompresses their CrLZH members (`.?Y?`, Roger Warren's LZH, version 2,
checksum verified); `tools/lbr.py unlzh FILE` does a single file. For example, Log Maker:

    python tools/lbr.py x "Log Maker.lbr" lm
    python tools/cpmdisk.py new b.dsk
    python tools/cpmdisk.py put b.dsk lm/LOGMAKER.COM
    build/adampc --disk cpm_system.dsk --disk2 b.dsk      # A>B:  then  LOGMAKER bbsname number

A `.ddp` image (256 x 1K blocks) is written back when the emulated machine changed it. Tape
access runs at the real speed of the drive (a block every ~0.9 s, ~20 s to boot from a tape whose block 0 sits mid-tape)
but the window speeds up while a tape motor turns (Alt+T toggles it). Drop a `.ddp` on the
window to load it, or a cartridge image. `tools/mktesttape.py` builds test tapes
(a boot block that prints a message; `--rw` writes and reads back a block through EOS).

Keys: the PC keyboard is the ADAM keyboard. F1-F6 SmartKeys I-VI, F7 WILD CARD, F8 UNDO,
F9 MOVE/COPY, F10 STORE/GET, F11 CLEAR, F12 PRINT, Insert, Delete, Home, Caps Lock = LOCK,
Esc = ESCAPE/WP. Left Alt + R computer reset, + G game reset, + P screenshot,
+ 1..0 / - / = controller keypad. Controller 1: gamepad, or numpad 8/4/6/2, numpad 0 and
numpad Enter for the two buttons.

### ColecoVision, Super Game Module, MegaCart

    build/adampc --coleco --sgm --cart game.rom        # ColecoVision + SGM
    build/adampc --sgm --cart game.rom                 # the same cartridge on an ADAM (+ the SGM's AY)

Only `roms/adam/os7.u2` is needed for `--coleco` (OS-7 is the ColecoVision BIOS, the same
8K). Controller 1: gamepad, arrow keys + Z / X (with `--coleco`), or numpad 8/4/6/2 + numpad 0 /
Enter for the two buttons; Alt+digit for the keypad. `--joy FILE` plays a joystick script
("FRAME tokens" per line: up down left right fire1 fire2 k0-k9 k* k#, `-` = nothing, `p2` =
controller 2; the state holds until the next line), so a game can be driven headless to the
scene you want to look at.

### Profiling

`--prof` watches what a TMS9918A program does that a forgiving emulator lets pass, and prints a
summary at exit (in the window, running totals go in the title bar):

- **VRAM transfers, slot by slot.** The TMS9918A lets the CPU at the VRAM only in fixed slots
  of each line: in Graphics I/II one every 16 memory cycles at worst, in Text one in three, in
  the vertical border and with the screen off one in two (`src/vdpslots.h`, read by
  `tools/vdpslots.py` off the openMSX timing diagram, which is built on TI's 1978 master timing
  chart). A request is carried out in the first slot after a delay of about 2 µs (8-13 VDP
  cycles, 11 most likely). A new request before that slot finds the previous transfer still
  pending: "lost" (even with an 8-cycle delay), "likely lost" (11), "marginal" (12-13). The
  model gives 28.7 Z80 cycles as the Graphics worst case and 11.3 for Text: the "29 cycles" and
  "12 cycles" rules. The report lists the instructions (PC and MegaCart bank); OTIR (21 cycles)
  during the active display is the classic case. What the chip does with the pending byte is not
  documented, so nothing is dropped from VRAM.
- **Sprites per line:** lines with more than four sprites and how many sprite-lines are lost.
  Sprites parked at Y = 0 are on screen (lines 1-16) and count; 0xD0 ends the table.
- **Frame time:** the share of each frame from the frame interrupt to the program's idle loop
  (found automatically: a short backward jump taken again and again without changing memory,
  or HALT); frames that never reach it are overruns. Also the NMI handler's time.
- **Races:** an NMI handler that touches the VDP control port between the main program's two
  address bytes, or moves the address under a transfer in progress; status reads outside the
  NMI handler that take a set frame flag; an NMI handler that returns with another MegaCart
  bank mapped than the one it interrupted (the main program then reads the wrong bank).

`--prof-csv FILE` writes the same frame by frame, `--prof-log N` prints the first N risky
accesses, `--prof-from N` (headless) starts at frame N. `--prof-pc FILE` counts Z80 cycles per
instruction address and bank; `tools/profsym.py` turns that into a per-function table from
z88dk `.map` files (static functions included) or plain "ADDR name" lists:

    build/adampc --headless --coleco --sgm --cart ff.rom --joy walk.joy --frames 2750 \
                 --prof-from 2451 --prof-pc walk.pc
    python tools/profsym.py walk.pc slice78.map 20:ovl_battle.map

### Porting Applesoft programs: The Oregon Trail (1975)

SmartBASIC follows Applesoft closely, so Apple II BASIC listings run with few changes.
`tools/a2dos.py` reads Apple DOS 3.3 disk images (catalog, files, Applesoft detokenized to a
listing). `tools/oregon2adam.py` ports the Applesoft version of the 1975 MECC Oregon Trail by
Chris Torrence (2015, OREGON1975APPLE.DSK, Internet Archive): its 6502 typing timer becomes a
Z80 routine, its tables fit SmartBASIC's 31 columns. With your copy of the disk:

    python tools/a2dos.py OREGON1975APPLE.DSK OREGON1975 oregon_apple.bas
    python tools/oregon2adam.py oregon_apple.bas oregon_adam.bas
    cp smartbasic.dsk oregon.dsk      # a copy of your SmartBASIC 1.2 disk
    python tools/eosput.py oregon.dsk oregon_adam.bas OREGON --basic
    build/adampc --disk oregon.dsk    # Ctrl+C to leave the menu, RUN OREGON (~50 s to load), Caps Lock

## Tests

    build/zextest tests/zex/zexall.com     # your copy of zexall (or zexdoc)
    tests/regress.sh                       # 20 end-to-end tests in parallel, about 90 s
    tests/regress.sh sb_basic logmaker     # only these

`tests/regress.sh` checks things that do not need eyes: the printer's text, EOS and CP/M
directories, where blocks land on a disk, VRAM contents, measured frequencies. Some tests need
only the ROMs: boot and block write from test tapes (`tools/mktesttape.py`), SmartWriter
printing and STORE to a new data pack, and the AY and VDP-profiler checks, which run
hand-assembled cartridges built by `tests/tools/`. The others run real software you bring in
`media/` (SmartBASIC, CP/M, Log Maker, the alternative disk ROMs, coleco-ff); without it they
print SKIP.

## Credits

Written from public documents; the sources are listed in
[docs/hardware_research.md](docs/hardware_research.md). Among them:

- Coleco, *ADAM Technical Reference Manual* (1984), and the schematics in the ADAM archive.
- Texas Instruments, *TMS9918A/TMS9928A/TMS9929A Video Display Processors Data Manual* (1982).
- Wouter Vermaelen, *VDP VRAM timing* (openMSX, 2014), whose TMS9918 diagram is built on TI's
  1978 "9918 Master Timing" chart from Karl Guttag's archive: the source of the CPU access
  slots in `src/vdpslots.h`.
- Thomas Cherryhomes, *All About AdamNet* and *The Big Adam Talk*.
- Guy Cousineau, *E.O.S. Programming Manual*; Micro Innovations, *ADAM I/O Port Address Map*.
- General Instrument, AY-3-8910 data sheet; Motorola, MC6801 data sheet.
- Roger Warren, `LZHREL.DOC` (CrLZH), and Nino Brunori's reconstructed ADAM CP/M 2.2 source,
  where the disk parameter block comes from.

No other emulator's source code was used.

## Licence

MIT for the code, tools and documentation in this repository; see [LICENSE](LICENSE), which
also says what the licence does not cover (the firmware and the software the emulator runs).

## Documentation

- [docs/hardware_research.md](docs/hardware_research.md): hardware reference compiled from
  public documents, with sources.
- [docs/RECON.md](docs/RECON.md): what the firmware disassembly showed (in Italian).
- [TODO.md](TODO.md): the development log, session by session, and what is left (in Italian).
- `docs/dis/`: disassemblies of the 6801 ROMs, generated from your ROMs by `tools/gendis.sh`
  (addresses in RECON refer to them).
