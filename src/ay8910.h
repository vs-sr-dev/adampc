/* ay8910.h - General Instrument AY-3-8910 programmable sound generator
 * (the sound chip of the Super Game Module). Written from the GI data sheet:
 * three 12-bit tone generators, one 5-bit noise generator (17-bit shift register),
 * a mixer (R7), 4-bit amplitudes or the envelope (R8-R10 bit 4), a 16-bit envelope
 * period (R11/R12) and the envelope shape (R13: CONT, ATT, ALT, HOLD). */
#ifndef AY8910_H
#define AY8910_H
#include <stdint.h>

typedef struct AY8910 {
    uint8_t reg[16];
    uint8_t addr;                 /* latched register number */
    uint16_t tcount[3];
    uint8_t tout[3];
    uint16_t ncount;
    uint8_t nprescale;
    uint32_t lfsr;
    uint32_t ecount;
    uint8_t estep, eattack, ehold, evol;
    double ticks_per_sample;      /* clock/8 ticks per output sample */
    double acc;
    double dc;                    /* DC blocker state (the chip's outputs are unipolar) */
} AY8910;

void ay8910_init(AY8910 *s, double clock_hz, int sample_rate);
void ay8910_reset(AY8910 *s);
void ay8910_address(AY8910 *s, uint8_t v);
void ay8910_write(AY8910 *s, uint8_t v);
uint8_t ay8910_read(AY8910 *s);
/* render n mono samples (signed 16-bit) */
void ay8910_render(AY8910 *s, int16_t *buf, int n);

#endif
