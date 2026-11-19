/* wd2793.c - WD2793 floppy disk controller. See wd2793.h. */
#include "wd2793.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { S_IDLE, S_STEP, S_VERIFY, S_DONE, S_SEARCH, S_RDATA, S_WFIRST, S_WDATA, S_CRC,
       S_RADDR, S_WTRACK_WAIT, S_WTRACK };

enum { ST_BUSY = 0x01, ST_DRQ = 0x02, ST_INDEX = 0x02, ST_LOST = 0x04, ST_TR00 = 0x04, ST_CRC = 0x08,
       ST_RNF = 0x10, ST_SEEKERR = 0x10, ST_HLD = 0x20, ST_WP = 0x40, ST_NOTRDY = 0x80 };

#define REV        200000u     /* µs per turn at 300 rpm */
#define BYTE_T     (w->hd ? 16u : 32u)                         /* µs per MFM byte: 250 / 500 kbit/s */
#define TRACK_BYTES (REV / BYTE_T)
#define WT_MAX      (REV / 16u)                                /* write-track buffer, HD size */
/* ID field of the p-th sector, as the ADAM format lays the track out: gap 4a, sync and index
 * mark, gap 1 (146 bytes), then per sector sync 12 + ID mark 4 + ID 6 + gap 2 22 + sync 12 +
 * data mark 4 + data + CRC 2 + gap 3 100 (0x64, the value every controller ROM writes) */
#define SECTOR_PITCH(w) (174u + (unsigned)((w)->disk ? (w)->disk->secsize : 512))
#define ID_POS(p)  ((146u + (unsigned)(p) * SECTOR_PITCH(w)) * BYTE_T)
#define ID_LEN     (10u * BYTE_T)                              /* A1 A1 A1 FE C H R N CRC CRC */
#define DATA_GAP   (38u * BYTE_T)                              /* gap 2 + sync + data mark */

static const unsigned step_ms[4] = { 6, 12, 20, 30 };          /* at a 1 MHz clock */

static void set_intrq(WD2793 *w, int level)
{
    if (w->intrq != level) {
        w->intrq = (uint8_t)level;
        if (w->intrq_changed) w->intrq_changed(w->ctx, level);
    }
}

void wd_reset(WD2793 *w)
{
    w->cmd = 0; w->track = 0; w->sector = 1; w->data = 0;
    w->st = 0; w->busy = 0; w->drq = 0; w->type = 1;
    w->state = S_IDLE; w->dir = 1;
    set_intrq(w, 0);
}

static int disk_ok(WD2793 *w) { return w->disk && w->disk->present; }
/* the head can read the disk: present, side exists, data rate matches the media */
static int readable(WD2793 *w) { return disk_ok(w) && w->side < w->disk->sides && w->hd == w->disk->hd; }

/* sector data in the image for the current head position, or NULL */
static uint8_t *sector_ptr(WD2793 *w, int cyl, int id)
{
    Floppy *f = w->disk;
    if (!readable(w) || cyl < 0 || cyl >= f->tracks || id < 1 || id > f->sectors) return NULL;
    size_t off = (((size_t)cyl * (size_t)f->sides + (size_t)w->side) * (size_t)f->sectors + (size_t)(id - 1)) * (size_t)f->secsize;
    return off + (size_t)f->secsize <= f->size ? f->data + off : NULL;
}

/* time at which the ID field of sector `id` next starts passing under the head */
static uint64_t next_id(WD2793 *w, uint64_t now, int id)
{
    uint64_t phase = now % REV, t = ID_POS(id - 1);
    return now + (t >= phase ? t - phase : REV - phase + t);
}

static void finish(WD2793 *w, uint64_t now)
{
    (void)now;
    w->busy = 0;
    w->drq = 0;
    w->state = S_IDLE;
    set_intrq(w, 1);
}

