/* adam.c - Coleco ADAM machine glue. See adam.h. */
#include "adam.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------- ROM loading */
static int load_file(const char *dir, const char *name, uint8_t *dst, size_t size, size_t *got)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(dst, 1, size, f);
    fclose(f);
    if (got) *got = n;
    return n > 0;
}

static uint8_t master_rom[0x800], kbd_rom[0x800], tape_rom[0x800], prn_rom[0x800];

int adam_load_roms(Adam *a, const char *dir)
{
    char sub[1024];
    int ok = 1;
    snprintf(sub, sizeof sub, "%s/adam", dir);
    memset(a->wp, 0xFF, sizeof a->wp);
    memset(a->eos, 0xFF, sizeof a->eos);
    /* OS-7 is the ColecoVision BIOS, same 8K */
    if (!load_file(sub, "os7.u2", a->os7, sizeof a->os7, NULL)) {
        fprintf(stderr, "adam: missing %s/os7.u2\n", sub);
        return 0;
    }
    ok &= load_file(sub, "alf @1 rev 57 e3d5.u8", a->wp, 0x2000, NULL);
    ok &= load_file(sub, "alf @2 rev 57 ae6a.u20", a->wp + 0x2000, 0x2000, NULL);
    ok &= load_file(sub, "alf @3 rev 57 8534.u21", a->wp + 0x4000, 0x2000, NULL);
    a->wp_size = 0x6000;
    ok &= load_file(sub, "eos 6 rev 57 08dd.u22", a->eos, sizeof a->eos, NULL);
    ok &= load_file(sub, "master rev a 174b.u6", master_rom, sizeof master_rom, NULL);
    snprintf(sub, sizeof sub, "%s/adam_kb", dir);
    ok &= load_file(sub, "keyboard.u2", kbd_rom, sizeof kbd_rom, NULL);
    snprintf(sub, sizeof sub, "%s/adam_ddp", dir);
    ok &= load_file(sub, "tape rev a 8865.u24", tape_rom, sizeof tape_rom, NULL);
    snprintf(sub, sizeof sub, "%s/adam_prn", dir);
    ok &= load_file(sub, "printer.u2", prn_rom, sizeof prn_rom, NULL);
    snprintf(sub, sizeof sub, "%s/adam_fdc", dir);
    ok &= load_file(sub, "adam disk u10 ad 31 rev a 09-27-84.u10", a->fdc_rom[0], sizeof a->fdc_rom[0], NULL);
    memcpy(a->fdc_rom[1], a->fdc_rom[0], sizeof a->fdc_rom[1]);
    if (!ok && !a->coleco) fprintf(stderr, "adam: missing ROM files under %s\n", dir);
    return ok ? 1 : -1;
}

/* ------------------------------------------------------------- Z80 bus */
#define XRAM(a) ((a)->xram + (size_t)((a)->xbank % (a)->xram_banks) * 65536u)

int adam_set_xram(Adam *a, int kbytes)
{
    int banks = kbytes / 64;
    if (banks < 1 || banks > 256 || kbytes % 64) return 0;
    free(a->xram);
    a->xram = calloc((size_t)banks, 65536);
    if (!a->xram) return 0;
    a->xram_banks = banks; a->has_xram = 1; a->xbank = 0;
    return 1;
}
/* cartridge port 0x8000-0xFFFF. MegaCart (more than 32K): the last 16K bank is fixed at
 * 0x8000, 0xC000-0xFFFF shows the selected bank, and reading 0xFFC0+N selects bank N
 * (the read itself returns the byte of the current bank) */
static uint8_t cart_read(Adam *a, uint16_t addr)
{
    if (!a->has_cart) return 0xFF;
    if (a->cart_banks <= 2) return a->cart[addr - 0x8000];
    if (addr < 0xC000) return a->cart[(size_t)(a->cart_banks - 1) * 0x4000 + (addr - 0x8000)];
    uint8_t v = a->cart[(size_t)a->cart_bank * 0x4000 + (addr - 0xC000)];
    if (addr >= 0xFFC0) a->cart_bank = (uint8_t)((addr & 0x3F) % a->cart_banks);
    return v;
}

/* ColecoVision memory map (+ Super Game Module) */
static uint8_t cv_read(Adam *a, uint16_t addr)
{
    if (addr < 0x2000) return (a->sgm && !(a->sgm_7f & 2)) ? a->sgm_ram[addr] : a->os7[addr];
    if (addr < 0x8000) {
        if (a->sgm && (a->sgm_53 & 1)) return a->sgm_ram[addr];
        return addr >= 0x6000 ? a->cv_ram[addr & 0x3FF] : 0xFF;
    }
    return cart_read(a, addr);
}
static void cv_write(Adam *a, uint16_t addr, uint8_t v)
{
    if (addr < 0x2000) { if (a->sgm && !(a->sgm_7f & 2)) a->sgm_ram[addr] = v; return; }
    if (addr < 0x8000) {
        if (a->sgm && (a->sgm_53 & 1)) a->sgm_ram[addr] = v;
        else if (addr >= 0x6000) a->cv_ram[addr & 0x3FF] = v;
    }
}

