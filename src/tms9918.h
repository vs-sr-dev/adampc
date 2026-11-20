/* tms9918.h - TI TMS9918A/9928A Video Display Processor (scanline renderer). */
#ifndef TMS9918_H
#define TMS9918_H
#include <stdint.h>

#define VDP_LINES      262          /* NTSC */
#define VDP_CPU_PER_LINE 228        /* Z80 cycles (3.579545 MHz) per scanline */
/* output picture: 256x192 active area with a border around it */
#define VDP_BORDER_X   16
#define VDP_BORDER_Y   16
#define VDP_W          (256 + 2 * VDP_BORDER_X)
#define VDP_H          (192 + 2 * VDP_BORDER_Y)

typedef struct TMS9918 {
    uint8_t vram[16384];
    uint8_t reg[8];
    uint8_t status;
    uint16_t addr;
    uint8_t latch, latch_full;
    uint8_t readbuf;
    uint8_t int_out;               /* current level of /INT (1 = asserted) */
    int line;                      /* next scanline to render */
    uint8_t fb[VDP_W * VDP_H];     /* palette indices 0..15 */
    uint32_t frame;
    uint32_t prof_5s_lines, prof_dropped;   /* lines with more than 4 sprites, sprites lost */
} TMS9918;

extern const uint32_t tms9918_palette[16];   /* 0xRRGGBB */

void tms9918_reset(TMS9918 *v);
uint8_t tms9918_read(TMS9918 *v, int port);   /* port bit 0: 0 = data, 1 = status */
void tms9918_write(TMS9918 *v, int port, uint8_t val);
/* render scanline v->line and advance; returns 1 when a frame completes */
int tms9918_run_line(TMS9918 *v);

#endif
