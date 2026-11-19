/* sn76489.h - TI SN76489A programmable sound generator. */
#ifndef SN76489_H
#define SN76489_H
#include <stdint.h>

typedef struct SN76489 {
    uint16_t tone[3];        /* 10-bit periods */
    uint8_t vol[4];          /* attenuation 0..15 (15 = off) */
    uint8_t noise;           /* noise control (3 bits) */
    uint8_t latched;         /* latched register 0..7 */
    uint16_t counter[4];
    uint8_t out[4];          /* current flip-flop outputs */
    uint16_t lfsr;
    double clock_per_sample; /* chip ticks (clock/16) per output sample */
    double acc;
} SN76489;

void sn76489_init(SN76489 *s, double clock_hz, int sample_rate);
void sn76489_write(SN76489 *s, uint8_t v);
/* render n mono samples (signed 16-bit) */
void sn76489_render(SN76489 *s, int16_t *buf, int n);

#endif