static uint8_t status(WD2793 *w, uint64_t now)
{
    uint8_t s = w->st | (w->busy ? ST_BUSY : 0);
    if (w->type == 1 || w->type == 4) {
        s &= (uint8_t)~(ST_DRQ | ST_TR00 | ST_NOTRDY);
        if (w->cyl == 0) s |= ST_TR00;
        if (disk_ok(w) && now % REV < 4000) s |= ST_INDEX;
        s |= ST_HLD;
        if (disk_ok(w) && w->disk->wprot) s |= ST_WP;
    } else {
        if (w->drq) s |= ST_DRQ;
    }
    return s;
}

static void start_type2(WD2793 *w, uint64_t now)
{
    w->type = (w->cmd & 0x40) ? 3 : 2;    /* refined by the caller for type III */
    w->busy = 1; w->drq = 0; w->st = 0; w->revs = 0;
    w->state = S_SEARCH;
    w->next = now + ((w->cmd & 0x04) ? 30000u : 0u);   /* E: 30 ms head settle at 1 MHz */
}

void wd_write(WD2793 *w, int reg, uint8_t v, uint64_t now)
{
    wd_update(w, now);
    switch (reg) {
    case 1: w->track = v; return;
    case 2: w->sector = v; return;
    case 3: w->data = v; w->drq = 0; return;
    default: break;
    }
    if (w->trace) fprintf(stderr, "[wd %10llu] cmd %02X  trk %d sec %d data %d  head %d side %d%s\n",
                          (unsigned long long)now, v, w->track, w->sector, w->data, w->cyl, w->side, w->hd ? " HD" : "");
    if ((v & 0xF0) == 0xD0) {                 /* force interrupt */
        w->busy = 0; w->drq = 0; w->state = S_IDLE; w->type = 4; w->cmd = v;
        if (v & 0x08) set_intrq(w, 1); else set_intrq(w, 0);
        return;
    }
    if (w->busy) return;
    w->cmd = v;
    set_intrq(w, 0);
    if (!(v & 0x80)) {                          /* type I */
        /* target: -1 restore (out to track 0), >= 0 seek to the data register,
         * -2 one step (then -3 = done) */
        w->type = 1; w->busy = 1; w->drq = 0; w->st = 0;
        switch (v >> 4) {
        case 0x0: w->target = -1; break;
        case 0x1: w->target = w->data; break;
        case 0x2: case 0x3: w->target = -2; break;                     /* step, same direction */
        case 0x4: case 0x5: w->dir = 1; w->target = -2; break;         /* step in */
        default: w->dir = -1; w->target = -2; break;                   /* step out */
        }
        w->state = S_STEP;
        w->next = now;
        return;
    }
    switch (v >> 4) {
    case 0x8: case 0x9: case 0xA: case 0xB:     /* read / write sector */
        start_type2(w, now);
        w->type = 2;
        if ((v & 0x20) && disk_ok(w) && w->disk->wprot) { w->st |= ST_WP; finish(w, now); }
        break;
    case 0xC:                                   /* read address */
        start_type2(w, now); w->type = 3; w->state = S_RADDR;
        break;
    case 0xF:                                   /* write track */
        start_type2(w, now); w->type = 3;
        if (disk_ok(w) && w->disk->wprot) { w->st |= ST_WP; finish(w, now); break; }
        w->state = S_WTRACK_WAIT;
        w->drq = 1;                             /* first byte is requested right away... */
        w->next = now + 3u * BYTE_T;            /* ...and must come within 3 byte times */
        break;
    default:                                    /* read track: not supported, ends at once */
        w->type = 3; w->busy = 1; w->state = S_DONE; w->next = now + REV;
        break;
    }
}

uint8_t wd_read(WD2793 *w, int reg, uint64_t now)
{
    wd_update(w, now);
    switch (reg) {
    case 0: { uint8_t s = status(w, now); set_intrq(w, 0); return s; }
    case 1: return w->track;
    case 2: return w->sector;
    default: w->drq = 0; return w->data;
    }
}

