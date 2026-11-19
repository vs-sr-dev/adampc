# adamPC

A low-level emulator of the **Coleco ADAM** (1983), written from scratch in C from public
documentation and from the machine's own firmware. Every microcontroller on AdamNet runs its
original ROM: the AdamNet protocol is not reimplemented, it emerges from the 6801s talking
to each other over an emulated serial wire.

**Work in progress.** All the ADAM hardware is emulated: the machine boots into SmartWriter,
the keyboard types through the full AdamNet chain, and tape, disk and printer work through
their own 6801 firmware.

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

Runs: SmartWriter (typewriter and word processor, printing, STORE/GET on tape), SmartLOGO
(tape), SmartBASIC 1.2 (disk and tape, SAVE/LOAD, typed-in listings), CP/M 2.2 + WordStar,
Adam Links Golf, Market Monitor.

## Bring your own files

The repository holds only code written for this project, tools and documentation. Nothing that
belongs to someone else is in it: no ROMs, no software images, not even disassemblies of the
firmware (they would be the firmware itself). You supply them:

| What | Where | Used for |
|---|---|---|
| The ADAM ROM set (MAME layout) | `roms/` (see "Use") | everything |
| Disk, tape and cartridge images | anywhere, e.g. `media/` | the software you want to run |
| Firmware listings | `tools/gendis.sh` writes them to `docs/dis/` from your ROMs | following docs/RECON.md |

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
the AdamNet master), `--dump-ram FILE`.

    build/adampc                      # window
    build/adampc --cart game.col      # ColecoVision cartridge (game reset)
    build/adampc --headless --frames 900 --type "Hello ADAM" --png shot.png --wav sound.wav
    build/adampc --headless --trace-net 2> net.log   # every byte on AdamNet
    build/adampc --ddp tape.ddp       # data pack in drive 1 (--ddp2 for drive 2)
    build/adampc --disk cpm.dsk --disk2 ws.dsk   # disk drives 1 and 2 (drive 2 only if given)
    build/adampc --headless --disk sb.dsk --type "~03|NEW~0D" --png s --shots 600
                                      # typing: ~XX = ADAM key code in hex, | = 1 s pause;
                                      # --shots N saves s.<frame>.png every N frames
    build/adampc --headless --disk sb.dsk --type-at 3000 --type "~03|NEW~0D||"                  --type-file prog.bas --type-after "||RUN~0D"
                                      # type a BASIC listing (plain text, CR/LF lines), then more keys
    build/adampc --disk sb.dsk --disk2 big.dsk --fdc-rom2 "roms/adam_fdc/1440k micro innovations hd-dd.u10"
                                      # drive 2 with another controller ROM (image size = geometry:
                                      # 160K, 320K, 720K, 1.44M)
    build/adampc --xram 128           # memory expander (64K, or 64K banks through port 0x42)
    build/adampc --ddp new.ddp --ddp-new   # missing image: created as an empty EOS volume
                                      # (--disk-new for disks), as an initialised data pack

Disk images are written back when changed. Drop a `.dsk` on the window to put it in drive 1.
`tools/eosdir.py IMAGE` lists the EOS directory of a disk or tape image (standard 160K layout),
`tools/eosput.py IMAGE HOSTFILE NAME` copies a file into one.

A `.ddp` image (256 x 1K blocks) is written back when the emulated machine changed it. Tape access runs at the real
speed of the drive (a block every ~0.9 s, ~20 s to boot from a tape whose block 0 sits mid-tape)
but the window speeds up while a tape motor turns (Alt+T toggles it). Drop a `.ddp` on the
window to load it, or a cartridge image. `tools/mktesttape.py` builds test tapes
(a boot block that prints a message; `--rw` writes and reads back a block through EOS).

Keys: the PC keyboard is the ADAM keyboard. F1-F6 SmartKeys I-VI, F7 WILD CARD, F8 UNDO,
F9 MOVE/COPY, F10 STORE/GET, F11 CLEAR, F12 PRINT, Insert, Delete, Home, Caps Lock = LOCK,
Esc = ESCAPE/WP. Left Alt + R computer reset, + G game reset, + P screenshot,
+ 1..0 / - / = controller keypad. Controller 1: gamepad, or numpad 8/4/6/2, numpad 0 and
numpad Enter for the two buttons.

### Porting Applesoft programs: The Oregon Trail (1975)

SmartBASIC follows Applesoft closely, so Apple II BASIC listings run with few changes.
`tools/a2dos.py` reads Apple DOS 3.3 disk images (catalog, files, Applesoft detokenized to a
listing). `tools/oregon2adam.py` ports the Applesoft version of the 1975 MECC Oregon Trail by
Chris Torrence (2015, OREGON1975APPLE.DSK, Internet Archive): its 6502 typing timer becomes a
Z80 routine, its tables fit SmartBASIC's 31 columns. With your copy of the disk:

    python tools/a2dos.py OREGON1975APPLE.DSK OREGON1975 oregon_apple.bas
    python tools/oregon2adam.py oregon_apple.bas oregon_adam.bas
    cp smartbasic.dsk oregon.dsk      # a copy of your SmartBASIC 1.2 disk
    build/adampc --headless --disk oregon.dsk --frames 330000 --type-at 3400                  --type "~03|NEW~0D||" --type-file oregon_adam.bas --type-after "||SAVE OREGON~0D"
    build/adampc --disk oregon.dsk    # Ctrl+C to leave the menu, RUN OREGON (~50 s to load), Caps Lock

## Tests

    build/zextest tests/zex/zexall.com
    tests/regress.sh          # real software end to end (needs the images in media/):
                              # printing, tape STORE, typed-in BASIC, two-drive file
                              # conversion, format + block mapping of every disk ROM

## Documentation

- [docs/hardware_research.md](docs/hardware_research.md): hardware reference compiled from
  public documents, with sources.
- [docs/RECON.md](docs/RECON.md): what the firmware disassembly showed (in Italian).
- `docs/dis/`: disassemblies of the 6801 ROMs, generated from your ROMs by `tools/gendis.sh`
  (addresses in RECON refer to them).
