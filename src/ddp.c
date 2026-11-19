/* ddp.c - Digital Data Pack tape drives. See ddp.h and docs/RECON.md.
 *
 * Tape 6801 wiring (from the tape ROM):
 *   P1 out: b0 SPEED (1 = 80 ips), b1 STOP drive 0, b2 STOP drive 1, b3 GO FWD (low),
 *           b4 GO REV (low), b5 BRAKE, b6 WRITE ENABLE drive 0 (low), b7 WRITE ENABLE drive 1 (low)
 *   P2:     b0 out WRITE DATA, b1 in cassette in drive 1, b2 out TRACK select
 *   P4 in:  b3 MSENSE drive 0, b4 MSENSE drive 1, b5 cassette in drive 0, b7 READ DATA
 * Factory format of one block (bytes MSB first, bi-phase mark, 70 µs cells):
 *   header: 00 00 00 00 16  'H''E'|'G''W'  idx_hi idx_lo  ~idx_hi ~idx_lo  00 80  sum  00
 *           (sum makes the 9 header bytes add up to 0xFF)
 *   data:   00 00 00 00 16  <1024 bytes>  00 00 16  cks_hi cks_lo  00   (cks = 16-bit byte sum)
 */
#include "ddp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { C_LEVEL = 1, C_FLUX = 2, C_MID = 4 };
int ddp_trace;
#define HDR_CELLS   (15 * 8)
#define DATA_OFFSET 150                 /* cells from block start to the data record */
#define TRACK_UNITS ((int64_t)DDP_TRACK_CELLS * DDP_CELL)

static int logical_block(const Cassette *c, int track, int p)
{
    if (track) return 128 + p;
    return c->format_he ? (p + 64) % 128 : p;
}

static void put_bytes(uint8_t *cl, int64_t cell, const uint8_t *b, int n)
{
    for (int i = 0; i < n; i++)
        for (int k = 7; k >= 0; k--, cell++)
            if (cell >= 0 && cell < DDP_TRACK_CELLS)
                cl[cell] = (uint8_t)(C_FLUX | (((b[i] >> k) & 1) ? C_MID : 0));
}

static void compute_levels(uint8_t *cl)
{
    uint8_t lvl = 0;
    for (int64_t i = 0; i < DDP_TRACK_CELLS; i++) {
        uint8_t v = cl[i];
        if (v & C_FLUX) lvl ^= 1;
        cl[i] = (uint8_t)((v & ~C_LEVEL) | lvl);
        if (v & C_MID) lvl ^= 1;
    }
}

static void build_block(Cassette *c, int track, int p)
{
    uint8_t *cl = c->cells[track];
    int64_t start = DDP_LEADER_CELLS + (int64_t)p * DDP_PITCH_CELLS;
    uint8_t h[15] = { 0, 0, 0, 0, 0x16 };
    h[5] = c->format_he ? 'H' : 'G';
    h[6] = c->format_he ? 'E' : 'W';
    h[7] = (uint8_t)(p >> 8); h[8] = (uint8_t)p;
    h[9] = (uint8_t)~h[7]; h[10] = (uint8_t)~h[8];
    h[11] = 0x00; h[12] = 0x80;
    uint8_t s = 0;
    for (int i = 5; i < 13; i++) s = (uint8_t)(s + h[i]);
    h[13] = (uint8_t)(0xFF - s);
    h[14] = 0;
    /* the formatter records zero bits between the records: a sync search that locks onto
     * a data record's checksum sync must run through recorded zeros, not unrecorded tape,
     * to find the next header's preamble intact */
    for (int64_t i = start; i < start + DDP_PITCH_CELLS; i++) cl[i] = C_FLUX;
    put_bytes(cl, start, h, 15);

    const uint8_t *data = c->image + logical_block(c, track, p) * DDP_BLOCK_SIZE;
    uint16_t sum = 0;
    for (int i = 0; i < DDP_BLOCK_SIZE; i++) sum = (uint16_t)(sum + data[i]);
    static const uint8_t pre[5] = { 0, 0, 0, 0, 0x16 };
    uint8_t post[7] = { 0, 0, 0x16, 0, 0, 0, 0 };
    post[3] = (uint8_t)(sum >> 8); post[4] = (uint8_t)sum;
    int64_t d = start + DATA_OFFSET;
    put_bytes(cl, d, pre, 5);
    put_bytes(cl, d + 5 * 8, data, DDP_BLOCK_SIZE);
    put_bytes(cl, d + (5 + DDP_BLOCK_SIZE) * 8, post, 6);
}

static void build_tape(Cassette *c)
{
    for (int t = 0; t < 2; t++) {
        if (!c->cells[t]) c->cells[t] = malloc(DDP_TRACK_CELLS);
        memset(c->cells[t], 0, DDP_TRACK_CELLS);
        for (int p = 0; p < 128; p++) build_block(c, t, p);
        compute_levels(c->cells[t]);
    }
}

