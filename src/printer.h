/* printer.h - SmartWriter daisy-wheel printer mechanics, as driven by its 6801.
 *
 * P1 high nibble: 4-phase stepper of the daisy wheel (96 petals), P1 low nibble: carriage
 * stepper (2 steps per column). P4 out: b0 hammer (0 = strike), b1 ribbon, b2/b3 platen DC
 * motor (b2 low = forward, b3 low = brake). P4 in: b4 carriage home, b5 platen cam,
 * b6 wheel home, b7 1 = normal mode (0 = self test). P3 in: speed trims (all high).
 * The printed character is the petal facing the paper when the hammer falls.
 */
#ifndef PRINTER_H
#define PRINTER_H
#include <stdint.h>
#include <stdio.h>

#define PAGE_ROWS 4000       /* half-lines kept: continuous paper, 2000 lines */
#define PAGE_COLS 96

typedef struct {
    uint8_t p1, p4;
    int carriage;            /* steps from the home edge (home zone: <= 0) */
    int wheel;               /* petal 0..95 facing the paper */
    int64_t platen;          /* Âµs of forward platen drive */
    uint64_t last;
    int petal_char[96];
    char page[PAGE_ROWS][PAGE_COLS + 1];
    int used_rows;
    int strikes;
    /* every hammer blow, for the page image: half-line, carriage step, character */
    struct PrnStrike { int32_t row; int16_t step; uint8_t ch, pad; } *log;
    int nlog, caplog;
    const uint8_t *font;     /* 8x8 font for ASCII 0x20-0x7E (SmartWriter ROM) */
    /* mechanical sound: events stamped in E cycles (µs), mixed into the frame's audio */
    struct PrnEvent { uint64_t t; uint8_t kind; } ev[4096];
    int nev;
    int click_n, click_len, click_amp, hum;
    uint32_t noise;
    int sound;               /* 1 = mix the mechanism noise into the audio */
    int trace;
    char path[1024];
} Printer;

void printer_init(Printer *p, const uint8_t *rom2k, const uint8_t *font);
uint8_t printer_port_in(Printer *p, int port, uint64_t now);
void printer_port_out(Printer *p, int port, uint8_t data, uint8_t ddr, uint64_t now);
int printer_save(Printer *p);           /* write the page as text (if anything was printed) */
int printer_save_png(Printer *p, const char *path);
/* add the mechanism noise for [t0_us, t1_us) to n samples (events after t1 are kept) */
void printer_mix(Printer *p, int16_t *buf, int n, uint64_t t0_us, uint64_t t1_us);   /* the page as an image, ink where the petals hit */

#endif
