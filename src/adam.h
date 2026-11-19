/* adam.h - the Coleco ADAM machine: Z80 side, AdamNet master and peripherals. */
#ifndef ADAM_H
#define ADAM_H
#include <stdint.h>
#include "z80.h"
#include "m6801.h"
#include "tms9918.h"
#include "sn76489.h"
#include "adamnet.h"
#include "ddp.h"
#include "wd2793.h"
#include "printer.h"

#define Z80_HZ        3579545.0
#define PS_PER_Z80    279365ull        /* 1e12 / 3579545, rounded */
#define PS_PER_E      1000000ull       /* 6801 E clock = 1 MHz */
#define AUDIO_RATE    44100

/* keyboard matrix: 13 columns (P3 bits 0-7, P4 bits 0-4) x 8 rows (P1) */
#define KBD_KEYS      (13 * 8)
enum { KEY_CONTROL = 0x48, KEY_SHIFT = 0x50, KEY_LOCK = 0x58, KEY_HOME = 0x60 };

typedef struct Adam Adam;

typedef struct Dev6801s {
    M6801 cpu;
    NetNode *node;
    const uint8_t *rom; uint16_t rom_base;
    Adam *adam;
    int present;
    int in_reset;
    int unit;
    void (*tick)(struct Dev6801s *d);   /* called after each instruction (peripheral timing) */
} Dev6801;

struct Adam {
    Z80 z80;
    TMS9918 vdp;
    SN76489 psg;

    uint8_t ram[65536];           /* intrinsic RAM */
    uint8_t *xram;                /* memory expander: xram_banks x 64K (NULL = none) */
    int has_xram, xram_banks;
    uint8_t xbank;                /* port 0x42: expander bank (expanders larger than 64K) */
    uint8_t os7[0x2000];
    uint8_t wp[0x8000];           /* SmartWriter ROMs, lower 32K */
    int wp_size;
    uint8_t eos[0x4000];
    uint8_t cart[0x8000]; int has_cart;
    uint8_t xrom[0x8000]; int has_xrom;

    uint8_t memmap;               /* port 0x7F */
    uint8_t port3f;               /* net reset / EOS enable */
    uint8_t ctrl_mode;            /* 0 = joystick, 1 = keypad */
    uint8_t joy[2];               /* bits: 0 up 1 right 2 down 3 left 4 fire L 5 fire R (active high) */
    int8_t keypad[2];             /* -1 none, 0..9, 10 '*', 11 '#' */

    AdamNet net;
    Dev6801 master, kbd, tape;
    DDP ddp;
    Dev6801 prn;                  /* SmartWriter printer (node 2) */
    Printer printer;
    Dev6801 fdc[2];               /* disk drives 1 and 2 (AdamNet nodes 4 and 5) */
    WD2793 wd[2];
    Floppy floppy[2];
    uint8_t fdc_ram[2][1024];
    uint8_t fdc_rom[2][0x1000];   /* disk controller ROM of each drive */
    int fdc_ready_high[2], fdc_hd_select[2];   /* board variants, see adam_set_fdc_rom */
    uint8_t keys[KBD_KEYS];       /* 1 = pressed */

    uint64_t z80_ps;              /* emulated time of the Z80 */
    uint64_t now_ps;              /* end of the last slice */
    int z80_stall;                /* cycles stolen by AdamNet DMA */
    int busrq;                    /* master holds the Z80 bus (P2.0 low) */
    uint64_t busrq_start;         /* master E cycle when it took the bus */
    int line_cycles;
    uint8_t last_vdp_int;
    uint64_t frames;

    int trace_z80;
    int trace_eos;                /* log calls into the EOS jump table (FC30-FD5F) */
    int watch;                    /* RAM address whose writes are logged (-1 = off) */

    /* audio: PSG output rendered in step with the writes */
    int16_t audio[2048];
    int audio_n;
    uint64_t frame_start_ps;
};

int  adam_load_roms(Adam *a, const char *romdir);
void adam_init(Adam *a);
void adam_power_on(Adam *a);              /* power on / computer reset */
void adam_game_reset(Adam *a);             /* cartridge reset: OS-7 + cartridge */
int  adam_load_cart(Adam *a, const char *path);
void adam_attach_disk_drive(Adam *a, int unit);   /* before adam_power_on */
int  adam_insert_disk(Adam *a, int unit, const char *path);
int  adam_set_fdc_rom(Adam *a, int unit, const char *path);   /* after adam_load_roms */
int  adam_set_xram(Adam *a, int kbytes);   /* memory expander in slot 3: 64K, or n x 64K banks */
void adam_run_frame(Adam *a);           /* one video frame (262 lines) */
void adam_key(Adam *a, int index, int pressed);
int  adam_key_for_code(Adam *a, uint8_t code, int *need_shift, int *need_ctrl);
void adam_shutdown(Adam *a);              /* write back modified media */

#endif