void ddp_init(DDP *d)
{
    memset(d, 0, sizeof *d);
    d->p1 = 0xDE;
}

int ddp_insert(DDP *d, int drive, const char *path, int create_blank)
{
    Cassette *c = &d->drive[drive];
    memset(c->image, 0, sizeof c->image);
    FILE *f = fopen(path, "rb");
    if (f) {
        size_t n = fread(c->image, 1, sizeof c->image, f);
        fclose(f);
        if (n == 0) return 0;
        c->file_size = n;
    } else if (!create_blank) {
        return 0;
    } else {
        c->dirty = 1;
        c->file_size = sizeof c->image;
    }
    snprintf(c->path, sizeof c->path, "%s", path);
    c->format_he = 1;
    build_tape(c);
    c->present = 1;
    c->pos = 0; c->vel = 0; c->stalled = 0; c->writing = 0;
    return 1;
}

int ddp_save(DDP *d, int drive)
{
    Cassette *c = &d->drive[drive];
    if (!c->present || !c->dirty || !c->path[0]) return 1;
    FILE *f = fopen(c->path, "wb");
    if (!f) return 0;
    /* keep the size of short block images (e.g. an 18K .img); warn if data was written past it */
    size_t size = c->file_size ? c->file_size : sizeof c->image;
    for (size_t i = size; i < sizeof c->image; i++)
        if (c->image[i]) {
            fprintf(stderr, "ddp: %s holds %zu bytes, blocks written past its end are not saved\n", c->path, size);
            break;
        }
    size_t n = fwrite(c->image, 1, size, f);
    fclose(f);
    if (n != size) return 0;
    c->dirty = 0;
    return 1;
}

void ddp_eject(DDP *d, int drive)
{
    Cassette *c = &d->drive[drive];
    ddp_save(d, drive);
    c->present = 0; c->vel = 0; c->writing = 0;
}

/* ------------------------------------------------------------- mechanics */
static void advance(Cassette *c, uint64_t now)
{
    if (now > c->last && c->vel) {
        int64_t np = c->pos + (int64_t)c->vel * (int64_t)(now - c->last);
        if (np < 0) { np = 0; c->stalled = 1; }
        else if (np >= TRACK_UNITS) { np = TRACK_UNITS - 1; c->stalled = 1; }
        c->pos = np;
    }
    c->last = now;
}

static int msense(const Cassette *c) { return c->present && c->vel && !c->stalled; }

static int head_level(const Cassette *c, int track)
{
    if (!c->present) return 1;
    int64_t cell = c->pos / DDP_CELL;
    if (cell < 0 || cell >= DDP_TRACK_CELLS) return 1;
    uint8_t v = c->cells[track][cell];
    int lvl = v & C_LEVEL;
    if ((v & C_MID) && c->pos % DDP_CELL >= DDP_CELL / 2) lvl ^= 1;
    return lvl;
}

/* ------------------------------------------------------------- writing */
static int push_cell(uint8_t **bits, int *n, int *cap, uint8_t v)
{
    if (*n == *cap) {
        *cap = *cap ? *cap * 2 : 16384;
        uint8_t *nb = realloc(*bits, (size_t)*cap);
        if (!nb) return 0;
        *bits = nb;
    }
    (*bits)[(*n)++] = v;
    return 1;
}

static int read_bits(const uint8_t *bits, int n, int *i, int count, uint8_t *out)
{
    for (int k = 0; k < count; k++) {
        uint8_t v = 0;
        for (int b = 0; b < 8; b++) {
            if (*i >= n) return 0;
            v = (uint8_t)(v << 1 | bits[(*i)++]);
        }
        out[k] = v;
    }
    return 1;
}
static int find_sync(const uint8_t *bits, int n, int *i)
{
    uint8_t sr = 0;
    while (*i < n) {
        sr = (uint8_t)(sr << 1 | bits[(*i)++]);
        if (sr == 0x16) return 1;
    }
    return 0;
}

