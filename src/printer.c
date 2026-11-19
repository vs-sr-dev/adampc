/* printer.c - SmartWriter printer mechanics. See printer.h and docs/RECON.md. */
#include "printer.h"
#include "png.h"
#include <stdlib.h>
#include <string.h>

/* wheel home flag: homing steps forward out of the flag, then keeps stepping forward (a full
 * turn) until the flag comes back, and calls that petal 5: so the flag starts at petal 5 */
#define WHEEL_HOME_FIRST 5
#define WHEEL_HOME_LAST  10
#define HALFLINE_US    60000      /* forward drive needed for one platen cam cycle (half line) */

static uint8_t rol4(uint8_t v) { return (uint8_t)(((v << 1) | (v >> 3)) & 0x0F); }
static uint8_t ror4(uint8_t v) { return (uint8_t)(((v >> 1) | (v << 3)) & 0x0F); }

void printer_init(Printer *p, const uint8_t *rom, const uint8_t *font)
{
    char path[1024];
    int trace = p->trace, sound = p->sound;
    memcpy(path, p->path, sizeof path);
    free(p->log);
    memset(p, 0, sizeof *p);
    p->trace = trace; p->sound = sound;
    p->font = font;
    memcpy(p->path, path, sizeof path);
    p->p1 = 0xCC; p->p4 = 0x0F;
    p->carriage = 37;         /* wherever the carriage was left at power off */
    p->wheel = 50;
    /* petal of each character, from the firmware's table at FCF7 (indexed by the code) */
    for (int i = 0; i < 96; i++) p->petal_char[i] = '?';
    for (int c = 0x21; c < 0x7F; c++) {
        int petal = rom[0xFCF7 - 0xF800 + c];
        if (petal >= 0 && petal < 96) p->petal_char[petal] = c;
    }
    for (int r = 0; r < PAGE_ROWS; r++) { memset(p->page[r], ' ', PAGE_COLS); p->page[r][PAGE_COLS] = 0; }
}

static int platen_forward(const Printer *p) { return !(p->p4 & 0x04) && (p->p4 & 0x08); }

static void advance(Printer *p, uint64_t now)
{
    if (now > p->last && platen_forward(p)) p->platen += (int64_t)(now - p->last);
    p->last = now;
}

static int halfline(const Printer *p) { return (int)((p->platen + HALFLINE_US / 4) / HALFLINE_US); }

uint8_t printer_port_in(Printer *p, int port, uint64_t now)
{
    if (port == 4) {
        advance(p, now);
        uint8_t v = 0x8F;                                       /* b7: normal mode */
        if (p->carriage <= 0) v |= 0x10;
        int64_t phase = p->platen % HALFLINE_US;
        if (phase < HALFLINE_US / 4 || phase >= HALFLINE_US * 3 / 4) v |= 0x20;
        if (p->wheel >= WHEEL_HOME_FIRST && p->wheel <= WHEEL_HOME_LAST) v |= 0x40;
        return v;
    }
    return 0xFF;                                                /* P3 speed trims: none */
}

static void strike(Printer *p)
{
    int row = halfline(p), col = p->carriage / 2;
    int c = p->petal_char[p->wheel];
    p->strikes++;
    if (p->trace) fprintf(stderr, "[prn] strike '%c' (petal %d) at row %d col %d\n", c, p->wheel, row, col);
    if (p->nlog == p->caplog) {
        int cap = p->caplog ? p->caplog * 2 : 4096;
        struct PrnStrike *n = realloc(p->log, sizeof *n * (size_t)cap);
        if (n) { p->log = n; p->caplog = cap; }
    }
    if (p->nlog < p->caplog)
        p->log[p->nlog++] = (struct PrnStrike){ row, (int16_t)p->carriage, (uint8_t)c, 0 };
    if (row < 0 || row >= PAGE_ROWS || col < 0 || col >= PAGE_COLS) return;
    p->page[row][col] = (char)c;
    if (row + 1 > p->used_rows) p->used_rows = row + 1;
}

enum { EV_STRIKE, EV_WHEEL, EV_CARRIAGE, EV_PLATEN_ON, EV_PLATEN_OFF };

static void event(Printer *p, uint64_t now, int kind)
{
    if (p->sound && p->nev < (int)(sizeof p->ev / sizeof p->ev[0]))
        p->ev[p->nev++] = (struct PrnEvent){ now, (uint8_t)kind };
}

void printer_port_out(Printer *p, int port, uint8_t data, uint8_t ddr, uint64_t now)
{
    if (port == 1) {
        uint8_t v = (uint8_t)(data | ~ddr);
        uint8_t lo = v & 0x0F, olo = p->p1 & 0x0F, hi = v >> 4, ohi = p->p1 >> 4;
        if (lo != olo) {
            if (lo == rol4(olo)) p->carriage++;
            else if (lo == ror4(olo)) p->carriage--;
            event(p, now, EV_CARRIAGE);
        }
        if (hi != ohi) {
            if (hi == rol4(ohi)) p->wheel = (p->wheel + 1) % 96;
            else if (hi == ror4(ohi)) p->wheel = (p->wheel + 95) % 96;
            event(p, now, EV_WHEEL);
        }
        p->p1 = v;
    } else if (port == 4) {
        uint8_t v = (uint8_t)((data & ddr) | (0x0F & ~ddr) | (p->p4 & 0xF0));
        advance(p, now);
        if ((p->p4 & 0x01) && !(v & 0x01)) { strike(p); event(p, now, EV_STRIKE); }
        int was = platen_forward(p);
        p->p4 = (uint8_t)(v & 0x0F);
        if (platen_forward(p) != was) event(p, now, was ? EV_PLATEN_OFF : EV_PLATEN_ON);
    }
}

