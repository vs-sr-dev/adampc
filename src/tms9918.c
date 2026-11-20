/* tms9918.c - TMS9918A/9928A VDP. See tms9918.h. */
#include "tms9918.h"
#include <string.h>

/* commonly used approximation of the TMS9928A output colours */
const uint32_t tms9918_palette[16] = {
    0x000000, 0x000000, 0x21C842, 0x5EDC78, 0x5455ED, 0x7D76FC, 0xD4524D, 0x42EBF5,
    0xFC5554, 0xFF7978, 0xD4C154, 0xE6CE80, 0x21B03B, 0xC95BBA, 0xCCCCCC, 0xFFFFFF
};

void tms9918_reset(TMS9918 *v)
{
    memset(v->reg, 0, sizeof v->reg);
    v->status = 0; v->addr = 0; v->latch = 0; v->latch_full = 0; v->readbuf = 0;
    v->int_out = 0; v->line = 0;
}

static void update_int(TMS9918 *v)
{
    v->int_out = (uint8_t)((v->status & 0x80) && (v->reg[1] & 0x20));
}

uint8_t tms9918_read(TMS9918 *v, int port)
{
    v->latch_full = 0;
    if (port & 1) {
        uint8_t s = v->status;
        v->status &= 0x1F;
        update_int(v);
        return s;
    }
    uint8_t r = v->readbuf;
    v->readbuf = v->vram[v->addr & 0x3FFF];
    v->addr = (uint16_t)((v->addr + 1) & 0x3FFF);
    return r;
}

void tms9918_write(TMS9918 *v, int port, uint8_t val)
{
    if (port & 1) {
        if (!v->latch_full) {
            v->latch = val;
            v->latch_full = 1;
            v->addr = (uint16_t)((v->addr & 0x3F00) | val);
            return;
        }
        v->latch_full = 0;
        if (val & 0x80) {
            v->reg[val & 7] = v->latch;
            update_int(v);
        } else {
            v->addr = (uint16_t)(((val & 0x3F) << 8) | v->latch);
            if (!(val & 0x40)) {
                v->readbuf = v->vram[v->addr];
                v->addr = (uint16_t)((v->addr + 1) & 0x3FFF);
            }
        }
        return;
    }
    v->latch_full = 0;
    v->vram[v->addr & 0x3FFF] = val;
    v->readbuf = val;
    v->addr = (uint16_t)((v->addr + 1) & 0x3FFF);
}

/* ------------------------------------------------------------- rendering */
static void draw_sprites(TMS9918 *v, int y, uint8_t *out)
{
    const uint8_t *sat = v->vram + ((v->reg[5] & 0x7F) << 7);
    const uint8_t *spg = v->vram + ((v->reg[6] & 7) << 11);
    int big = v->reg[1] & 2, mag = v->reg[1] & 1;
    int size = (big ? 16 : 8) << mag;
    int shown[4], nshown = 0, i, extra = 0, fifth = -1;
    for (i = 0; i < 32; i++) {
        int sy = sat[i * 4];
        if (sy == 0xD0) break;
        if (sy > 0xE0) sy -= 256;
        int row = y - sy - 1;
        if (row < 0 || row >= size) continue;
        if (nshown == 4) {
            /* 5S is set only while the frame flag is 0 (data manual 2.3.3) */
            if (!extra && !(v->status & 0xC0)) { v->status = (uint8_t)((v->status & 0xA0) | 0x40 | i); fifth = i; }
            extra++;               /* the profiler counts every sprite the line loses */
            continue;
        }
        shown[nshown++] = i;
    }
    if (extra) { v->prof_5s_lines++; v->prof_dropped += (uint32_t)extra; }
    if (fifth < 0 && !(v->status & 0x40)) v->status = (uint8_t)((v->status & 0xE0) | (i > 31 ? 31 : i));

    uint8_t taken[256];
    memset(taken, 0, sizeof taken);
    for (int k = 0; k < nshown; k++) {
        const uint8_t *s = sat + shown[k] * 4;
        int sy = s[0]; if (sy > 0xE0) sy -= 256;
        int row = (y - sy - 1) >> mag;
        int x0 = s[1] - ((s[3] & 0x80) ? 32 : 0);
        uint8_t col = s[3] & 0x0F;
        uint8_t pat = big ? (s[2] & 0xFC) : s[2];
        int w = big ? 16 : 8;
        for (int px = 0; px < (w << mag); px++) {
            int x = x0 + px;
            if (x < 0 || x > 255) continue;
            int bx = px >> mag;
            uint8_t bits = spg[(pat * 8 + (bx >= 8 ? 16 : 0) + row) & 0x7FF];
            if (!(bits & (0x80 >> (bx & 7)))) continue;
            if (taken[x]) { v->status |= 0x20; continue; }
            taken[x] = 1;
            if (col) out[x] = col;
        }
    }
}