static uint8_t z_read(void *ctx, uint16_t addr)
{
    Adam *a = ctx;
    if (a->coleco) return cv_read(a, addr);
    if (addr < 0x8000) {
        switch (a->memmap & 3) {
        case 0:
            /* 0x6000-0x7FFF: the 16K u22 chip; EOS ENABLE (port 3F bit 1) picks its half:
             * lower half = EOS, upper half = the last 8K of SmartWriter */
            if (addr >= 0x6000) return a->eos[(a->port3f & 2 ? 0 : 0x2000) + addr - 0x6000];
            return addr < a->wp_size ? a->wp[addr] : 0xFF;
        case 1: return a->ram[addr];
        case 2: return a->has_xram ? XRAM(a)[addr] : 0xFF;
        default: return addr < 0x2000 ? a->os7[addr] : a->ram[addr];
        }
    }
    switch ((a->memmap >> 2) & 3) {
    case 0: return a->ram[addr];
    case 1: return a->has_xrom ? a->xrom[addr - 0x8000] : 0xFF;
    case 2: return a->has_xram ? XRAM(a)[addr] : 0xFF;
    default: return cart_read(a, addr);
    }
}

static void z_write(void *ctx, uint16_t addr, uint8_t v)
{
    Adam *a = ctx;
    /* idle detection: only a write that changes memory is work (a wait loop that calls a
     * function pushes the same return address again and again); 0xFFC0+ is never RAM on a
     * cartridge and reading it would switch MegaCart banks */
    if (a->prof.enabled) {
        int cart_up = a->coleco || ((a->memmap >> 2) & 3) == 3;
        if (addr >= 0xFFC0 && cart_up) { }
        else if (z_read(a, addr) != v) vdpprof_write(&a->prof);
    }
    if (a->watch == addr)
        fprintf(stderr, "[watch %10.4f s] Z80 %04X writes %02X (was %02X)\n",
                (double)a->z80_ps / 1e12, a->z80.pc.w, v, a->ram[addr]);
    if (a->coleco) { cv_write(a, addr, v); return; }
    if (addr < 0x8000) {
        switch (a->memmap & 3) {
        case 1: a->ram[addr] = v; break;
        case 2: if (a->has_xram) XRAM(a)[addr] = v; break;
        case 3: if (addr >= 0x2000) a->ram[addr] = v; break;
        default: break;
        }
        return;
    }
    switch ((a->memmap >> 2) & 3) {
    case 0: a->ram[addr] = v; break;
    case 2: if (a->has_xram) XRAM(a)[addr] = v; break;
    default: break;
    }
}

static const uint8_t keypad_code[12] = { 0xA, 0xD, 0x7, 0xC, 0x2, 0x3, 0xE, 0x5, 0x1, 0xB, 0x9, 0x6 };

static uint8_t read_controller(Adam *a, int n)
{
    uint8_t v = 0xFF;
    if (a->ctrl_mode == 0) {
        uint8_t j = a->joy[n];
        if (j & 1) v &= (uint8_t)~0x01;
        if (j & 2) v &= (uint8_t)~0x02;
        if (j & 4) v &= (uint8_t)~0x04;
        if (j & 8) v &= (uint8_t)~0x08;
        if (j & 0x10) v &= (uint8_t)~0x40;
    } else {
        if (a->keypad[n] >= 0) v = (uint8_t)((v & 0xF0) | keypad_code[a->keypad[n]]);
        if (a->joy[n] & 0x20) v &= (uint8_t)~0x40;
    }
    return v;
}

static void net_reset_devices(Adam *a, int assert);

