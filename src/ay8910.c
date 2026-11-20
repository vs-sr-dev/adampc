/* ay8910.c - AY-3-8910 PSG. See ay8910.h.
 *
 * Frequencies from the data sheet: tone fT = fclock / (16 TP), noise fN = fclock / (16 NP),
 * envelope fE = fclock / (256 EP) for a whole 16-step ramp. Here everything is clocked at
 * fclock / 8: a tone output flips every TP ticks, the noise register shifts every 2 NP ticks,
 * the envelope moves one step every 2 EP ticks. */
#include "ay8910.h"
#include <string.h>
#include <math.h>

static const uint8_t regmask[16] = {
    0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0xFF, 0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF
};
static int32_t voltab[16];

void ay8910_reset(AY8910 *s)
{
    memset(s->reg, 0, sizeof s->reg);
    s->addr = 0;
    for (int c = 0; c < 3; c++) { s->tcount[c] = 0; s->tout[c] = 0; }
    s->ncount = 0; s->nprescale = 0; s->lfsr = 1;
    s->ecount = 0; s->estep = 0; s->eattack = 0; s->ehold = 1; s->evol = 0;
}

void ay8910_init(AY8910 *s, double clock_hz, int sample_rate)
{
    memset(s, 0, sizeof *s);
    s->ticks_per_sample = clock_hz / 8.0 / sample_rate;
    /* 16 amplitude steps, about 3 dB apart; 0 is silence */
    voltab[0] = 0;
    for (int i = 1; i < 16; i++) voltab[i] = (int32_t)(8000.0 * pow(10.0, -(15 - i) * 3.0 / 20.0));
    ay8910_reset(s);
}

void ay8910_address(AY8910 *s, uint8_t v) { s->addr = v; }

void ay8910_write(AY8910 *s, uint8_t v)
{
    if (s->addr > 15) return;                 /* the chip ignores addresses 16-255 */
    int r = s->addr;
    s->reg[r] = v & regmask[r];
    if (r == 13) {                             /* a new shape restarts the envelope */
        s->ecount = 0; s->estep = 0; s->ehold = 0;
        s->eattack = (v & 4) ? 1 : 0;
        s->evol = s->eattack ? 0 : 15;
    }
}

uint8_t ay8910_read(AY8910 *s)
{
    if (s->addr > 15) return 0xFF;
    /* I/O ports A/B set as inputs (R7 bits 6/7 = 0) read the pins: nothing connected */
    if (s->addr == 14 && !(s->reg[7] & 0x40)) return 0xFF;
    if (s->addr == 15 && !(s->reg[7] & 0x80)) return 0xFF;
    return s->reg[s->addr];
}

static void envelope_step(AY8910 *s)
{
    if (s->ehold) return;
    if (++s->estep < 16) {
        s->evol = s->eattack ? s->estep : (uint8_t)(15 - s->estep);
        return;
    }
    uint8_t shape = s->reg[13];
    if (!(shape & 8)) { s->ehold = 1; s->evol = 0; return; }           /* CONT = 0: one ramp, then 0 */
    if (shape & 1) {                                                    /* HOLD */
        s->ehold = 1;
        s->evol = (((shape >> 2) ^ (shape >> 1)) & 1) ? 15 : 0;          /* ATT xor ALT */
        return;
    }
    if (shape & 2) s->eattack ^= 1;                                      /* ALT: next ramp reversed */
    s->estep = 0;
    s->evol = s->eattack ? 0 : 15;
}

static void tick(AY8910 *s)
{
    for (int c = 0; c < 3; c++) {
        uint16_t tp = (uint16_t)(s->reg[c * 2] | (s->reg[c * 2 + 1] << 8));
        if (!tp) tp = 1;
        if (++s->tcount[c] >= tp) { s->tcount[c] = 0; s->tout[c] ^= 1; }
    }
    s->nprescale ^= 1;
    if (!s->nprescale) {
        uint8_t np = s->reg[6] ? s->reg[6] : 1;
        if (++s->ncount >= np) {
            s->ncount = 0;
            uint32_t fb = (s->lfsr ^ (s->lfsr >> 3)) & 1;
            s->lfsr = (s->lfsr >> 1) | (fb << 16);
        }
        uint32_t ep = (uint32_t)(s->reg[11] | (s->reg[12] << 8));
        if (!ep) ep = 1;
        if (++s->ecount >= ep) { s->ecount = 0; envelope_step(s); }
    }
}

void ay8910_render(AY8910 *s, int16_t *buf, int n)
{
    for (int i = 0; i < n; i++) {
        s->acc += s->ticks_per_sample;
        int ticks = (int)s->acc;
        s->acc -= ticks;
        int32_t sum = 0;
        for (int t = 0; t < ticks; t++) {
            tick(s);
            uint8_t mix = s->reg[7], nout = (uint8_t)(s->lfsr & 1);
            for (int c = 0; c < 3; c++) {
                int on = (s->tout[c] | ((mix >> c) & 1)) & (nout | ((mix >> (c + 3)) & 1));
                if (!on) continue;
                uint8_t amp = s->reg[8 + c];
                sum += voltab[(amp & 0x10) ? s->evol : (amp & 0x0F)];
            }
        }
        double v = ticks ? (double)sum / ticks : 0.0;
        /* one-pole DC blocker: the outputs swing between 0 and +A */
        s->dc += (v - s->dc) * 0.0005;      /* about 3.5 Hz at 44.1 kHz */
        double o = (v - s->dc) * 0.5;
        if (o > 32767) o = 32767;
        if (o < -32768) o = -32768;
        buf[i] = (int16_t)o;
    }
}
