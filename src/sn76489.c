/* sn76489.c - SN76489A PSG. See sn76489.h. */
#include "sn76489.h"
#include <string.h>
#include <math.h>

static int16_t voltab[16];

void sn76489_init(SN76489 *s, double clock_hz, int sample_rate)
{
    memset(s, 0, sizeof *s);
    for (int i = 0; i < 4; i++) { s->vol[i] = 15; s->counter[i] = 1; }
    s->lfsr = 0x4000;
    s->clock_per_sample = clock_hz / 16.0 / sample_rate;
    for (int i = 0; i < 15; i++) voltab[i] = (int16_t)(8000.0 * pow(10.0, -0.1 * i)); /* 2 dB steps */
    voltab[15] = 0;
}

void sn76489_write(SN76489 *s, uint8_t v)
{
    if (v & 0x80) {
        s->latched = (v >> 4) & 7;
        int ch = s->latched >> 1;
        if (s->latched & 1) s->vol[ch] = v & 0x0F;
        else if (ch < 3) s->tone[ch] = (uint16_t)((s->tone[ch] & 0x3F0) | (v & 0x0F));
        else { s->noise = v & 7; s->lfsr = 0x4000; }
    } else {
        int ch = s->latched >> 1;
        if (s->latched & 1) s->vol[ch] = v & 0x0F;
        else if (ch < 3) s->tone[ch] = (uint16_t)((s->tone[ch] & 0x00F) | ((v & 0x3F) << 4));
        else { s->noise = v & 7; s->lfsr = 0x4000; }
    }
}

static void tick(SN76489 *s)
{
    for (int c = 0; c < 3; c++) {
        if (--s->counter[c] == 0) {
            s->counter[c] = s->tone[c] ? s->tone[c] : 0x400;
            if (s->tone[c] > 1) s->out[c] ^= 1; else s->out[c] = 1;
        }
    }
    if (--s->counter[3] == 0) {
        static const uint16_t rates[3] = { 0x10, 0x20, 0x40 };
        int r = s->noise & 3;
        s->counter[3] = r == 3 ? (uint16_t)(s->tone[2] ? s->tone[2] : 0x400) : rates[r];
        s->out[3] ^= 0x80;                           /* noise shifts on every other toggle */
        if (s->out[3] & 0x80) {
            uint16_t fb = (s->noise & 4) ? ((s->lfsr ^ (s->lfsr >> 1)) & 1) : (s->lfsr & 1);
            s->lfsr = (uint16_t)((s->lfsr >> 1) | (fb << 14));
            s->out[3] = (uint8_t)((s->out[3] & 0x80) | (s->lfsr & 1));
        }
    }
}

void sn76489_render(SN76489 *s, int16_t *buf, int n)
{
    for (int i = 0; i < n; i++) {
        s->acc += s->clock_per_sample;
        /* average the outputs over the chip ticks of this sample */
        int ticks = (int)s->acc;
        s->acc -= ticks;
        int32_t sum = 0;
        for (int t = 0; t < ticks; t++) {
            tick(s);
            int32_t v = 0;
            for (int c = 0; c < 3; c++) v += s->out[c] ? voltab[s->vol[c]] : -voltab[s->vol[c]];
            v += (s->out[3] & 1) ? voltab[s->vol[3]] : -voltab[s->vol[3]];
            sum += v;
        }
        buf[i] = (int16_t)(ticks ? sum / ticks / 2 : 0);
    }
}