static void render_active(TMS9918 *v, int y, uint8_t *out)
{
    uint8_t bd = v->reg[7] & 0x0F;
    const uint8_t *vr = v->vram;
    if (!(v->reg[1] & 0x40)) { memset(out, bd, 256); return; }
    int m1 = v->reg[1] & 0x10, m2 = v->reg[1] & 0x08, m3 = v->reg[0] & 0x02;
    uint16_t nt = (uint16_t)((v->reg[2] & 0x0F) << 10);

    if (m1) { /* text 40x24, 6-pixel characters */
        uint8_t fg = v->reg[7] >> 4, bg = bd;
        uint16_t pg = (uint16_t)((v->reg[4] & 7) << 11);
        memset(out, bd, 256);
        for (int c = 0; c < 40; c++) {
            uint8_t name = vr[(nt + (y >> 3) * 40 + c) & 0x3FFF];
            uint8_t bits = vr[(pg + name * 8 + (y & 7)) & 0x3FFF];
            for (int p = 0; p < 6; p++) {
                uint8_t ci = (bits & (0x80 >> p)) ? fg : bg;
                out[8 + c * 6 + p] = ci ? ci : bd;
            }
        }
        return; /* no sprites in text mode */
    }
    for (int c = 0; c < 32; c++) {
        uint8_t name = vr[(nt + (y >> 3) * 32 + c) & 0x3FFF];
        uint8_t bits, color;
        if (m2) { /* multicolor */
            uint16_t pg = (uint16_t)((v->reg[4] & 7) << 11);
            uint8_t b = vr[(pg + name * 8 + ((y >> 3) & 3) * 2 + ((y >> 2) & 1)) & 0x3FFF];
            bits = 0xF0; color = b;
        } else if (m3) { /* graphics II */
            unsigned idx = (unsigned)((y >> 6) << 11 | name << 3 | (y & 7));
            unsigned pa = ((v->reg[4] & 4u) << 11) | (idx & (((v->reg[4] & 3u) << 11) | 0x7FF));
            unsigned ca = ((v->reg[3] & 0x80u) << 6) | (idx & (((v->reg[3] & 0x7Fu) << 6) | 0x3F));
            bits = vr[pa & 0x3FFF]; color = vr[ca & 0x3FFF];
        } else { /* graphics I */
            uint16_t pg = (uint16_t)((v->reg[4] & 7) << 11);
            bits = vr[(pg + name * 8 + (y & 7)) & 0x3FFF];
            color = vr[((v->reg[3] << 6) + (name >> 3)) & 0x3FFF];
        }
        uint8_t fg = color >> 4, bg = color & 0x0F;
        if (!fg) fg = bd;
        if (!bg) bg = bd;
        for (int p = 0; p < 8; p++) out[c * 8 + p] = (bits & (0x80 >> p)) ? fg : bg;
    }
    draw_sprites(v, y, out);
}

int tms9918_run_line(TMS9918 *v)
{
    int y = v->line, done = 0;
    uint8_t bd = v->reg[7] & 0x0F;
    int row = -1;
    if (y < 192) row = VDP_BORDER_Y + y;
    else if (y < 192 + VDP_BORDER_Y) row = VDP_BORDER_Y + y;
    else if (y >= VDP_LINES - VDP_BORDER_Y) row = y - (VDP_LINES - VDP_BORDER_Y);
    if (row >= 0) {
        uint8_t *p = v->fb + row * VDP_W;
        memset(p, bd, VDP_W);
        if (y < 192) render_active(v, y, p + VDP_BORDER_X);
    }
    if (y == 191) { v->status |= 0x80; update_int(v); }
    if (++v->line >= VDP_LINES) { v->line = 0; v->frame++; done = 1; }
    return done;
}
