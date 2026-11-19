# Coleco ADAM – Hardware Reference for an LLE Emulator

Compiled from public documentation only: the Coleco Technical Reference Manual (TRM), Coleco schematics, EOS manuals, community prose write-ups, and chip datasheets. **No emulator source code was read.** In a few places the ROM dumps in `roms/` were disassembled by hand. Those dumps are the target machine's own firmware, not emulator code, and every such fact is tagged `[ROM-DIS]`.

Confidence levels:
- **sure**: stated in a primary document or seen directly in a ROM.
- **likely**: a secondary source, or a clear reading of a schematic.
- **guess**: an inference. Verify before relying on it.

Anything not found in documentation is marked **UNKNOWN**.

## 0. Source key

| Tag | Document |
|---|---|
| TRM | *ADAM Technical Reference Manual, Preliminary Release* (Coleco 1984). PDF: https://wiki.console5.com/tw/images/b/b5/Coleco_ADAM_Technical_Reference_Manual.pdf. Clean transcription: adamarchive.org `Technical/ADAM Tech Manuals/ADAM Technical Reference Manual - Section 1-3.zip` (`Text Format/…txt`, `HTML Format/…`). References use TRM chapter.section. |
| TRM-PDCB | `P_DCB_EQ.ASM` equates printed in the TRM (Ch.5 listings; HTML `p_dcb_eq.asm.html` in the zip above) |
| TRM-8000 | `8000H.ASM` cartridge/super-game header example in the TRM (Ch.5 §1.5) |
| SCH-MIO | Memory & I/O board schematic: `Gamma Board #1.pdf` in the same zip, and TRM App.4.1 sheets 1 and 2 (PDF pp.142–143) |
| SCH-FDC | Coleco dwg 300012 rev A2 "Schematic, Controller/Power Supply, 83-104 Disc Drive 5¼" (`diskboard_final_bitmap.pdf` in the same zip) |
| EOSPM | G. Cousineau, *E.O.S. Programming Manual*: https://www.colecoadam.net/manuals%20&%20docs/e.o.s%20programming%20manual%20(199x)%20(guy%20cousineau).html |
| ANET | T. Cherryhomes, *All About AdamNet* (FujiNet firmware author's bus notes, measured with a logic analyser): https://github.com/FujiNetWIFI/fujinet-config/blob/main/notes/all-about-adamnet.md |
| BAT | T. Cherryhomes, *The Big Adam Talk*: https://github.com/tschak909/the-big-adam-talk/blob/main/the-big-adam-talk.org |
| IOMAP | *ADAM Computer Detailed I/O Port Address Map v3.2* (M. Gordon / Micro Innovations, rev. 2026), adamarchive `Technical/ADAM Tech Manuals/` |
| TWWMCA | R. Drushel, *This Week With My Coleco ADAM*, adamarchive `Technical/` |
| KBM | Archer/Radio Shack 277-1020 "ADAM keyboard, 75 keys, matrix output" sheet, adamarchive `Technical/ADAM Schematics/` |
| KEYS | TRM Appendix 1, "ASCII Keycode Table for the ADAM Keyboard, Rev 9, Sept 6 1983" |
| ADAMEM-DOC | ADAMEm distribution web page: the **prose disk-type table only**, no source code (http://www.komkon.org/~dekogel/adamem.html) |
| ROM-DIS | Hand disassembly of `roms/adam/wp_r80.rom`, `alf @1 rev 57 e3d5.u8`, `os7.u2`, `master rev a 174b.u6` |
| DS6801 | Motorola MC6801/6803 datasheet. General chip facts, not verified against a copy here. |

---

## 1. Z80 side

### 1.1 Clocks and CPU

| Fact | Value | Src | Conf |
|---|---|---|---|
| CPU | Z80A on the CPU board (the ColecoVision board in Expansion Module #3) | TRM 2.2.2 | sure |
| Z80 clock | 7.1 MHz crystal oscillator ÷2 = 3.58 MHz (3.579545). The timing table gives tc = 279.36 ns. | TRM 2.2.8, Table 2 note 11 | sure |
| VDP clock | 10.7 MHz, taken as the 3rd harmonic of 3.58 MHz from a tuned tank | TRM 2.2.8 | sure |
| Chroma | Derived from the 3.58 MHz system clock | TRM 2.2.6 | sure |
| DRAM refresh | The Z80 supplies a 7-bit refresh address. The MIOC generates the 8th bit (RA7) from /MREQ, M1, /WAIT, RFSH, A7. Ignore this for emulation. | TRM 2.1.6, App.2 | sure |
| Wait states | /WAIT inserts cycles "during opcode fetch cycles and when accessing slow memory or I/O". The SN76489 has wait-request hardware. Exact counts are **UNKNOWN**. | TRM 2.1.9a, 2.2.5 | sure (existence) |

### 1.2 Memory map and port 0x7F (MIOC)

The 64K space is split into two 32K halves. Each half takes one of four options, selected by writing to **port 0x7F**. Bits D0–D1 select the lower half and D2–D3 the upper half. D4–D7 are reserved and must be written as 0. [TRM 3.2, 3.2.5] (sure)

| D1 D0 | Lower 0x0000–0x7FFF |
|---|---|
| 00 | SmartWRITER ROM, plus EOS ROM when enabled through port 0x3F bit 1 |
| 01 | 32K intrinsic RAM (lower half of the internal 64K) |
| 10 | 32K expansion RAM (64K Memory Expander in slot #3, lower half) |
| 11 | OS-7 ROM (8K, 0x0000–0x1FFF) + 24K intrinsic RAM (0x2000–0x7FFF) |

| D3 D2 | Upper 0x8000–0xFFFF |
|---|---|
| 00 | 32K intrinsic RAM (upper half) |
| 01 | Expansion ROM (card in connector #2, J6) |
| 10 | 32K expansion RAM (upper half) |
| 11 | Cartridge ROM (32K) |

- Full value table, as given in EOSPM "Memory Banks" (sure). The value equals upper×4 + lower:
  - 0 = SW / RAM
  - 1 = RAM / RAM
  - 2 = XRAM / RAM
  - 3 = OS7 / RAM, the "SmartKey" apps configuration
  - 4–7 = the four lower options with XROM upper
  - 8–11 = the four lower options with XRAM upper
  - 12–15 = the four lower options with cartridge upper; **15 is the cartridge-game configuration**
- **Decoding:** the TRM lists only 0x7F; ports 0x60–0x7E are "reserved".
  - IOMAP and leadedsolder.com (2024) report that the whole range 0x60–0x7F is decoded as the memory-map port. (likely)
  - Reading the port back is "MAY be available" [IOMAP], i.e. **UNKNOWN**.
- **DMA restriction:** AdamNet DMA works only into **intrinsic RAM** [TRM 3.2.1/3.2.2] (sure). The master 6801 always reaches the internal 64K, whatever the Z80's current map [TRM App.2] (likely).
- **Board ROM sockets:** up to 40K of ROM on the memory board [TRM 2.1.5].
  - The decoder is driven by A13, BOOTROMCS and EOS ENABLE [TRM 2.1.5].
  - The schematic shows U21 and U22 as 47128 (16K) parts or optional 2764s, U8 as a 2764, and U23 as a '139 decoder [SCH-MIO] (likely).
  - The ROM set in `roms/adam`: three 8K SmartWriter ROMs (u8, u20, u21), a 16K EOS-6 ROM (u22), and a single 32K `wp_r80.rom` variant.
  - Drushel describes both "3-ROM (8K EOS, 8K+16K SW)" and "4-ROM (8K EOS, 3×8K SW)" board versions [TWWMCA] (sure that both exist).
- **Where EOS ROM appears:** with lower = SmartWriter and port 0x3F bit 1 = 1, EOS ROM is visible at **0x6000–0x7FFF**. EOS_BOOT copies that 8K to RAM at 0xE000 [ROM-DIS, see 1.6] (sure for the first 8K).
  - How the second 8K of the 16K EOS-6 chip is mapped is **UNKNOWN**. It is probably selected via A13 or the u22 decoding (guess).
  - What 0x6000–0x7FFF reads when EOS is disabled with the 3×8K SmartWriter set is **UNKNOWN**.
- **Game-mode RAM:** in ColecoVision/game mode the cartridge RAM sits at 0x7000–0x73FF [TRM 3.9.1], and OS-7 uses 0x7000–0x73FF [TRM 3.8.1].
  - On ADAM, 0x2000–0x7FFF is real 24K RAM [TRM 3.2.1].
  - Whether the ColecoVision 1K mirror behaviour is reproduced is **UNKNOWN** (guess: it is not, because ADAM has real RAM there).

**EOS RAM map (EOS-5/6, upper intrinsic RAM)** [TRM Fig 3-4, EOSPM p.4] (sure)

| Range | Content |
|---|---|
| D390–D3FF | FCB headers (3 FCBs × 36 bytes) |
| D400–DFFF | 3 × 1K FCB buffers (FCB0 is reserved for directory work) |
| E000–F3FF | EOS code: VRAM, joystick, sound, file routines |
| F400–FBFF | AdamNet device drivers / executive / DCB routines |
| FC17–FC26 | Values 0..15 used as memory-configuration codes |
| FC27 | Memory-map port number (0x7F) |
| FC28 | Net-reset port number (0x3F) |
| FC29 / FC2A | VDP control (0xBF = 191) / data (0xBE = 190) |
| FC2B / FC2C | Joystick 0 (0xFC = 252) / joystick 1 (0xFF = 255) input |
| FC2D / FC2E | Strobe set (0x80 = 128) / strobe reset (0xC0 = 192) |
| FC2F | Sound (0xFF = 255) |
| FC30–FD5F | EOS jump table. EOS_START = FC30; SYNC = FCB1; READ_1_BLOCK = FCF3; WRITE_1_BLOCK = FCF6; GOTO_WP = FCE7; SWITCH_MEM_BANKS = FD14; HARD_RESET_NET = FC60 [EOSPM jump table, BAT] |
| FD60–FEBF | Data and stack. FD60 = EOS revision. FD6F = current/boot device (DEFAULT_BT_DEV) [BAT]. |
| FEC0–FEC3 | **PCB** (default) |
| FEC4–FFFE | **DCBs**: 15 × 21 bytes |
| FFFF | "DMA reserved byte" |

The ports in FC27–FC2F are the EOS-6 values [EOSPM "Set VDP ports"] (sure).

### 1.3 Z80 I/O ports

TRM 3.2.4 port table (sure, read from the page image):

| Port | Function |
|---|---|
| 00–1D | Reserved. Later third-party use: Powermate SASI/IDE, MIDI-Mite, 2681 serial [IOMAP]. |
| 1E | Optional auto-dialer |
| 1F | Reserved |
| 20–3E | Reserved |
| **3F** | **Network reset / EOS enable.** Bit 0: set then clear = AdamNet hardware reset pulse. Bit 1: 1 = EOS ROM enabled (only when the SmartWriter ROMs are selected), 0 = disabled. |
| 40–4E | Reserved. Later use: 0x40 parallel printer, **0x42 expansion-RAM bank number** for >64K expanders, 0x44–0x47 serial cards [IOMAP, BAT] (likely). |
| 4F | Expansion connector #2 |
| 50–5D | Reserved |
| 5E | Optional modem (ADAMlink) data I/O |
| 5F | Optional modem control/status |
| 60–7E | Reserved (decoded as 0x7F in practice, see 1.2) |
| **7F** | **Memory-map control** (write) |
| 80–FF | Reserved for ColecoVision use |

- **Port 0x3F reality:** EOS HARD_RESET_NET writes **0x0F** to the port at FC28, waits, then writes an "idle" value [EOSPM p.8] (sure).
  - IOMAP says the reset decode covers all of **0x20–0x3F**, output only (likely).
  - What bits 2–3 do is **UNKNOWN**.
  - EOS_BOOT itself writes 0x02 (EOS on) and then 0x00 to port 0x3F [ROM-DIS].

ColecoVision-compatible ports (A7 = 1). An LS138 decodes A6, A5 and /WR [IOMAP note 7] (likely):

| Range | Write | Read |
|---|---|---|
| 80–9F | **Strobe set**: select keypad / right button ("common 1"). EOS uses 0x80 [EOSPM]. | – |
| A0–BF | VDP. A0 = 0: VRAM data (0xBE). A0 = 1: register/address (0xBF) [TRM 2.2.3]. | VDP: data at even ports, status at odd ports |
| C0–DF | **Strobe reset**: select joystick / left fire ("common 0"). EOS uses 0xC0 [EOSPM]. | – |
| E0–FF | SN76489A, write only; TRM gives port 0xFF [TRM 2.2.5] | Controllers. A1 = 0 → controller 1 (EOS uses 0xFC); A1 = 1 → controller 2 (EOS uses 0xFF). The A1 split is a guess based on FC/FF and FC/FE usage. |

- **Conflicting statement:** TRM Fig 2-5 labels the halves "Common 0 enabled by ports FD, FF" and "Common 1 enabled by ports FC, FE".
  - EOS-6 itself uses OUT 0x80 (set) and 0xC0 (reset) followed by IN 0xFC / 0xFF.
  - Follow the EOS/OS-7 usage (likely). The meaning of the figure is **UNKNOWN**.

**Controller bits (active low)** [TRM Fig 2-5, decoded] (sure):
- Joystick half: D0 = N, D1 = E, D2 = S, D3 = W (diagonals combine bits), D6 = left fire.
- Keypad half: D6 = right fire ("arm"). D0–D3 are pulled low as follows:

| Key | Lines pulled low | Raw low nibble |
|---|---|---|
| 1 | D1 | 0xD |
| 2 | D3 | 0x7 |
| 3 | D0, D1 | 0xC |
| 4 | D0, D2, D3 | 0x2 |
| 5 | D2, D3 | 0x3 |
| 6 | D0 | 0xE |
| 7 | D1, D3 | 0x5 |
| 8 | D1, D2, D3 | 0x1 |
| 9 | D2 | 0xB |
| 0 | D0, D2 | 0xA |
| * | D1, D2 | 0x9 |
| # | D0, D3 | 0x6 |

- Spinner A drives the "INT" line (and the Z80 INT). Spinner B appears on D5 [Fig 2-5] (likely).
- Controller connector pins [TRM 2.2.7]:
  - 1 = D0, 2 = D2, 3 = D3, 4 = D1, 6 = D6, 7 = D5, 9 = INT
  - 5 = common 1 strobe, 8 = common 0 strobe
  - The strobe is about a 350 µs pulse.

### 1.4 Interrupts

| Source | Z80 pin | Notes | Src | Conf |
|---|---|---|---|---|
| TMS9928A VDP INT | **NMI** | Vectored to 0x66. With OS-7 in lower memory, OS-7 jumps to the cartridge NMI vector at **0x8021**. If you map RAM at 0 with no handler, disable VDP interrupts (R1 bit 5) first. Reading the VDP status register (port 0xBF) re-arms the interrupt. | TRM App.3.2, TWWMCA, ROM-DIS (`os7` 0x66: `JP 8021h`) | sure |
| Spinners (controller pin 9) | **/INT** (maskable) | Gated by /SPINDIS (MIOC pin 19, active low). OS-7 RST 38h → cartridge soft vector 0x801E. | TRM 2.1.9a, 2.2.7, TRM-8000 | sure |
| Expansion-port / slot devices | /INT, /NMI | Available on the expansion connectors | TRM 2.1.8 | sure |
| AdamNet / master 6801 | none described | Z80↔6801 communication is polled through PCB/DCB in RAM | TRM 3.3.8, EOSPM | likely |

The interrupt mode is not stated. OS-7 vectors through RST 38h, which implies IM 1 (guess).

### 1.5 Reset and boot

Hardware reset sources [TRM 2.1.9a] (sure):
- **CVRST** ("game"/cartridge reset): resets the Z80, the MIOC and the **master 6801**. The MIOC map becomes 0000–1FFF OS-7, 2000–7FFF RAM, 8000–FFFF cartridge, i.e. **port 0x7F = 0x0F**. Execution starts in OS-7 at 0x0000.
- **PBRST** ("computer" reset) and power-on: the MIOC selects SmartWriter low and intrinsic RAM high (= **0x00**) [TRM 3.2.3]. The Z80 starts at 0x0000 in the SmartWriter ROM, where EOS_BOOT lives.
- The MIOC also has a pin called CPRST, function **UNKNOWN** [SCH-MIO].

**EOS_BOOT sequence**, decoded from ROM-DIS of `wp_r80.rom` 0x0080–0x00CF. Identical in rev 57 `u8`. (sure)
1. `OUT (7Fh),04h`: SmartWriter low, expansion ROM high.
2. If `[8000h]=66h` and `[8001h]=99h`: `LD HL,009Ah` / `JP 8002h`. **Expansion-ROM signature is 66 99 at 0x8000; entry is at 0x8002.**
3. `OUT (7Fh),00h`, then copy ROM 0x0000–0x00FE to RAM 0x8000 and continue at 0x80AE (now running from RAM).
4. `OUT (3Fh),02h` (EOS on), then `LDIR` 0x6000→0xE000, length 0x2000. `OUT (3Fh),00h` (EOS off).
5. `JP FC30h` (EOS_START).

EOS_START / cold start [EOSPM p.7, TRM 3.2.3] (sure):
1. Set the stack, clear the data tables, call SET VDP PORTS, SOUND OFF and FILL VRAM, then bank-switch to the normal configuration.
2. **HARD_INIT**: PCB → 0xFEC0, HARD_RESET_NET, delay, clear DCBs, SYNCHRONIZE CLOCKS (two PCB sync commands), then SCAN FOR DEVICES (STATUS sent to nodes 1–15).
3. INIT FILE MANAGER.
4. Probe for media in this order: **disk 1 (4), disk 2 (5), tape 1 (8), tape 2 (0x18)**.
5. On the first device holding valid media, load **block 0 to 0xC800**, put the device ID in **Z80 register B** (also stored at DEFAULT_BT_DEV), and `JP C800h`.
6. If no medium is found, `JUMP TO SMARTWRITER`: lower = SmartWriter, then jump into it.

Super-game convention [TRM Ch.5 §1.4] (sure): the block-0 loader loads the main program to 0x8000 (a ColecoVision-style header) and passes control via the vector at 0x800A. B holds the boot device ID.

---

## 2. Master 6801 (AdamNet master) and its link to the Z80

| Fact | Value | Src | Conf |
|---|---|---|---|
| Part | MC6801, socket U6 (68701 optional). 2048 bytes ROM, 128 bytes RAM, SCI (UART). | TRM 2.1.2, SCH-MIO | sure |
| Mode | **Single-chip mode** | TRM 2.1.2 | sure |
| Clock | 4.0 MHz crystal (Y1), internal ÷4, so E = **1 MHz**. All ADAM 6801s run at 1 MHz. | TRM 2.1.2, SCH-MIO | sure |
| ROM vectors | Reset → 0xF8AF; all other vectors → 0xFBE6. ROM occupies 0xF800–0xFFFF. | ROM-DIS (master rev A) | sure |
| Internal map | Registers 0x00–0x1F, RAM 0x80–0xFF, ROM 0xF800–0xFFFF | DS6801 | sure (chip) |
| Master RAM layout | NIM_BLOCK and the data module `D_MMR` at 0x83… with D1_MODE_WORD at 0x90. Variables include MEM_PTR, MEM_LEN, NIM_DMA_BLK, etc. | TWWMCA 9611 (reconstructed Coleco listing page) | likely |
| SCI | P23 = NET RxD, P24 = NET TxD (pin 12). Line rate 62.5 kbit/s = E/16. | SCH-MIO, DS6801 | sure |
| NMI / IRQ1 | Both tied high through R11 3.3K: unused | SCH-MIO | likely |

### 2.1 Bus wiring (Memory & I/O schematic, U6 ↔ MIOC U7) [SCH-MIO] (likely; read from a scan)

- **Port 3 (P30–P37) = Z80 data bus BD0–BD7.**
- **Ports 1 and 4 drive the Z80 address bus:**
  - P40 = BA0, P41 = BA1, P42 = BA2, P43 = BA3, P44 = BA4, P45 = BA5, P46 = BA6
  - P47 → RA7 (the A7 / refresh-bit line)
  - P10 = BA8, P11 = BA9, P12 = BA10, P13 = BA11, P14 = BA12, P15 = BA13, P16 = BA14, P17 = BA15
- **IS3 (pin 39) and OS3 (pin 38)**, the 6801 port-3 strobes, go to the MIOC (pins 30 and 31; CR11 diode on IS3).
- **MIOC pins:**
  - Bus/strobe side: IORQ, BM1, BA13–15, BD0–BD3 (the MIOC latches only 4 data bits, enough for 7F/3F), BRD, BWR, BMREQ, BRFSH
  - Control side: ADDRBUFEN, 245EN, MUX, RAS, CAS1, CAS2, **DMA**, **RST**, **BUSRQ**, **BUSAK**, WAIT, CVRST, CPRST, PBRST
  - Function selects: AUX ROMCS, **NET RST**, **SPIN DIS**, BOOT ROMCS, AUX DECODE1
- **Interpretation (guess):** the master bit-bangs a Z80-bus cycle.
  1. Put the address on P1/P4.
  2. Request the bus (the MIOC "DMA" input, driven by a master port-2 line, probably P20 or P21).
  3. The MIOC asserts /BUSRQ, waits for /BUSAK, and enables the buffers.
  4. A port-3 access by the 6801 fires OS3/IS3, and the MIOC turns it into /MREQ + /WR or /RD on DRAM.
- The TRM confirms only that "BUSRQ is generated by the MIOC as the result of a DMA request… generally only the master 6801 may assert BUSRQ" and that "the MIOC is responsible for the setup and execution of DMA by the master 6801" [TRM 2.1.9a, App.2] (sure).
- Drushel calls the MIOC "a totally-undocumented black box" [TWWMCA 9611.25].
- **UNKNOWN:** which port-2 bit requests DMA, whether BUSRQ is held per byte or per burst, the cycle timing, and any Z80-visible effect beyond stolen cycles.

### 2.2 PCB / DCB protocol (Z80 RAM mailbox)

**PCB** (Processor Control Block, 4 bytes, default **0xFEC0**) [TRM-PDCB, EOSPM] (sure):

| Off | Name | Meaning |
|---|---|---|
| 0 | P_COM_STAT | Command (Z80 writes); the 6801 sets bit 7 when it acknowledges |
| 1–2 | P_REL_ADDR | PCB's own address (lo, hi); used by relocation |
| 3 | P_NUM_DCBS | Number of DCBs / devices found (base ADAM = 3: kbd, printer, tape) |

PCB commands: 0 IDLE, 1 SYNC1, 2 SYNC2, 3 SNA (set new PCB address), 4 RESET (reset all nodes), 5 WAIT. The ACK value is the command | 0x80.
- In normal operation the low 7 bits of P_COM_STAT usually read **2** and bit 7 = acknowledged [EOSPM "Find PCB"] (sure).
- RELOCATE PCB writes the new address into bytes 1–2, sends SNA through the *old* PCB, and spins with no timeout until it is acknowledged [EOSPM] (sure).

**DCBs** start right after the PCB (0xFEC4). Each is 21 bytes, up to 15 of them [TRM 3.3.8, TRM-PDCB, EOSPM] (sure):

| Off | Field |
|---|---|
| 0 | D_COM_STAT: command (Z80) / status (6801) |
| 1–2 | Buffer address (lo, hi), must be in intrinsic RAM |
| 3–4 | Buffer length (keyboard = 1, printer = 16, block = 1024) |
| 5–8 | Block number, 32-bit little-endian |
| 9 | Secondary device ID (tape 2 uses the tape-1 DCB with 0x18 here per EOSPM) |
| 10–13 | Reserved |
| 14–15 | Retry count |
| 16 | Device (node) address 1–15 |
| 17–18 | Max message length (from the node's STATUS reply) |
| 19 | Device type: 0 = character, 1 = block |
| 20 | Device-dependent status. Nibbles: high = alternate drive, low = main. Holds the media-present flag. |

- **DCB commands:** 0 IDLE, 1 STATUS, 2 RESET (soft reset node), 3 WRITE, 4 READ.
- **Completion:** the 6801 writes a value ≥ 0x80 into byte 0.
  - 0x80 = OK
  - 0x80 + error otherwise: 0x86 = printer busy (PR_NAK), 0x8C = no key ready (KBD_NAK), 0x9B = timeout
  - EOS error codes = status & 0x7F: 1 DCB not found … 18/19 sync errors, 21 media status, 22 device error, 27 timeout [EOSPM p.92]
- The master scans the PCB/DCBs by DMA and writes results and data back [TRM 3.3.8–3.9]. There is no queue; reads and writes are asynchronous [BAT] (sure).
- **Quirk:** EOS READ_BLOCK issues *two* READ_1_BLOCK requests. Data appears in Z80 RAM only after the second; WRITE needs one [TWWMCA 9611.18] (sure as an observation; cause **UNKNOWN**).
- "SYNCHRONIZE CLOCKS" (PCB SYNC1/SYNC2) must follow every net reset and cancels all active DCBs [EOSPM p.10]. What the master does internally is **UNKNOWN**.
- How often the master scans the DCBs is **UNKNOWN**.

### 2.3 Network reset and other control lines

- Port 0x3F bit 0 pulses the MIOC **NET RST** output, which drives the AdamNet reset line through comparator U19 (TRM: "the comparator also can reset all the devices on AdamNet via MIOC control") [TRM 2.1.7, SCH-MIO] (sure).
- Reset low time seen on the bus is about 2.5 ms; the first STATUS poll follows about 160 ms later [ANET, measured] (likely).
- Whether NET RST also resets the master 6801 is **UNKNOWN**. CVRST does reset it [TRM].
- The master has no documented way to reset or interrupt the Z80 (likely).

---

## 3. AdamNet bus protocol

### 3.1 Physical layer

| Fact | Value | Src | Conf |
|---|---|---|---|
| Topology | Shared 4-wire bus: Data, Reset, GND, +5V. Single master, logically token-like. Every peripheral 6801 has a quad comparator interface. | TRM 3.3.1, 2.1.7 | sure |
| Data line | Half-duplex, one wire for RX and TX. Open-collector "wired" bus. | TRM, BAT | sure |
| Polarity | Inverted relative to TTL serial: logic 0 is driven high (+5V), logic 1 is low (ground) on the wire | ANET | likely |
| Rate | **62.5 kbit/s**, 16 µs bit cell, 8N1 (start + 8 data + stop) = **160 µs per byte** | TRM 3.3.7 | sure |
| Connectors | RJ12 6P6C with crossed cables. The printer cable also carries AdamNet (pin 6 blue) and Reset (pin 7 violet). The +5V supply is limited to about 40 mA for external devices. | ANET, TRM 2.6.3 | likely |
| Framing trick | Nodes not addressed set the 6801 SCI **wake-up (RWU)** bit and ignore traffic until the line idles for 10 bit times | ANET, BAT, DS6801 | likely |
| Packet end | A gap of one byte time (about 160 µs) with no traffic | ANET | likely |

### 3.2 Message format

The first byte holds **type in the high nibble and node address in the low nibble** (1–15; 0 = master) [TRM 3.3.3] (sure):

| Type | TRM name (dir) | Payload |
|---|---|---|
| 0 | COMMAND.CONTROL RESET (M→N) | – |
| 1 | COMMAND.CONTROL STATUS | – |
| 2 | COMMAND.CONTROL ACK | – |
| 3 | COMMAND.CONTROL CLR (clear to send) | – |
| 4 | COMMAND.CONTROL RECEIVE | – |
| 5 | COMMAND.CONTROL CANCEL | – (unused in firmware per ANET) |
| 6 | COMMAND.DATA SEND (M→N) | len_hi, len_lo, data…, cs |
| 7 | COMMAND.CONTROL NACK | – |
| 8 | RESPONSE.CONTROL STATUS (N→M) | maxlen ×2, type, status, cs (see below) |
| 9 | RESPONSE.CONTROL ACK | – |
| A | RESPONSE.CONTROL CANCEL | – (unused) |
| B | RESPONSE.DATA SEND (N→M) | len_hi, len_lo, data…, cs |
| C | RESPONSE.CONTROL NACK | – |
| D | READY (M→N), "are you ready?" | – (ANET; not in the TRM; added for slow devices like the DDP) |

- **Checksum:** XOR of the payload data bytes only. Header and length bytes are excluded for SEND packets. For STATUS replies it covers bytes 1–4 [TRM 3.3.4/3.3i, ANET] (sure).
- **SEND length byte order:** big-endian (hi, lo), e.g. `64 00 05 …`, `B1 00 01 0D 0D`, `B4 04 00 …` [ANET examples] (likely).
- **STATUS reply byte order:** the TRM draws the max-length field as "l(HI), l(LO)", but every captured example is **lo, hi**:
  - `81 01 00 00 00 01`: keyboard, max 1
  - `82 10 00 00 00 10`: printer, max 16
  - `84 00 04 01 40 45`: disk, max 1024, block device, status 0x40 = OK
  - Use **lo, hi** [ANET] (likely). Byte 3: 0 = character, 1 = block. Byte 4: device-dependent status. Bit 4 of byte 3 is "r" (reserved) in the TRM.
- Block devices receive a 5-byte SEND: 4-byte block number (little-endian) + 1 reserved byte. Example: `64 00 05 00 00 00 00 00 94` [ANET] (likely).

### 3.3 Transactions (from ANET bus traces) (likely)

- **Status / roll call:** `M:1n`, then `N:8n …` within about 80–200 µs, then `M:2n` (ACK). If no reply arrives within about 4 ms, the master moves to the next node.
- **Character read (keyboard):**
  - No key: `M:41`, `N:C1` (NACK).
  - Key: `M:41`, `N:91`, `M:31` (CLR), `N:B1 00 01 kk kk`, `M:21`.
- **Block read:** `M:Dn`, `N:9n`; `M:6n 00 05 <blk32 LE> 00 cs`, `N:9n`; `M:4n`, `N:9n` (must come within about 2 ms or RECEIVE is resent); `M:3n`, `N:Bn 04 00 <1024 bytes> cs`; `M:2n`.
- **Block write:** `M:Dn`, `N:9n`; `M:6n` (block number), `N:9n`; `M:6n 04 00 <1024> cs`, `N:9n`.
- **Timing:** node ACKs should arrive within about 400 µs of the end of the command; replying too early collides with the master's own stop bit.
- The master retries with READY, RECEIVE or STATUS until the DCB retry count runs out. Exact retry intervals and timeouts are **UNKNOWN**; the traces show a few ms between retries.

### 3.4 Node IDs

The EOS device numbers are the AdamNet node addresses [TRM 3.4.1.3 (FCB_DEVICE), EOSPM p.94] (sure):

| ID | Device | ID | Device |
|---|---|---|---|
| 0 | Master | 8 | Data pack drive 1 |
| 1 | Keyboard | 0x18 | Data pack drive 2: node 8, secondary ID (DCB+9) |
| 2 | Printer | 9–0C | Reserved (EOSPM: 9 = tape 3, 0x0B = modem, 0x0C = hi-res monitor) |
| 3 | Reserved ("Copywriter" in EOSPM) | 0D | Parallel interface |
| 4 / 5 | Floppy 1 / floppy 2 (6, 7 = disks 3, 4 in EOSPM) | 0E | RS-232 interface |
|  |  | 0F | Gateway (FujiNet uses 0x0F) |

---

## 4. Peripheral 6801s

All peripheral 6801s run at E = 1 MHz from a 4 MHz crystal and talk AdamNet through a quad comparator [TRM 2.1.2].

### 4.1 Keyboard

- 6801 on the keyboard PCB [TRM 2.3.1]. The 2K ROM dump is `roms/adam_kb/keyboard.u2`.
- The node responds to Reset (clears the buffer and unlocks shift-lock) and Send-character (one byte per transaction) [TRM 2.3.1] (sure).
- Scans the matrix every 5 or 8 ms. Columns are 6801 outputs and rows are 6801 inputs. A key is accepted when two consecutive scans match [TRM 2.3.1] (sure).
- Type-ahead buffer of about 31 characters [Jameco KB75 sheet, OCR poor] (guess). Max message length = 1.
- **Matrix** [KBM] (likely). The ribbon has 21 pins: pins 1–8 are columns and pins 9–21 are rows.

| Row pin | Col 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| 9 | WILD CARD | I | II | III | IV | V | VI | UNDO |
| 10 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| 11 | TAB | Q | W | E | R | T | Y | U |
| 12 | A | S | D | F | G | H | J | K |
| 13 | Z | X | C | V | B | N | M | , < |
| 14 | 9 | 0 | – | + | ^ | L | ; | ' " |
| 15 | I | O | P | [ { | ] } | . > | / ? | RETURN |
| 16 | \ \| | ESC/WP | (n/c) | (n/c) | SPACE | MOVE/COPY | STORE/GET | CLEAR |
| 17 | ↑ | → | ↓ | ← | BACKSPACE | INSERT | PRINT | DELETE |
| 18–21 | CONTROL, SHIFT, LOCK, HOME (on column 1) | | | | | | | |

- Which 6801 port bit drives each column or row: **UNKNOWN**.
- **Key codes** [KEYS] (sure):
  - ASCII 0x00–0x7F, with CTRL/SHIFT/LOCK applied by the keyboard
  - 0x80 HOME
  - 0x81–0x86 smart keys I–VI; 0x89–0x8E shift+I–VI
  - 0x90–0x97: WILD CARD, UNDO, MOVE, STORE, INSERT, PRINT, CLEAR, DELETE; 0x98–0x9F the shifted versions (COPY = 0x9A, GET = 0x9B)
  - 0xA0–0xA3 arrows ↑ → ↓ ←; 0xA4–0xA7 CTRL+arrow; 0xA8–0xAB diagonals; 0xAC–0xAF HOME+arrow
  - 0xB8 shift-BACKSPACE, 0xB9 shift-TAB (back-tab)
  - 0xC0–0xEF unused; 0xF0–0xFF internal
  - Keys that repeat: printable characters, BS and arrows

### 4.2 Digital Data Pack (tape) drive

- The **tape 6801 sits on the Memory & I/O board** (U24, 6801/68701, 4 MHz crystal Y2), not in the drive. It serves both drives as one node (0x08) [TRM 2.2.3/2.3.1, SCH-MIO sheet 2, EOSPM] (sure).
- External RAM: two 1K×4 chips (2114). The TRM text says "ROM" but the schematic shows 2114 SRAM, giving a **1 KB block buffer**, with a '373 address latch [TRM 2.3.1, SCH-MIO sheet 2] (likely).
- This implies expanded multiplexed mode (guess). ROM dump: `roms/adam_ddp/tape rev a 8865.u24` (2K).
- **Drive interface signals** (J10/J11 for drive 0, J12/J13 for drive 1) [TRM 2.3.5] (sure):

| Signal | Dir | Meaning |
|---|---|---|
| BRAKE | →drive | 1 = brake |
| GO REV / GO FWD | →drive | Active low |
| STOP | →drive | 1 = stop and force the data output to 1 |
| SPEED SELECT | →drive | 0 = 20 ips, 1 = 80 ips |
| MSENSE | ←drive | 1 = tape moving properly |
| CIP | ←drive | 0 = cassette in place |
| DATA IN | →drive | Write data, bi-phase mark |
| DATA OUT | ←drive | Read data, bi-phase mark; jitter ≤ 4%, peak shift ≤ 5% |
| TRACK A/B | →drive | 1 = track A, 0 = track B |
| WRENABLE | →drive | 0 = write, 1 = read |

- **Tape 6801 port use** from a barely legible scan [SCH-MIO sheet 2] (guess):
  - P10 = SPD SEL, P11 = STOP, P13 = GO FWD, P14 = GO REV, P15 = BRAKE
  - P2x pin 10 = TRACK A/B, another P2x pin = WR DATA
  - Separate MSENSE 0/1 and RD DATA 0/1 lines per drive
  - Verify against the tape ROM's I/O access patterns.
- **Medium** [TRM 2.3.4, 3.5] (sure):
  - Tape is 300 ft × 0.15 in, two tracks.
  - Bit cell **70 µs**, bi-phase mark: a flux change inside the cell means 1, none means 0, with a transition at every cell boundary.
  - 20 ips normal, 80 ips search/rewind. Effective transfer rate about 1.4 KB/s.
  - 256 KB = 2 tracks × 128 blocks × 1 KB. The tachometer is an optical encoder wheel.
- **Block layout on tape** [TRM 3.5] (sure). The arrow is tape motion.
  - **GW** format (no directory; Buck Rogers super game): track 0 holds blocks 0x00…0x7F, track 1 holds 0x80…0xFF. "Block 0 at the end" (TRM) — BAT says "beginning", so this is ambiguous.
  - **HE** format (blank packs, SmartBASIC): track 0 reads 0x40…0x7F, 0x00…0x3F, so block 0 is mid-tape. Track 1 reads 0x80…0xBF, 0xC0…0xFF.
- **Timing** [TRM Ch.5 §1.2] (sure):
  - About 1 s per block read without repositioning.
  - A retry costs about 1.2–1.4 s, and there are at most 3 attempts per block (checksum failure).
  - Repositioning takes about 1 s per 80 inches of travel.
  - Rewinding from block 15 to track 1 block 128 takes about 5 s.
- **On-tape block format** (sync/preamble, header, block-number field, checksum, inter-block gap, byte bit-order): **UNKNOWN** in prose documentation.
- **.DDP image:** 262,144 bytes = 256 × 1K blocks, presumably in logical block order 0…255 with no header [BAT `dd … count=262144`, FujiNet uses it as a block device] (likely). Physical placement follows the GW/HE layout above.

### 4.3 Printer (brief)

- 6801 on the printer board. It drives the daisy-wheel motor, carriage motor, platen advance, print solenoid and ribbon solenoid, using parallel drivers [TRM 2.4] (sure).
- 16-character buffer, so max message length = 16. Responds to CR, LF, BS, ESC, SO (reverse direction) and SI. 10 cps, 10 pitch [TRM 2.4] (sure).
- Returns NACK/busy as DCB status 0x86. ROM dump: `adam_prn/printer.u2`.
- Motor and solenoid port mapping: **UNKNOWN**. For a first emulator, HLE the printer as an AdamNet character sink.
- The printer houses the power supply (+5, −5, +12 logic, +12 inductive, 18V unregulated) [TRM 2.6].

### 4.4 Floppy drive (Coleco 5¼", node 4 or 5)

**Controller board** [SCH-FDC, Coleco dwg 300012 rev A2, 1984] (likely, clear scan):
- U6 = 6801 (or 6803) with a 4 MHz crystal, E = 1 MHz. Bus is **expanded multiplexed**:
  - P30–P37 = A0–A7/D0–D7, latched by U7 LS373 on SC1 (AS)
  - P40–P43 = A8–A11
  - P44, P47 and logic (LS04, LS32, LS74, 74121 one-shot) form the chip selects and the 2793 /WE timing
- Memory chips:
  - **U10 2732: 4K ROM.** Dumps in `roms/adam_fdc/*.u10` (Coleco "ad 31 rev a 09-27-84" plus third-party 320K, 720K and 1.44M variants).
  - **U8, U9 2114: 1 KB RAM**, the sector buffer.
- **U11 WD2793:**
  - D0–D7 on P30–P37
  - CA0 ← P45, CA1 ← P46 (register select)
  - DDEN ← P13 (double density)
  - DRQ → P12; INTRQ → port/NMI logic (U14 inverter to /NMI) (guess)
  - ENP ← P11; /RE and /WE from decode; CLK from E (1 MHz, correct for 5¼")
  - TEST/HLT/RDY strapped
  - Internal data separator (RPW, VCO, PUMP)
  - Write precomp is adjusted to 188 ns. VCO adjusted to 250 kHz ± 650 Hz.
- Drive connector P5: MOTOR ON (16), STEP (20), DIRECTION (18), WRITE GATE (24), WRITE DATA (22), READ DATA (30), TRACK 00 (26), INDEX (8), WRITE PROTECT (28), DRIVE SELECT (10).
- SW1/jumper on P2x selects **drive 1 or 2, i.e. node 4 or 5**.
- AdamNet: P23 RxD, P24 TxD. Two RJ jacks (P1, P2) for daisy-chaining.
- Exact address map (ROM at F000? RAM at 0x0400?, 2793 at ?): **UNKNOWN**. The decode cannot be read reliably from the scan. Derive it from the vectors and accesses in the 2732 ROM.

**Media** (sure unless noted):
- 160 KB, **single-sided, double density (MFM)** [TRM Sect. III "Disk drive"].
- **40 tracks × 8 sectors × 512 bytes** [Wikipedia floppy-format table; ADAMEM-DOC table] (likely).
- EOS blocks 0x00–0x9F (160 × 1K), so one block = 2 sectors [TRM Sect. III] (sure).
- Formatted only by the Coleco Disk Manager software. The DDP cannot be user-formatted [TRM Sect. III].
- Software interleave: **5** for 160K/320K and **4** for 640K/720K/1.44M [ADAMEM-DOC prose table] (likely). The exact block→(track, sector) formula is **UNKNOWN**.
- **.DSK image:** a raw dump read by a PC FDC (`cp /dev/fd1 x.dsk`, `DCOPY /S:8 /H:1 /T:40`). Ordered by track, then physical sector 1..8, 512 bytes each, 163,840 bytes, no header [ADAMEM-DOC] (likely).
  - The ADAM's logical interleave therefore applies when mapping blocks to image offsets.
  - Other sizes: 320K (2 heads × 40 × 8), 640K (2 × 80 × 8), 720K (2 × 80 × 9), 1.44M (2 × 80 × 18).

**EOS directory** (common to tape and disk) [TRM 3.4.1.2, EOSPM] (sure):
- Block 0 = boot (cold-start loader). Block 1… = directory, 26-byte entries.
- Volume record:
  - Name: 12 bytes
  - Dir size: 1 byte, where bit 7 = delete-protect and bits 6–0 = directory blocks
  - **DIR CHECK = 55 AA 00 FF** at bytes 13–16
  - Volume size: 4 bytes; reserved: 2 bytes; date: 3 bytes
- File record:
  - Name: 12 bytes; the 12th byte is the type (A/a/H/h), terminated by 0x03
  - Attribute: 1 byte. Bits: p w r u s a e f = permanent, write-protected, read-protected, user, system, deleted, exec-protect, not-a-file
  - Start block: 4 bytes; allocated blocks: 2; used blocks: 2; bytes in last block: 2 (0–0x400); date: 3
- Up to 127 directory blocks, 39 entries per block. Files are contiguous. "BLOCKS LEFT" is a pseudo-entry.

---

## 5. Cartridge and expansion ROM formats

### 5.1 ColecoVision cartridge (upper 32K = cartridge, lower = OS-7)

- Cartridge connector J1 pins [TRM 2.1.9e]:
  - D0–D7, A0–A14
  - /CS1–/CS4 = LS138 8K chip selects, for 8000/A000/C000/E000 (likely)
  - +5V at 0.2 A
- **Header at 0x8000** [TRM-8000, ROM-DIS os7] (sure):

| Addr | Content |
|---|---|
| 8000–8001 | Signature. OS-7 checks for **`55 AA`** and, if present, immediately does `JP (800Ah)` (skips the title screen) [ROM-DIS os7 0x006E]. **`AA 55`** = normal cartridge with the COLECOVISION title screen [community knowledge; TRM-8000 comment says "55H,0AAH: cartridge present, ColecoVision logo"] (likely). |
| 8002 | Pointer to sprite name table |
| 8004 | Pointer to sprite order table |
| 8006 | Pointer to work buffer |
| 8008 | Pointer to controller-map area |
| 800A | Start address of the game |
| 800C/800F/8012/8015/8018/801B | `JP` targets for RST 08h/10h/18h/20h/28h/30h. The os7 ROM has `JP 800Ch` at 0x0008. |
| 801E | Maskable interrupt (RST 38h / spinner) vector |
| 8021 | NMI (VDP) vector. OS-7 0x0066 = `JP 8021h`. |
| 8024… | Game-name string (`/`-separated, 0x1D = ©, 0x1E/0x1F = ™). ADAM super games put an OCT pointer and a device-ID byte after the date. |

OS-7 reset: `LD SP,73B9h` / `JP 006Eh` [ROM-DIS] (sure).

### 5.2 ADAM expansion ROM (connector #2, J6; upper option 01)

- **Signature `66h, 99h` at 0x8000–0x8001**. EOS_BOOT enters at **0x8002** with HL = 0x009A as a return address [ROM-DIS wp_r80 0x0086–0x0097] (sure). An AtariAge forum report also says SmartWriter "looks for 9966h" (consistent).
- The check runs with map 0x04 (SmartWriter + XROM) before EOS is initialised. A valid XROM therefore pre-empts EOS, disk and tape boot [TRM 3.2.2, 3.2.3] (sure).

### 5.3 Boot media

- Block 0 → 0xC800, `JP C800h`, B = device ID (see 1.5).
- The search order is disk 1, disk 2, tape 1, tape 2. If none is found, EOS falls back to SmartWriter [TRM 3.2.3, EOSPM] (sure).
- "Valid data" means the device reports media present in its DCB status (EOSPM: "the first to contain media is presumed to be the boot media"). Whether block 0 content is validated: **UNKNOWN**; probably not (guess).

---

## 6. Video, sound and expansion (brief)

| Item | Fact | Src | Conf |
|---|---|---|---|
| VDP | TI TMS9928A, 16K VRAM (8 × 16K×1) | TRM 2.2.3 | sure |
| VDP / NMI note | The VDP auto-increment address is corrupted if an NMI handler touches the VDP mid-access. Software defers this; hardware does nothing special. | TRM App.3.1 | sure |
| Sound | SN76489A ("76489/6496"), write-only at 0xFF (decoded across E0–FF), clocked at 3.58 MHz. Hardware wait while the chip latches. Audio READY appears on expansion pin 56. | TRM 2.2.5, 2.1.8 | sure |
| Expansion port (side) | 60-pin edge with buffered Z80 bus, /INT, /NMI, /BUSRQ, /BUSAK, /WAIT, /HALT, SPINNER INT DISABLE, GAME MODE RESET, 76489 disable, video and audio in | TRM 2.1.8 | sure |
| Slots | #1 (J7): I/O only, BA0–7, for the modem. #2 (J6): ROM and I/O, BA0–15. #3 (J5): RAM/ROM up to 64K, with RA7 in place of BA7. | TRM 2.1.7 | sure |
| Expansion Module #3 | The ColecoVision supplies the CPU board. Address bus buffered (ADDRBUFEN), data bus buffered (245EN). No composite video out. | TRM 2.2.4 (2.4) | sure |

---

## 7. Practical emulation notes

These follow from the sections above (guess, unless the section they cite says otherwise).

1. **Power-on:** reset with port 7F = 0x00 and port 3F = 0, then run SmartWriter from 0x0000. EOS_BOOT does the rest.
   - Make 0x6000–0x7FFF return the first 8K of the EOS chip when 3F bit 1 is set.
   - **Game reset:** 7F = 0x0F, run from 0x0000 in OS-7.
2. **Master 6801:** run the real 2K master ROM single-chip. Give it a port-1/3/4 "bus window" into the 64K intrinsic DRAM:
   - Address from P1/P4.
   - A P3 read or write strobed by IS3/OS3 is a Z80 memory access.
   - Stall the Z80 (BUSRQ) for the access. The cost per byte is UNKNOWN; start with about 1 Z80 M-cycle per byte.
   - Find the DMA-request bit by watching which P2 bit toggles around P3 accesses in the master ROM.
3. **AdamNet:** a shared byte-serial bus at 160 µs per byte between the SCIs of the master, keyboard, tape and disk 6801s.
   - Implement the 6801 SCI wake-up (RWU) bit. Collisions cause framing errors.
4. HLE fallbacks per node (keyboard, printer, disk, tape) are easy to build from section 3 if LLE of a peripheral is not ready.
