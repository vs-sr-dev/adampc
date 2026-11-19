/* z80.h - Zilog Z80 CPU core (instruction-stepped, T-state counted).
 * Implements documented and undocumented behaviour: X/Y flags, MEMPTR (WZ),
 * Q register for SCF/CCF, undocumented opcodes (SLL, IXH/IXL, DDCB copies).
 */
#ifndef Z80_H
#define Z80_H
#include <stdint.h>

typedef union { uint16_t w; struct { uint8_t l, h; } b; } z80_pair;

typedef struct Z80 {
    z80_pair af, bc, de, hl, ix, iy, sp, pc, wz;
    z80_pair af_, bc_, de_, hl_;
    uint8_t i, r;            /* refresh: only bits 0-6 count, bit 7 sticks */
    uint8_t iff1, iff2, im;
    uint8_t halted;
    uint8_t ei_delay;        /* 1 = the previous instruction was EI */
    uint8_t q, qnext;        /* Q register (flags written by last instruction) */
    uint8_t nmi_pending;     /* edge latched */
    uint8_t int_line;        /* level, active high here */
    uint8_t int_data;        /* byte on the data bus during INTA (IM0/IM2) */
    uint64_t cycles;         /* total T-states executed */

    void *ctx;
    uint8_t (*read)(void *ctx, uint16_t addr);
    void    (*write)(void *ctx, uint16_t addr, uint8_t v);
    uint8_t (*in)(void *ctx, uint16_t port);
    void    (*out)(void *ctx, uint16_t port, uint8_t v);
} Z80;

void z80_init(Z80 *z);
void z80_reset(Z80 *z);
/* execute one instruction (or interrupt acknowledge); returns T-states used */
int  z80_step(Z80 *z);
void z80_nmi(Z80 *z);                 /* pulse the NMI line (edge) */
void z80_set_int(Z80 *z, int level);  /* drive /INT (1 = asserted) */
int  z80_disasm(Z80 *z, uint16_t pc, char *buf, int bufsize); /* returns length */

#endif