/* Mechanism noise, not a recording: a sharp decaying noise burst for the hammer, short
 * ticks for each stepper step (the wheel's whirr is many ticks close together), and a
 * low buzz while the platen motor drives. */
void printer_mix(Printer *p, int16_t *buf, int n, uint64_t t0, uint64_t t1)
{
    int e = 0;
    if (!p->sound) { p->nev = 0; return; }
    for (int i = 0; i < n; i++) {
        uint64_t t = t0 + (t1 - t0) * (uint64_t)i / (uint64_t)n;
        for (; e < p->nev && p->ev[e].t <= t; e++) {
            int len = 0, amp = 0;
            switch (p->ev[e].kind) {
            case EV_STRIKE:     len = 180; amp = 11000; break;   /* ~4 ms */
            case EV_WHEEL:      len = 18;  amp = 1400;  break;
            case EV_CARRIAGE:   len = 22;  amp = 1800;  break;
            case EV_PLATEN_ON:  p->hum = 1; break;
            case EV_PLATEN_OFF: p->hum = 0; break;
            }
            if (amp && amp * len >= p->click_amp * p->click_n) { p->click_n = p->click_len = len; p->click_amp = amp; }
        }
        int s = 0;
        p->noise = p->noise * 1664525u + 1013904223u;
        int r = (int)(p->noise >> 16) - 32768;                 /* white noise, +-32K */
        if (p->click_n > 0) {
            int env = p->click_amp * p->click_n / p->click_len;
            s += r * env / 32768;
            p->click_n--;
        }
        if (p->hum) s += ((t / 4000) & 1) ? 500 : -500;          /* 125 Hz buzz */
        int v = buf[i] + s;
        buf[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    /* keep the events that belong to the next frame */
    int k = 0;
    for (; e < p->nev; e++) p->ev[k++] = p->ev[e];
    p->nev = k;
}

int printer_save(Printer *p)
{
    if (!p->strikes || !p->path[0]) return 1;
    FILE *f = fopen(p->path, "w");
    if (!f) return 0;
    /* two half-lines per text line; a character struck on either half lands on that line */
    for (int r = 0; r < p->used_rows; r += 2) {
        char line[PAGE_COLS + 1];
        for (int c = 0; c < PAGE_COLS; c++) {
            char a = p->page[r][c], b = r + 1 < PAGE_ROWS ? p->page[r + 1][c] : ' ';
            line[c] = a != ' ' ? a : b;
        }
        int n = PAGE_COLS;
        while (n > 0 && line[n - 1] == ' ') n--;
        line[n] = 0;
        fprintf(f, "%s\n", line);
    }
    fclose(f);
    return 1;
}

/* Page image: a column is 2 carriage steps = 12 px, a half-line 12 px, glyphs are the 5x7
 * screen font at 2x. Every strike multiplies the paper by the ink, so overstrikes (bold,
 * underline, typed-over corrections) come out darker, as on paper. */
#define PX_STEP   6
#define PX_HALF   12
#define MARGIN    24

int printer_save_png(Printer *p, const char *path)
{
    if (!p->nlog || !path || !path[0]) return 1;
    int minrow = p->log[0].row, maxrow = minrow, maxstep = 0;
    for (int i = 0; i < p->nlog; i++) {
        if (p->log[i].row < minrow) minrow = p->log[i].row;
        if (p->log[i].row > maxrow) maxrow = p->log[i].row;
        if (p->log[i].step > maxstep) maxstep = p->log[i].step;
    }
    if (minrow > 0) minrow = 0;                          /* keep the top of the sheet */
    int w = MARGIN * 2 + (maxstep < 2 * 80 ? 2 * 80 : maxstep + 2) * PX_STEP;
    int h = MARGIN * 2 + (maxrow - minrow + 2) * PX_HALF;
    if (h > 60000) h = 60000;
    uint8_t *rgb = malloc((size_t)w * (size_t)h * 3);
    if (!rgb) return 0;
    for (size_t i = 0; i < (size_t)w * (size_t)h; i++) { rgb[i * 3] = 250; rgb[i * 3 + 1] = 248; rgb[i * 3 + 2] = 240; }
    for (int i = 0; i < p->nlog; i++) {
        int c = p->log[i].ch;
        if (c < 0x20 || c > 0x7E || !p->font) continue;
        const uint8_t *g = p->font + (c - 0x20) * 8;
        int x0 = MARGIN + p->log[i].step * PX_STEP, y0 = MARGIN + (p->log[i].row - minrow) * PX_HALF;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 12; x++) {
                if (!(g[y / 2] & (0x80 >> (x / 2)))) continue;
                int px = x0 + x, py = y0 + y;
                if (px < 0 || px >= w || py < 0 || py >= h) continue;
                uint8_t *q = rgb + ((size_t)py * (size_t)w + (size_t)px) * 3;
                q[0] = (uint8_t)(q[0] * 30 / 100); q[1] = (uint8_t)(q[1] * 30 / 100); q[2] = (uint8_t)(q[2] * 38 / 100);
            }
    }
    int ok = png_write_rgb(path, rgb, w, h);
    free(rgb);
    return ok;
}