/* render PSG samples up to the Z80's current time within the frame */
static void audio_sync(Adam *a)
{
    uint64_t dt = a->z80_ps > a->frame_start_ps ? a->z80_ps - a->frame_start_ps : 0;
    int target = (int)(dt * AUDIO_RATE / 1000000000000ull);
    if (target > (int)(sizeof a->audio / sizeof a->audio[0])) target = (int)(sizeof a->audio / sizeof a->audio[0]);
    if (target <= a->audio_n) return;
    int n = target - a->audio_n;
    sn76489_render(&a->psg, a->audio + a->audio_n, n);
    if (a->sgm) {
        ay8910_render(&a->ay, a->ay_audio, n);
        for (int i = 0; i < n; i++) {
            int v = a->audio[a->audio_n + i] + a->ay_audio[i];
            a->audio[a->audio_n + i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
    }
    a->audio_n = target;
}

static uint8_t z_in(void *ctx, uint16_t port)
{
    Adam *a = ctx;
    uint8_t p = (uint8_t)port;
    if (p >= 0xA0 && p <= 0xBF) {
        uint8_t r = tms9918_read(&a->vdp, p & 1);
        /* a status read that takes a set frame flag away from the NMI handler is told apart */
        if (a->prof.enabled) a->vdp_kind = !(p & 1) ? VP_DATA_R : (r & 0x80) ? VP_STATUS_F : VP_STATUS_R;
        return r;
    }
    if (p >= 0xE0) return read_controller(a, (p >> 1) & 1);
    if (p == 0x52 && a->sgm) return ay8910_read(&a->ay);
    return 0xFF;
}

static void z_out(void *ctx, uint16_t port, uint8_t v)
{
    Adam *a = ctx;
    uint8_t p = (uint8_t)port;
    if (a->prof.enabled) vdpprof_write(&a->prof);
    if (p >= 0xA0 && p <= 0xBF) {
        if (a->prof.enabled)
            a->vdp_kind = !(p & 1) ? VP_DATA_W : !a->vdp.latch_full ? VP_CTRL_1ST
                        : (v & 0x80) ? VP_REG_W : (v & 0x40) ? VP_SETUP_W : VP_SETUP_R;
        tms9918_write(&a->vdp, p & 1, v);
        return;
    }
    if (a->sgm && p == 0x50) { ay8910_address(&a->ay, v); return; }
    if (a->sgm && p == 0x51) {
        if (a->trace_ay) fprintf(stderr, "[ay frame %llu] R%d = %02X  PC=%04X\n", (unsigned long long)a->frames, a->ay.addr, v, a->z80.pc.w);
        audio_sync(a); ay8910_write(&a->ay, v);
        return;
    }
    if (a->coleco) {
        /* ColecoVision: nothing decodes 0x00-0x7F except an attached SGM */
        if (a->sgm && p == 0x53) a->sgm_53 = v;
        else if (a->sgm && p == 0x7F) a->sgm_7f = v;
        else if (p >= 0x80 && p <= 0x9F) a->ctrl_mode = 1;
        else if (p >= 0xC0 && p <= 0xDF) a->ctrl_mode = 0;
        else if (p >= 0xE0) { audio_sync(a); sn76489_write(&a->psg, v); }
        return;
    }
    if (p >= 0x20 && p <= 0x3F) {
        uint8_t old = a->port3f;
        a->port3f = v;
        if ((v ^ old) & 1) net_reset_devices(a, v & 1);
    } else if (p == 0x42) {
        a->xbank = v;                           /* expander bank (>64K expanders) */
    } else if (p >= 0x60 && p <= 0x7F) {
        a->memmap = v & 0x0F;
    } else if (p >= 0x80 && p <= 0x9F) {
        a->ctrl_mode = 1;
    } else if (p >= 0xC0 && p <= 0xDF) {
        a->ctrl_mode = 0;
    } else if (p >= 0xE0) {
        audio_sync(a);
        sn76489_write(&a->psg, v);
    }
}

/* ------------------------------------------------------------- 6801 nodes */
static uint8_t dev_read(void *ctx, uint16_t addr)
{
    Dev6801 *d = ctx;
    if (addr >= d->rom_base) return d->rom[addr - d->rom_base];
    return 0xFF;
}
static void dev_write(void *ctx, uint16_t addr, uint8_t v) { (void)ctx; (void)addr; (void)v; }
static void dev_sci_tx(void *ctx, uint8_t b, uint64_t s, uint64_t e) { Dev6801 *d = ctx; adamnet_tx(d->node, b, s, e); }
static void dev_sci_poll(void *ctx) { Dev6801 *d = ctx; adamnet_poll(d->node); }
static uint8_t pins_high(void *ctx, int port) { (void)ctx; (void)port; return 0xFF; }

/* master: bit-banged Z80 bus master. P1 = A15-A8, P4 = A7-A0, P3 = data,
 * P20 low = bus request, P22 = R/W (1 = read); OS3 starts the cycle, IS3 latches read data. */
static void master_os3(void *ctx)
{
    Dev6801 *d = ctx;
    Adam *a = d->adam;
    M6801 *m = &d->cpu;
    if (!(m->ddr[1] & 0x01) || (m->dout[1] & 0x01)) return;  /* no bus request */
    uint16_t addr = (uint16_t)(m->dout[0] << 8 | m->dout[3]);
    if (m->dout[1] & 0x04) m6801_is3(m, a->ram[addr]);
    else {
        if (a->watch == addr)
            fprintf(stderr, "[watch %10.4f s] master %04X writes %02X (was %02X)\n",
                    (double)m->cycles / 1e6, m->pc, m->dout[2], a->ram[addr]);
        a->ram[addr] = m->dout[2];
    }
}

/* BUSRQ = P2.0 driven low. The Z80 is off the bus for as long as the master holds it:
 * the F800/F833 routines assert it with the P2DDR write and release it with the final
 * P1DDR/P2DDR store, 16-20 E cycles per byte, about 60-70 Z80 cycles. */
static void master_port_out(void *ctx, int port, uint8_t data, uint8_t ddr)
{
    Dev6801 *d = ctx;
    Adam *a = d->adam;
    if (port != 2) return;
    int req = (ddr & 1) && !(data & 1);
    if (req && !a->busrq) { a->busrq = 1; a->busrq_start = d->cpu.cycles; }
    else if (!req && a->busrq) {
        a->busrq = 0;
        a->z80_stall += (int)((d->cpu.cycles - a->busrq_start) * PS_PER_E / PS_PER_Z80);
    }
}

static void dev_setup(Adam *a, Dev6801 *d, const uint8_t *rom, uint16_t base, const char *name, int mode)
{
    memset(d, 0, sizeof *d);
    d->adam = a; d->rom = rom; d->rom_base = base; d->present = 1;
    M6801 *m = &d->cpu;
    m->ctx = d;
    m->read = dev_read; m->write = dev_write;
    m->port_in = pins_high;
    m->sci_tx = dev_sci_tx; m->sci_poll = dev_sci_poll;
    m6801_init(m, (uint8_t)mode);
    d->node = adamnet_attach(&a->net, m, name);
}

/* keyboard: columns driven low through P3 (cols 0-7) / P4 (cols 8-12) DDR, rows read on P1 */
static void kbd_lines(Adam *a, uint8_t *rows_low, uint16_t *cols_low)
{
    M6801 *m = &a->kbd.cpu;
    uint16_t driven = 0;
    for (int c = 0; c < 8; c++) if ((m->ddr[2] >> c) & 1 && !((m->dout[2] >> c) & 1)) driven |= (uint16_t)(1 << c);
    for (int c = 0; c < 5; c++) if ((m->ddr[3] >> c) & 1 && !((m->dout[3] >> c) & 1)) driven |= (uint16_t)(1 << (c + 8));
    uint8_t rl = 0;
    for (int c = 0; c < 13; c++)
        if (driven & (1 << c))
            for (int r = 0; r < 8; r++) if (a->keys[c * 8 + r]) rl |= (uint8_t)(1 << r);
    uint16_t cl = driven;
    for (int c = 0; c < 13; c++)
        for (int r = 0; r < 8; r++) if (a->keys[c * 8 + r] && (rl >> r) & 1) cl |= (uint16_t)(1 << c);
    *rows_low = rl; *cols_low = cl;
}
static uint8_t kbd_port_in(void *ctx, int port)
{
    Dev6801 *d = ctx;
    uint8_t rl; uint16_t cl;
    kbd_lines(d->adam, &rl, &cl);
    switch (port) {
    case 1: return (uint8_t)~rl;
    case 3: return (uint8_t)~(cl & 0xFF);
    case 4: return (uint8_t)(~(cl >> 8) & 0xFF);
    default: return 0xFF;
    }
}

/* tape 6801: expanded multiplexed mode, 1K block buffer (2114 x2) at 0x0400 */
static uint8_t tape_read(void *ctx, uint16_t addr)
{
    Dev6801 *d = ctx;
    if (addr >= 0xF800) return d->rom[addr - 0xF800];
    if ((addr & 0xFC00) == 0x0400) return d->adam->ddp.ram[addr & 0x3FF];
    return 0xFF;
}
static void tape_write(void *ctx, uint16_t addr, uint8_t v)
{
    Dev6801 *d = ctx;
    if ((addr & 0xFC00) == 0x0400) d->adam->ddp.ram[addr & 0x3FF] = v;
}
static uint8_t tape_port_in(void *ctx, int port)
{
    Dev6801 *d = ctx;
    return ddp_port_in(&d->adam->ddp, port, d->cpu.cycles);
}
static void tape_port_out(void *ctx, int port, uint8_t data, uint8_t ddr)
{
    Dev6801 *d = ctx;
    ddp_port_out(&d->adam->ddp, port, data, ddr, d->cpu.cycles);
}

/* time of a disk board in µs (the WD2793 model counts µs) */
static uint64_t dev_us(const Dev6801 *d) { return d->cpu.cycles >> d->cpu.clock_shift; }

/* disk controller 6801: expanded multiplexed mode, 4K ROM at F000, WD2793 + 1K RAM.
 * A11 selects the 2793 (A14:A13 = register), A10 the RAM, and A12 sets the direction:
 * A12 = 1: the 2793 is written and the RAM drives the bus, A12 = 0: the 2793 drives the bus
 * and the RAM stores it. So LDAA 6C00+i moves a byte 2793 -> RAM, LDAA 7C00+i RAM -> 2793. */
static uint8_t fdc_read(void *ctx, uint16_t a)
{
    Dev6801 *d = ctx;
    Adam *ad = d->adam;
    int u = d->unit, reg = (a >> 13) & 3;
    if (a >= 0xF000) return d->rom[a - 0xF000];
    if (a & 0x8000) return 0xFF;
    uint8_t v = 0xFF;
    if ((a & 0x0800) && !(a & 0x1000)) v = wd_read(&ad->wd[u], reg, dev_us(d));
    if ((a & 0x0400) && (a & 0x1000)) v = ad->fdc_ram[u][a & 0x3FF];
    if ((a & 0x0400) && !(a & 0x1000)) ad->fdc_ram[u][a & 0x3FF] = v;
    if ((a & 0x0800) && (a & 0x1000)) wd_write(&ad->wd[u], reg, v, dev_us(d));
    return v;
}
static void fdc_write(void *ctx, uint16_t a, uint8_t v)
{
    Dev6801 *d = ctx;
    Adam *ad = d->adam;
    int u = d->unit;
    if (a & 0x8000) return;
    if ((a & 0x0800) && (a & 0x1000)) wd_write(&ad->wd[u], (a >> 13) & 3, v, dev_us(d));
    if ((a & 0x0400) && !(a & 0x1000)) ad->fdc_ram[u][a & 0x3FF] = v;
}
/* P1: b0 in = no disk, b2 in = DRQ, b7 in = unit jumper (node 4/5); b5 out = motor */
static uint8_t fdc_port_in(void *ctx, int port)
{
    Dev6801 *d = ctx;
    Adam *ad = d->adam;
    int u = d->unit;
    if (port != 1) return 0xFF;
    wd_update(&ad->wd[u], dev_us(d));
    uint8_t v = 0x7A;
    if ((!ad->floppy[u].present) != ad->fdc_ready_high[u]) v |= 0x01;   /* a720 board: 1 = ready */
    if (ad->wd[u].drq) v |= 0x04;
    if (u) v |= 0x80;
    return v;
}
/* P1 out: b4 side (0 = side 1, all the double-sided ROMs; the standard ROM holds it at 1),
 * b3 DDEN, or the data rate (1 = 500 kbit/s) on the 1.44M board */
static void fdc_port_out(void *ctx, int port, uint8_t data, uint8_t ddr)
{
    Dev6801 *d = ctx;
    Adam *ad = d->adam;
    WD2793 *w = &ad->wd[d->unit];
    if (port != 1) return;
    uint8_t v = (uint8_t)(data | ~ddr);
    wd_update(w, dev_us(d));
    w->side = !(v & 0x10);
    w->hd = ad->fdc_hd_select[d->unit] ? (v >> 3) & 1 : 0;
}

static void fdc_tick(Dev6801 *d)
{
    WD2793 *w = &d->adam->wd[d->unit];
    if (w->state && w->next <= dev_us(d)) wd_update(w, dev_us(d));
}
static void fdc_intrq(void *ctx, int level)
{
    Dev6801 *d = ctx;
    if (level) m6801_nmi(&d->cpu);     /* INTRQ -> /NMI */
}

static uint8_t prn_port_in(void *ctx, int port)
{
    Dev6801 *d = ctx;
    return printer_port_in(&d->adam->printer, port, d->cpu.cycles);
}
static void prn_port_out(void *ctx, int port, uint8_t data, uint8_t ddr)
{
    Dev6801 *d = ctx;
    printer_port_out(&d->adam->printer, port, data, ddr, d->cpu.cycles);
}

static void net_reset_devices(Adam *a, int assert)
{
    Dev6801 *devs[] = { &a->kbd, &a->tape, &a->prn, &a->fdc[0], &a->fdc[1] };
    for (unsigned i = 0; i < sizeof devs / sizeof devs[0]; i++) {
        Dev6801 *d = devs[i];
        if (!d->present) continue;
        if (assert) d->in_reset = 1;
        else if (d->in_reset) {
            d->in_reset = 0;
            uint64_t cyc = d->cpu.cycles;
            m6801_reset(&d->cpu);
            d->cpu.cycles = cyc;
            adamnet_flush_node(d->node);
        }
    }
}

void adam_init(Adam *a)
{
    a->z80.ctx = a;
    a->z80.read = z_read; a->z80.write = z_write; a->z80.in = z_in; a->z80.out = z_out;
    z80_init(&a->z80);
    adamnet_init(&a->net);
    dev_setup(a, &a->master, master_rom, 0xF800, "master", 7);
    a->master.cpu.os3 = master_os3;
    a->master.cpu.port_out = master_port_out;
    dev_setup(a, &a->kbd, kbd_rom, 0xF800, "kbd", 7);
    a->kbd.cpu.port_in = kbd_port_in;
    ddp_init(&a->ddp);
    dev_setup(a, &a->tape, tape_rom, 0xF800, "tape", 6);
    a->tape.cpu.read = tape_read; a->tape.cpu.write = tape_write;
    a->tape.cpu.port_in = tape_port_in; a->tape.cpu.port_out = tape_port_out;
    a->printer.sound = 1;
    printer_init(&a->printer, prn_rom, a->wp + 0x404);   /* font: SmartWriter ROM, ASCII 0x20-0x7E */
    dev_setup(a, &a->prn, prn_rom, 0xF800, "prn", 7);
    a->prn.cpu.port_in = prn_port_in; a->prn.cpu.port_out = prn_port_out;
    adam_attach_disk_drive(a, 0);      /* the usual ADAM has disk drive 1 */
    sn76489_init(&a->psg, Z80_HZ, 44100);
    ay8910_init(&a->ay, Z80_HZ / 2, AUDIO_RATE);      /* SGM: AY clocked at 1.789772 MHz */
    a->vdp_kind = -1;
    a->keypad[0] = a->keypad[1] = -1;
}

static void cart_power_on(Adam *a)
{
    /* MegaCart at power on: the last bank in both windows */
    a->cart_bank = (uint8_t)(a->cart_banks ? a->cart_banks - 1 : 0);
    a->sgm_53 = 0; a->sgm_7f = 0x0F;
    ay8910_reset(&a->ay);
}

void adam_power_on(Adam *a)
{
    cart_power_on(a);
    if (a->coleco) {
        memset(a->cv_ram, 0, sizeof a->cv_ram);
        memset(a->sgm_ram, 0, sizeof a->sgm_ram);
        a->ctrl_mode = 0;
        z80_reset(&a->z80);
        tms9918_reset(&a->vdp);
        a->z80_ps = a->now_ps = 0;
        return;
    }
    memset(a->ram, 0, sizeof a->ram);
    a->memmap = 0; a->port3f = 0; a->ctrl_mode = 0;
    z80_reset(&a->z80);
    tms9918_reset(&a->vdp);
    a->z80_ps = a->now_ps = 0;
    for (int u = 0; u < 2; u++) wd_reset(&a->wd[u]);
    printer_init(&a->printer, prn_rom, a->wp + 0x404);   /* font: SmartWriter ROM, ASCII 0x20-0x7E */
    Dev6801 *devs[] = { &a->master, &a->kbd, &a->tape, &a->prn, &a->fdc[0], &a->fdc[1] };
    for (unsigned i = 0; i < 6; i++) {
        if (!devs[i]->present) continue;
        devs[i]->cpu.cycles = 0;
        m6801_reset(&devs[i]->cpu);
        devs[i]->in_reset = 0;
        adamnet_flush_node(devs[i]->node);
    }
}

void adam_game_reset(Adam *a)
{
    /* CVRST: Z80, MIOC (map = OS-7 + cartridge) and the master 6801 */
    cart_power_on(a);
    a->memmap = 0x0F; a->port3f = 0;
    z80_reset(&a->z80);
    tms9918_reset(&a->vdp);
    if (a->coleco) return;
    uint64_t cyc = a->master.cpu.cycles;
    m6801_reset(&a->master.cpu);
    a->master.cpu.cycles = cyc;
    adamnet_flush_node(a->master.node);
}

int adam_load_cart(Adam *a, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (!a->cart) a->cart = malloc(CART_MAX);
    if (!a->cart) { fclose(f); return 0; }
    memset(a->cart, 0xFF, CART_MAX);
    size_t n = fread(a->cart, 1, CART_MAX, f);
    fclose(f);
    a->has_cart = n > 0;
    /* whole 16K banks; a MegaCart image is a power of two of them */
    a->cart_banks = (int)((n + 0x3FFF) / 0x4000);
    if (a->cart_banks < 2) a->cart_banks = 2;
    if (a->cart_banks > 2) fprintf(stderr, "cartridge: MegaCart, %d banks of 16K\n", a->cart_banks);
    return a->has_cart;
}

void adam_attach_disk_drive(Adam *a, int unit)
{
    Dev6801 *d = &a->fdc[unit];
    if (d->present) return;
    dev_setup(a, d, a->fdc_rom[unit], 0xF000, unit ? "disk2" : "disk1", 2);
    d->unit = unit;
    d->cpu.read = fdc_read; d->cpu.write = fdc_write;
    d->cpu.port_in = fdc_port_in; d->cpu.port_out = fdc_port_out;
    d->tick = fdc_tick;
    d->cpu.clock_shift = a->fdc_hd_select[unit];
    WD2793 *w = &a->wd[unit];
    memset(w, 0, sizeof *w);
    w->disk = &a->floppy[unit];
    w->ctx = d; w->intrq_changed = fdc_intrq;
    wd_reset(w);
    m6801_reset(&d->cpu);
}

/* another disk controller ROM for a drive; board differences recognised from the code:
 * "MMSG" at FFE6 (a720dipi): ready input inverted; reset vector F000 (1.44M Micro
 * Innovations): P1.3 selects the data rate */
int adam_set_fdc_rom(Adam *a, int unit, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint8_t buf[0x1000];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (n != sizeof buf) return 0;
    memcpy(a->fdc_rom[unit], buf, sizeof buf);
    static const uint8_t mmsg[4] = { 'M' | 0x80, 'M' | 0x80, 'S' | 0x80, 'G' | 0x80 };
    a->fdc_ready_high[unit] = !memcmp(buf + 0xFE6, mmsg, 4);
    a->fdc_hd_select[unit] = buf[0xFFE] == 0xF0 && buf[0xFFF] == 0x00;
    /* the 1.44M board must feed the 2793 a byte every 16 µs: its 6801 runs at E = 2 MHz
     * (hypothesis: the code uses an external SCI clock, which keeps AdamNet at 62.5 kbit/s) */
    a->fdc[unit].cpu.clock_shift = a->fdc_hd_select[unit];
    if (a->fdc_ready_high[unit]) fprintf(stderr, "disk%d: a720 board (ready = 1)\n", unit + 1);
    if (a->fdc_hd_select[unit]) fprintf(stderr, "disk%d: 1.44M board (P1.3 = data rate)\n", unit + 1);
    return 1;
}

int adam_insert_disk(Adam *a, int unit, const char *path)
{
    floppy_save(&a->floppy[unit]);
    return floppy_load(&a->floppy[unit], path);
}

void adam_shutdown(Adam *a)
{
    if (!printer_save(&a->printer)) fprintf(stderr, "printer: cannot write %s\n", a->printer.path);
    else if (a->printer.strikes && a->printer.path[0]) {
        /* the page image goes next to the text: FILE.txt -> FILE.png, FILE -> FILE.png */
        char png[1100];
        size_t n = strlen(a->printer.path);
        if (n > 4 && !strcmp(a->printer.path + n - 4, ".txt")) n -= 4;
        snprintf(png, sizeof png, "%.*s.png", (int)n, a->printer.path);
        int ok = printer_save_png(&a->printer, png);
        fprintf(stderr, "printer: page saved to %s%s%s\n", a->printer.path, ok ? " and " : "", ok ? png : "");
    }
    for (int u = 0; u < 2; u++)
        if (!floppy_save(&a->floppy[u])) fprintf(stderr, "disk: cannot write back %s\n", a->floppy[u].path);
    for (int i = 0; i < 2; i++)
        if (!ddp_save(&a->ddp, i)) fprintf(stderr, "ddp: cannot write back %s\n", a->ddp.drive[i].path);
}

void adam_key(Adam *a, int index, int pressed)
{
    if (index >= 0 && index < KBD_KEYS) a->keys[index] = (uint8_t)(pressed != 0);
}

/* look a key code up in the keyboard ROM's translation tables (normal/shift/control) */
int adam_key_for_code(Adam *a, uint8_t code, int *need_shift, int *need_ctrl)
{
    (void)a;
    for (int t = 0; t < 3; t++)
        for (int i = 0; i < 0x48; i++)
            if (kbd_rom[0xFAF8 - 0xF800 + t * 0x48 + i] == code) {
                if (need_shift) *need_shift = t == 1;
                if (need_ctrl) *need_ctrl = t == 2;
                return i;
            }
    return -1;
}

/* ------------------------------------------------------------- scheduler */
#define SLICE_Z80 16

static void run_dev(Dev6801 *d, uint64_t until_ps)
{
    uint64_t until = until_ps / (PS_PER_E >> d->cpu.clock_shift);
    if (!d->present) return;
    if (d->in_reset) { if (d->cpu.cycles < until) d->cpu.cycles = until; return; }
    if (d == &d->adam->tape && ddp_trace >= 2) {
        /* firmware probes: header accepted / header error / data read result */
        while (d->cpu.cycles < until) {
            M6801 *m = &d->cpu;
            if (m->pc == 0xFE53)
                fprintf(stderr, "[tape %10llu] header %c%c idx %02X%02X ~%02X%02X bpt %02X%02X  target %02X%02X\n",
                        (unsigned long long)m->cycles, m->ram[0x35], m->ram[0x36], m->ram[0x37], m->ram[0x38],
                        m->ram[0x39], m->ram[0x3A], m->ram[0x3B], m->ram[0x3C], m->ram[0x2B], m->ram[0x2C]);
            else if (m->pc == 0xFE65) fprintf(stderr, "[tape %10llu] header read failed\n", (unsigned long long)m->cycles);
            else if (m->pc == 0xFB21) fprintf(stderr, "[tape %10llu] data read, checksum on tape %02X%02X\n",
                                              (unsigned long long)m->cycles, m->ram[0x33], m->ram[0x34]);
            else if (m->pc == 0xFB24) fprintf(stderr, "[tape %10llu] buffer sum %02X%02X\n", (unsigned long long)m->cycles, m->a, m->b);
            m6801_step(m);
        }
        return;
    }
    if (d->tick) { while (d->cpu.cycles < until) { m6801_step(&d->cpu); d->tick(d); } return; }
    while (d->cpu.cycles < until) m6801_step(&d->cpu);
}

/* T-state (from the start of the instruction at pc) at which its I/O machine cycle ends: the
 * write strobe or the read sample. IN/OUT (n) and (C) end on their I/O cycle; the block
 * instructions do it in the middle: OUTI/OTIR M1 4 + 5, memory read 3, I/O 4 (then 5 to repeat),
 * INI/INIR M1 4 + 5, I/O 4, memory write 3 */
static int io_cycle_end(Adam *a, uint16_t pc, int cycles)
{
    if (pc >= 0xFFC0) return cycles;                 /* do not touch the MegaCart trigger area */
    if (z_read(a, pc) != 0xED) return cycles;
    uint8_t op = z_read(a, (uint16_t)(pc + 1));
    if ((op & 0xE7) == 0xA3) return 16;              /* OUTI OUTD OTIR OTDR */
    if ((op & 0xE7) == 0xA2) return 13;              /* INI IND INIR INDR */
    return cycles;
}

static void run_until(Adam *a, uint64_t end_ps)
{
    while (a->now_ps < end_ps) {
        uint64_t slice = a->now_ps + SLICE_Z80 * PS_PER_Z80;
        if (slice > end_ps) slice = end_ps;
        while (a->z80_ps < slice) {
            if (a->z80_stall) { a->z80_ps += (uint64_t)a->z80_stall * PS_PER_Z80; a->z80.cycles += (uint64_t)a->z80_stall; a->z80_stall = 0; continue; }
            if (a->trace_eos && a->z80.pc.w >= 0xFC30 && a->z80.pc.w < 0xFD60 && (a->z80.pc.w - 0xFC30) % 3 == 0)
                fprintf(stderr, "[eos %10.3f s] call %04X  A=%02X BC=%04X DE=%04X HL=%04X  from %04X\n",
                        (double)a->z80_ps / 1e12, a->z80.pc.w, a->z80.af.b.h, a->z80.bc.w, a->z80.de.w, a->z80.hl.w,
                        (uint16_t)(a->ram[a->z80.sp.w] | a->ram[(uint16_t)(a->z80.sp.w + 1)] << 8) - 3);
            if (a->trace_z80) {
                fprintf(stderr, "Z80 %04X AF=%04X BC=%04X DE=%04X HL=%04X SP=%04X\n",
                        a->z80.pc.w, a->z80.af.w, a->z80.bc.w, a->z80.de.w, a->z80.hl.w, a->z80.sp.w);
            }
            if (a->prof.enabled) {
                uint16_t pc0 = a->z80.pc.w;
                int nmi = a->z80.nmi_pending;
                int bank0 = (a->cart_banks > 2 && pc0 >= 0xC000 && (a->coleco || ((a->memmap >> 2) & 3) == 3))
                            ? a->cart_bank : -1;
                a->vdp_kind = -1;
                int c = z80_step(&a->z80);
                vdpprof_pc(&a->prof, pc0, bank0, c);
                a->prof.cur_bank = a->cart_banks > 2 ? a->cart_bank : -1;
                if (nmi) vdpprof_nmi(&a->prof, &a->vdp, pc0, a->z80.sp.w, a->z80_ps);
                else {
                    if (a->vdp_kind >= 0) {
                        int at = io_cycle_end(a, pc0, c);
                        vdpprof_access(&a->prof, &a->vdp, a->vdp_kind, a->z80_ps + (uint64_t)at * PS_PER_Z80,
                                       a->frame_start_ps, pc0, bank0);
                    }
                    vdpprof_step(&a->prof, pc0, a->z80.pc.w, a->z80.sp.w, a->z80.halted, a->z80_ps);
                }
                a->z80_ps += (uint64_t)c * PS_PER_Z80;
                continue;
            }
            int c = z80_step(&a->z80);
            a->z80_ps += (uint64_t)c * PS_PER_Z80;
        }
        if (a->coleco) { a->now_ps = slice; continue; }
        run_dev(&a->master, slice);
        run_dev(&a->kbd, slice);
        run_dev(&a->tape, slice);
        run_dev(&a->prn, slice);
        run_dev(&a->fdc[0], slice);
        run_dev(&a->fdc[1], slice);
        a->now_ps = slice;
    }
}

void adam_run_frame(Adam *a)
{
    a->frame_start_ps = a->now_ps;
    a->audio_n = 0;
    for (;;) {
        uint64_t line_end = a->now_ps + VDP_CPU_PER_LINE * PS_PER_Z80;
        run_until(a, line_end);
        int line = a->vdp.line;
        int done = tms9918_run_line(&a->vdp);
        if (line == 191 && a->prof.enabled) vdpprof_frame_flag(&a->prof, &a->vdp, a->now_ps, a->frames);
        if (a->vdp.int_out && !a->last_vdp_int) z80_nmi(&a->z80);
        a->last_vdp_int = a->vdp.int_out;
        if (done) break;
    }
    audio_sync(a);
    printer_mix(&a->printer, a->audio, a->audio_n, a->frame_start_ps / 1000000, a->now_ps / 1000000);
    a->frames++;
}