static void commit_write(Cassette *c)
{
    uint8_t *cl = c->cells[c->wtrack];
    int64_t s = c->wstart / DDP_CELL, e = c->pos / DDP_CELL;
    if (e < s) { int64_t t = s; s = e; e = t; }
    for (int64_t i = s; i <= e && i < DDP_TRACK_CELLS; i++) if (i >= 0) cl[i] = 0;   /* erased */

    uint8_t *bits = NULL;
    int n = 0, cap = 0;
    if (c->nwtrans >= 2) {
        int64_t boundary = c->wtrans[0];
        int mid = 0;
        for (int i = 1; i < c->nwtrans; i++) {
            int64_t dd = c->wtrans[i] - boundary;
            if (dd < DDP_CELL * 3 / 4) { mid = 1; continue; }
            push_cell(&bits, &n, &cap, (uint8_t)mid);
            for (int64_t k = (dd + DDP_CELL / 2) / DDP_CELL; k > 1; k--) push_cell(&bits, &n, &cap, 0);
            boundary = c->wtrans[i];
            mid = 0;
        }
        push_cell(&bits, &n, &cap, (uint8_t)mid);
        int64_t c0 = (c->wtrans[0] + DDP_CELL / 2) / DDP_CELL;
        for (int i = 0; i < n; i++)
            if (c0 + i >= 0 && c0 + i < DDP_TRACK_CELLS) cl[c0 + i] = (uint8_t)(C_FLUX | (bits[i] ? C_MID : 0));
    }
    compute_levels(cl);

    /* decode the record and update the image */
    int i = 0;
    uint8_t buf[DDP_BLOCK_SIZE], ck[2];
    int64_t p = (s - DDP_LEADER_CELLS) / DDP_PITCH_CELLS;
    if (bits && p >= 0 && p < 128 && find_sync(bits, n, &i) && read_bits(bits, n, &i, DDP_BLOCK_SIZE, buf)
        && find_sync(bits, n, &i) && read_bits(bits, n, &i, 2, ck)) {
        uint16_t sum = 0;
        for (int k = 0; k < DDP_BLOCK_SIZE; k++) sum = (uint16_t)(sum + buf[k]);
        int b = logical_block(c, c->wtrack, (int)p);
        if (sum == (uint16_t)(ck[0] << 8 | ck[1])) {
            memcpy(c->image + b * DDP_BLOCK_SIZE, buf, DDP_BLOCK_SIZE);
            c->dirty = 1;
        } else {
            fprintf(stderr, "ddp: write of block %d has a bad checksum\n", b);
        }
    } else {
        fprintf(stderr, "ddp: unreadable write at cell %lld\n", (long long)s);
    }
    free(bits);
    c->writing = 0;
    c->nwtrans = 0;
}

/* ------------------------------------------------------------- 6801 ports */
static void controls(DDP *d, uint64_t now)
{
    uint8_t p1 = d->p1;
    for (int i = 0; i < 2; i++) {
        Cassette *c = &d->drive[i];
        advance(c, now);
        int stop = p1 & (i ? 0x04 : 0x02);
        int v = 0;
        if (c->present && !stop && !(p1 & 0x20)) {
            int fwd = !(p1 & 0x08), rev = !(p1 & 0x10), sp = (p1 & 1) ? 4 : 1;
            if (fwd && !rev) v = sp;
            else if (rev && !fwd) v = -sp;
        }
        if (v != c->vel) {
            if (ddp_trace)
                fprintf(stderr, "[ddp %10llu] drive %d P1=%02X vel %d -> %d at cell %lld (block pos %.2f)\n",
                        (unsigned long long)now, i, p1, c->vel, v, (long long)(c->pos / DDP_CELL),
                        (double)(c->pos / DDP_CELL - DDP_LEADER_CELLS) / DDP_PITCH_CELLS);
            c->vel = v; c->stalled = 0;
        }
        int we = c->present && !(p1 & (i ? 0x80 : 0x40));
        if (we && !c->writing) {
            c->writing = 1; c->wtrack = (d->p2 >> 2) & 1; c->wstart = c->pos; c->nwtrans = 0;
        } else if (!we && c->writing) {
            commit_write(c);
        }
    }
}

uint8_t ddp_port_in(DDP *d, int port, uint64_t now)
{
    Cassette *c0 = &d->drive[0], *c1 = &d->drive[1];
    if (port == 4) {
        advance(c0, now); advance(c1, now);
        uint8_t v = 0x47;                                  /* outputs/unused read high */
        if (msense(c0)) v |= 0x08;
        if (msense(c1)) v |= 0x10;
        if (c0->present) v |= 0x20;
        int track = (d->p2 >> 2) & 1, data = 1;
        if (!(d->p1 & 0x02)) data &= head_level(c0, track);
        if (!(d->p1 & 0x04)) data &= head_level(c1, track);
        if (data) v |= 0x80;
        return v;
    }
    if (port == 2) return (uint8_t)(c1->present ? 0xFF : 0xFD);
    return 0xFF;
}

void ddp_port_out(DDP *d, int port, uint8_t data, uint8_t ddr, uint64_t now)
{
    if (port == 1) {
        d->p1 = (uint8_t)(data | ~ddr);
        controls(d, now);
    } else if (port == 2) {
        uint8_t v = (uint8_t)((data & ddr) | (~ddr & 0xFF));
        if ((v ^ d->p2) & 1) {
            for (int i = 0; i < 2; i++) {
                Cassette *c = &d->drive[i];
                if (!c->writing) continue;
                advance(c, now);
                if (c->nwtrans == c->capwtrans) {
                    c->capwtrans = c->capwtrans ? c->capwtrans * 2 : 16384;
                    c->wtrans = realloc(c->wtrans, sizeof *c->wtrans * (size_t)c->capwtrans);
                }
                c->wtrans[c->nwtrans++] = c->pos;
            }
        }
        d->p2 = v;
    }
}

int ddp_motor_on(DDP *d) { return d->drive[0].vel || d->drive[1].vel; }