/* store a formatted track: find ID fields (FE C H R N) and data fields (FB + 512 bytes) */
static void parse_written_track(WD2793 *w)
{
    uint8_t *t = w->wtrack;
    int n = w->wtlen, cur = -1;
    for (int i = 0; i < n; i++) {
        if (t[i] == 0xFE && i + 4 < n && i > 0 && t[i - 1] == 0xF5) {
            cur = t[i + 3];                     /* R */
            i += 4;
        } else if (t[i] == 0xFB && i > 0 && t[i - 1] == 0xF5 && cur >= 0) {
            uint8_t *p = sector_ptr(w, w->cyl, cur);
            if (p) {
                int len = w->disk->secsize;
                for (int k = 0; k < len && i + 1 + k < n; k++) p[k] = t[i + 1 + k];
                w->disk->dirty = 1;
            }
            cur = -1;
        }
    }
}

void wd_update(WD2793 *w, uint64_t now)
{
    while (w->state != S_IDLE && w->next <= now) {
        uint64_t t = w->next;
        switch (w->state) {
        case S_STEP: {
            int done;
            if (w->target == -1) done = w->cyl == 0;
            else if (w->target >= 0) done = w->track == w->target;
            else done = w->target == -3;
            if (!done) {
                if (w->target >= 0) w->dir = w->target > w->track ? 1 : -1;
                if (w->target == -1) w->dir = -1;
                w->cyl += w->dir;
                if (w->cyl < 0) w->cyl = 0;
                if (w->cyl > 79) w->cyl = 79;
                /* seek/restore always update the track register; step commands only with u */
                if (w->target != -2 || (w->cmd & 0x10)) w->track = (uint8_t)(w->track + w->dir);
                if (w->target == -2) w->target = -3;
                w->next = t + step_ms[w->cmd & 3] * 1000u;
                break;
            }
            if (w->target == -1) w->track = 0;
            if (w->cmd & 0x04) { w->state = S_VERIFY; w->next = t + 30000u; }
            else w->state = S_DONE;
            break;
        }
        case S_DONE:
            finish(w, t);
            break;
        case S_VERIFY:
            if (!disk_ok(w) || w->track != w->cyl || w->cyl >= w->disk->tracks) w->st |= ST_SEEKERR;
            finish(w, t);
            break;
        case S_SEARCH: {
            if (!sector_ptr(w, w->cyl, w->sector) || w->track != w->cyl) {
                w->st |= ST_RNF;                /* five turns of searching */
                w->state = S_DONE; w->next = t + 5u * REV;
                break;
            }
            w->ptr = sector_ptr(w, w->cyl, w->sector);
            uint64_t id_end = next_id(w, t, w->sector) + ID_LEN;
            w->pos = 0;
            if (w->cmd & 0x20) {                /* write: request the first byte */
                w->state = S_WFIRST; w->next = id_end + 2u * BYTE_T;
            } else {
                w->state = S_RDATA; w->next = id_end + DATA_GAP;
            }
            break;
        }
        case S_RDATA:
            if (w->drq) w->st |= ST_LOST;
            w->data = w->ptr[w->pos++];
            w->drq = 1;
            if (w->pos < w->disk->secsize) w->next = t + BYTE_T;
            else { w->state = S_CRC; w->next = t + 2u * BYTE_T; }
            break;
        case S_WFIRST:
            w->drq = 1;
            w->state = S_WDATA; w->next = t + DATA_GAP;
            break;
        case S_WDATA:
            if (w->drq) { w->st |= ST_LOST; w->data = 0; }
            w->ptr[w->pos++] = w->data;
            w->disk->dirty = 1;
            if (w->pos < w->disk->secsize) { w->drq = 1; w->next = t + BYTE_T; }
            else { w->drq = 0; w->state = S_CRC; w->next = t + 2u * BYTE_T; }
            break;
        case S_CRC:
            w->drq = 0;
            if (w->cmd & 0x10) {                /* multiple sectors */
                w->sector++;
                if (sector_ptr(w, w->cyl, w->sector)) { w->state = S_SEARCH; w->next = t; break; }
            }
            finish(w, t);
            break;
        case S_RADDR:
            if (w->pos == 0) {
                if (!readable(w) || w->cyl >= w->disk->tracks) { w->st |= ST_RNF; w->state = S_DONE; w->next = t + 5u * REV; break; }
                /* the next ID to pass, any sector */
                uint64_t best = ~0ull; int id = 1;
                for (int s = 1; s <= w->disk->sectors; s++) { uint64_t x = next_id(w, t, s); if (x < best) { best = x; id = s; } }
                w->idbuf[0] = (uint8_t)w->cyl; w->idbuf[1] = (uint8_t)w->side; w->idbuf[2] = (uint8_t)id; w->idbuf[3] = 2;
                w->idbuf[4] = w->idbuf[5] = 0;
                w->pos = 1;
                w->next = best + 4u * BYTE_T;
                break;
            }
            if (w->drq) w->st |= ST_LOST;
            w->data = w->idbuf[w->pos - 1];
            w->drq = 1;
            if (++w->pos <= 6) w->next = t + BYTE_T;
            else { w->sector = w->idbuf[0]; w->state = S_CRC; w->cmd &= (uint8_t)~0x10; w->next = t + BYTE_T; }
            break;
        case S_WTRACK_WAIT: {                   /* start at the index hole */
            uint64_t phase = t % REV;
            if (w->drq) { w->st |= ST_LOST; finish(w, t); break; }
            if (!w->wtrack) w->wtrack = malloc(WT_MAX + 16);
            w->wtlen = 0;
            w->state = S_WTRACK;
            w->next = t + (REV - phase) % REV;
            break;
        }
        case S_WTRACK:
            if (w->drq) { w->st |= ST_LOST; w->data = 0; }
            if (w->wtlen < (int)TRACK_BYTES) w->wtrack[w->wtlen++] = w->data;
            if (w->wtlen >= (int)TRACK_BYTES) { parse_written_track(w); finish(w, t); break; }
            w->drq = 1;
            w->next = t + BYTE_T;
            break;
        default:
            w->state = S_IDLE;
            break;
        }
    }
}

