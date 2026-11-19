/* m6801.h - Motorola MC6801/6803 microcontroller core.
 * CPU + on-chip peripherals: ports 1-4 (with port 3 handshake strobes),
 * 16-bit timer (FRC/OCR/ICR), SCI, 128 bytes RAM.
 * The on-chip ROM and any external memory are supplied by the host through read/write.
 */
#ifndef M6801_H
#define M6801_H
#include <stdint.h>

typedef struct M6801 M6801;

struct M6801 {
    /* CPU */
    uint8_t a, b, cc;
    uint16_t x, sp, pc;
    uint8_t wai;              /* waiting in WAI */
    uint8_t mode;             /* operating mode latched from P20-P22 at reset */
    uint64_t cycles;          /* E cycles executed */
    int clock_shift;          /* E = 1 MHz << clock_shift (the board's crystal) */

    /* on-chip */
    uint8_t ram[128];
    uint8_t ramcr;
    uint8_t ddr[4], dout[4];  /* ports 1-4: data direction and output latches */
    uint8_t p3csr, p3latch, p3latched, p3csr_seen;

    uint16_t frc, ocr, icr;
    uint8_t tcsr, tcsr_seen;  /* flags seen by a TCSR read (for the clear sequence) */
    uint8_t frc_lo_latch;
    uint8_t p20_level, p21_out;

    uint8_t rmcr, trcsr, trcsr_seen, rdr, tdr;
    uint8_t tdr_full;         /* TDR holds a byte not yet moved to the shift register */
    uint8_t tx_busy;
    uint64_t tx_end;          /* E cycle when the frame/preamble on the line ends */
    uint64_t wu_set_cycle;    /* E cycle when WU was last set */

    uint8_t irq1;             /* external /IRQ1 level (1 = asserted) */
    uint8_t nmi_pending;

    /* host interface */
    void *ctx;
    uint8_t (*read)(void *ctx, uint16_t addr);           /* everything not on chip */
    void    (*write)(void *ctx, uint16_t addr, uint8_t v);
    uint8_t (*port_in)(void *ctx, int port);              /* pin levels, port 1..4 */
    void    (*port_out)(void *ctx, int port, uint8_t data, uint8_t ddr);
    void    (*os3)(void *ctx);                            /* port 3 output strobe */
    void    (*sci_tx)(void *ctx, uint8_t byte, uint64_t start, uint64_t end); /* E cycles */
    void    (*sci_poll)(void *ctx);                       /* deliver due received frames */
    uint64_t rx_due;          /* host sets: cycle of next frame end to deliver (or ~0) */
};

void m6801_init(M6801 *m, uint8_t mode);
void m6801_reset(M6801 *m);
int  m6801_step(M6801 *m);                   /* returns E cycles */
void m6801_set_irq1(M6801 *m, int level);
void m6801_nmi(M6801 *m);
void m6801_is3(M6801 *m, uint8_t data);       /* input strobe on port 3 (latches data) */
void m6801_set_p20(M6801 *m, int level);      /* timer input capture pin */
/* SCI receive: called by the host when a frame ends on the line.
 * Returns 1 if the receiver took it. */
int  m6801_sci_receive(M6801 *m, uint8_t byte, uint64_t frame_start, uint64_t idle_since);
unsigned m6801_bit_cycles(const M6801 *m);    /* E cycles per SCI bit */
int  m6801_disasm(M6801 *m, uint16_t pc, char *buf, int bufsize);

/* TRCSR bits */
enum { SCI_WU = 0x01, SCI_TE = 0x02, SCI_TIE = 0x04, SCI_RE = 0x08, SCI_RIE = 0x10,
       SCI_TDRE = 0x20, SCI_ORFE = 0x40, SCI_RDRF = 0x80 };

#endif