/* ------------------------------------------------------------- images */
int floppy_load(Floppy *f, const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (n <= 0 || n > 2 * 1024 * 1024) { fclose(fp); return 0; }
    floppy_free(f);
    f->data = malloc((size_t)n);
    f->size = fread(f->data, 1, (size_t)n, fp);
    fclose(fp);
    /* geometry from the size: 160K SS 40x8, 320K DS 40x8, 640K/720K/1.44M DS 80 tracks */
    f->secsize = 512; f->sectors = 8; f->sides = 1; f->tracks = 40;
    if (f->size == 327680) f->sides = 2;
    else if (f->size == 655360) { f->sides = 2; f->tracks = 80; }
    else if (f->size == 737280) { f->sides = 2; f->tracks = 80; f->sectors = 9; }
    else if (f->size == 1474560) { f->sides = 2; f->tracks = 80; f->sectors = 18; }
    f->hd = f->sectors == 18;
    snprintf(f->path, sizeof f->path, "%s", path);
    f->present = 1; f->dirty = 0; f->wprot = 0;
    return 1;
}

int floppy_save(Floppy *f)
{
    if (!f->present || !f->dirty || !f->path[0]) return 1;
    FILE *fp = fopen(f->path, "wb");
    if (!fp) return 0;
    size_t n = fwrite(f->data, 1, f->size, fp);
    fclose(fp);
    if (n != f->size) return 0;
    f->dirty = 0;
    return 1;
}

void floppy_free(Floppy *f)
{
    free(f->data);
    f->data = NULL; f->size = 0; f->present = 0;
}
