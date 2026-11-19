/* m6801.c - Motorola MC6801/6803 core with on-chip peripherals. See m6801.h. */
#include "m6801.h"
#include <string.h>
#include <stdio.h>

enum { CF = 0x01, VF = 0x02, ZF = 0x04, NF = 0x08, IF = 0x10, HF = 0x20 };
enum { TCSR_OLVL = 0x01, TCSR_IEDG = 0x02, TCSR_ETOI = 0x04, TCSR_EOCI = 0x08, TCSR_EICI = 0x10,
       TCSR_TOF = 0x20, TCSR_OCF = 0x40, TCSR_ICF = 0x80 };
enum { P3_LATCH = 0x08, P3_OSS = 0x10, P3_IS3IE = 0x40, P3_IS3F = 0x80 };


/* ------------------------------------------------------------- ports */
static uint8_t port_pins(M6801 *m, int n) { return m->port_in ? m->port_in(m->ctx, n) : 0xFF; }

static uint8_t port_outval(M6801 *m, int n)
{
    uint8_t v = m->dout[n - 1];
    if (n == 2 && (m->ddr[1] & 0x02)) v = (uint8_t)((v & ~0x02) | (m->p21_out ? 0x02 : 0));
    return v;
}
static void port_changed(M6801 *m, int n)
{
    if (m->port_out) m->port_out(m->ctx, n, port_outval(m, n), m->ddr[n - 1]);
}

unsigned m6801_bit_cycles(const M6801 *m)
{
    static const unsigned div[4] = { 16, 128, 1024, 4096 };
    /* RMCR CC = 11: the bit clock comes from P2.2; on the ADAM boards that use it (1.44M
     * disk, E = 2 MHz) it keeps AdamNet at 62.5 kbit/s: 16 µs per bit */
    if ((m->rmcr & 0x0C) == 0x0C) return 16u << m->clock_shift;
    return div[m->rmcr & 3];
}

/* ------------------------------------------------------------- SCI */
static void sci_update(M6801 *m)
{
    for (;;) {
        uint64_t start = m->cycles;
        if (m->tx_busy) {
            if (m->cycles < m->tx_end) return;
            m->tx_busy = 0;
            start = m->tx_end;
        }
        if (m->tdr_full && (m->trcsr & SCI_TE)) {
            m->tdr_full = 0;
            m->tx_busy = 1;
            m->tx_end = start + 10ull * m6801_bit_cycles(m);
            if (m->sci_tx) m->sci_tx(m->ctx, m->tdr, start, m->tx_end);
            continue;
        }
        return;
    }
}

int m6801_sci_receive(M6801 *m, uint8_t byte, uint64_t frame_start, uint64_t idle_since)
{
    if (!(m->trcsr & SCI_RE)) return 0;
    if (m->trcsr & SCI_WU) {
        uint64_t from = idle_since > m->wu_set_cycle ? idle_since : m->wu_set_cycle;
        if (frame_start >= from + 10ull * m6801_bit_cycles(m)) m->trcsr &= (uint8_t)~SCI_WU;
        else return 0;
    }
    if (m->trcsr & SCI_RDRF) { m->trcsr |= SCI_ORFE; return 1; }
    m->rdr = byte;
    m->trcsr |= SCI_RDRF;
    return 1;
}

/* ------------------------------------------------------------- timer */
static void timer_advance(M6801 *m, unsigned n)
{
    uint16_t old = m->frc;
    if ((uint16_t)(m->ocr - old - 1) < n) {
        m->tcsr |= TCSR_OCF;
        uint8_t lvl = m->tcsr & TCSR_OLVL;
        if (lvl != m->p21_out) { m->p21_out = lvl; if (m->ddr[1] & 0x02) port_changed(m, 2); }
    }
    if ((unsigned)old + n > 0xFFFF) m->tcsr |= TCSR_TOF;
    m->frc = (uint16_t)(old + n);
}

void m6801_set_p20(M6801 *m, int level)
{
    level = level != 0;
    if (level == m->p20_level) return;
    m->p20_level = (uint8_t)level;
    if (level == ((m->tcsr & TCSR_IEDG) ? 1 : 0)) {
        m->icr = m->frc;
        m->tcsr |= TCSR_ICF;
    }
}

void m6801_is3(M6801 *m, uint8_t data)
{
    if ((m->p3csr & P3_LATCH) && !m->p3latched) { m->p3latch = data; m->p3latched = 1; }
    else if (!(m->p3csr & P3_LATCH)) m->p3latch = data;
    m->p3csr |= P3_IS3F;
}

void m6801_set_irq1(M6801 *m, int level) { m->irq1 = (uint8_t)(level != 0); }
void m6801_nmi(M6801 *m) { m->nmi_pending = 1; }

/* ------------------------------------------------------------- memory */
static uint8_t reg_read(M6801 *m, uint8_t r)
{
    switch (r) {
    case 0x00: case 0x01: case 0x04: case 0x05: return m->ddr[r == 0 ? 0 : r == 1 ? 1 : r == 4 ? 2 : 3];
    case 0x02: return (uint8_t)((m->dout[0] & m->ddr[0]) | (port_pins(m, 1) & ~m->ddr[0]));
    case 0x03: {
        uint8_t v = (uint8_t)((port_outval(m, 2) & m->ddr[1]) | (port_pins(m, 2) & ~m->ddr[1]));
        return (uint8_t)((v & 0x1F) | ((m->mode & 7) << 5));
    }
    case 0x06: {
        uint8_t in = (m->p3csr & P3_LATCH) && m->p3latched ? m->p3latch : port_pins(m, 3);
        uint8_t v = (uint8_t)((m->dout[2] & m->ddr[2]) | (in & ~m->ddr[2]));
        m->p3latched = 0;
        if (m->p3csr_seen && (m->p3csr & P3_IS3F)) m->p3csr &= (uint8_t)~P3_IS3F;
        m->p3csr_seen = 0;
        if (!(m->p3csr & P3_OSS) && m->os3) m->os3(m->ctx);
        return v;
    }
    case 0x07: return (uint8_t)((m->dout[3] & m->ddr[3]) | (port_pins(m, 4) & ~m->ddr[3]));
    case 0x08: m->tcsr_seen = m->tcsr & 0xE0; return m->tcsr;
    case 0x09:
        if (m->tcsr_seen & TCSR_TOF) { m->tcsr &= (uint8_t)~TCSR_TOF; m->tcsr_seen &= (uint8_t)~TCSR_TOF; }
        m->frc_lo_latch = (uint8_t)m->frc;
        return (uint8_t)(m->frc >> 8);
    case 0x0A: return m->frc_lo_latch;
    case 0x0B: return (uint8_t)(m->ocr >> 8);
    case 0x0C: return (uint8_t)m->ocr;
    case 0x0D:
        if (m->tcsr_seen & TCSR_ICF) { m->tcsr &= (uint8_t)~TCSR_ICF; m->tcsr_seen &= (uint8_t)~TCSR_ICF; }
        return (uint8_t)(m->icr >> 8);
    case 0x0E: return (uint8_t)m->icr;
    case 0x0F: m->p3csr_seen = (m->p3csr & P3_IS3F) != 0; return m->p3csr;
    case 0x10: return (uint8_t)(m->rmcr | 0xF0);
    case 0x11: {
        uint8_t v = (uint8_t)((m->trcsr & ~SCI_TDRE) | (m->tdr_full ? 0 : SCI_TDRE));
        m->trcsr_seen = v & 0xE0;
        return v;
    }
    case 0x12:
        if (m->trcsr_seen & (SCI_RDRF | SCI_ORFE)) {
            m->trcsr &= (uint8_t)~(SCI_RDRF | SCI_ORFE);
            m->trcsr_seen &= (uint8_t)~(SCI_RDRF | SCI_ORFE);
        }
        return m->rdr;
    case 0x13: return m->tdr;
    case 0x14: return (uint8_t)(m->ramcr | 0x3F);
    default: return 0xFF;
    }
}

static void reg_write(M6801 *m, uint8_t r, uint8_t v)
{
    switch (r) {
    case 0x00: m->ddr[0] = v; port_changed(m, 1); break;
    case 0x01: m->ddr[1] = v; port_changed(m, 2); break;
    case 0x04: m->ddr[2] = v; port_changed(m, 3); break;
    case 0x05: m->ddr[3] = v; port_changed(m, 4); break;
    case 0x02: m->dout[0] = v; port_changed(m, 1); break;
    case 0x03: m->dout[1] = v; port_changed(m, 2); break;
    case 0x06:
        m->dout[2] = v; port_changed(m, 3);
        if (m->p3csr_seen && (m->p3csr & P3_IS3F)) m->p3csr &= (uint8_t)~P3_IS3F;
        m->p3csr_seen = 0;
        if ((m->p3csr & P3_OSS) && m->os3) m->os3(m->ctx);
        break;
    case 0x07: m->dout[3] = v; port_changed(m, 4); break;
    case 0x08: {
        uint8_t old = m->tcsr;
        m->tcsr = (uint8_t)((m->tcsr & 0xE0) | (v & 0x1F));
        (void)old;
        break;
    }
    case 0x09: case 0x0A: m->frc = 0xFFF8; break;
    case 0x0B: case 0x0C:
        if (r == 0x0B) m->ocr = (uint16_t)((m->ocr & 0x00FF) | v << 8);
        else m->ocr = (uint16_t)((m->ocr & 0xFF00) | v);
        if (m->tcsr_seen & TCSR_OCF) { m->tcsr &= (uint8_t)~TCSR_OCF; m->tcsr_seen &= (uint8_t)~TCSR_OCF; }
        break;
    case 0x0F: m->p3csr = (uint8_t)((m->p3csr & P3_IS3F) | (v & 0x58)); break;
    case 0x10: m->rmcr = v & 0x0F; break;
    case 0x11: {
        uint8_t old = m->trcsr;
        m->trcsr = (uint8_t)((m->trcsr & 0xC0) | (v & 0x1F));
        if ((v & SCI_TE) && !(old & SCI_TE) && !m->tx_busy) {
            /* enabling the transmitter sends one idle frame (preamble) first */
            m->tx_busy = 1;
            m->tx_end = m->cycles + 10ull * m6801_bit_cycles(m);
        }
        if (v & SCI_WU) m->wu_set_cycle = m->cycles;
        break;
    }
    case 0x13: m->tdr = v; m->tdr_full = 1; break;
    case 0x14: m->ramcr = v & 0xC0; break;
    default: break;
    }
}

static inline uint8_t rd(M6801 *m, uint16_t a)
{
    if (a < 0x20) return reg_read(m, (uint8_t)a);
    if (a >= 0x80 && a < 0x100 && (m->ramcr & 0x40)) return m->ram[a - 0x80];
    return m->read(m->ctx, a);
}
static inline void wr(M6801 *m, uint16_t a, uint8_t v)
{
    if (a < 0x20) { reg_write(m, (uint8_t)a, v); return; }
    if (a >= 0x80 && a < 0x100 && (m->ramcr & 0x40)) { m->ram[a - 0x80] = v; return; }
    m->write(m->ctx, a, v);
}
static inline uint16_t rd16(M6801 *m, uint16_t a) { uint8_t h = rd(m, a); return (uint16_t)(h << 8 | rd(m, (uint16_t)(a + 1))); }
static inline void wr16(M6801 *m, uint16_t a, uint16_t v) { wr(m, a, (uint8_t)(v >> 8)); wr(m, (uint16_t)(a + 1), (uint8_t)v); }
static inline uint8_t fetch(M6801 *m) { return rd(m, m->pc++); }
static inline uint16_t fetch16(M6801 *m) { uint16_t v = rd16(m, m->pc); m->pc += 2; return v; }
static inline void push8(M6801 *m, uint8_t v) { wr(m, m->sp--, v); }
static inline uint8_t pull8(M6801 *m) { return rd(m, ++m->sp); }
static inline void push16(M6801 *m, uint16_t v) { push8(m, (uint8_t)v); push8(m, (uint8_t)(v >> 8)); }
static inline uint16_t pull16(M6801 *m) { uint8_t h = pull8(m); return (uint16_t)(h << 8 | pull8(m)); }

/* ------------------------------------------------------------- init */
void m6801_init(M6801 *m, uint8_t mode)
{
    m->mode = mode;
    memset(m->ram, 0, sizeof m->ram);
    m6801_reset(m);
}

void m6801_reset(M6801 *m)
{
    m->a = m->b = 0; m->x = 0; m->sp = 0;
    m->cc = 0xC0 | IF;
    memset(m->ddr, 0, sizeof m->ddr);
    memset(m->dout, 0, sizeof m->dout);
    m->p3csr = 0; m->p3latched = 0;
    m->ramcr = 0x40;          /* RAM enabled */
    m->frc = 0; m->ocr = 0xFFFF; m->icr = 0; m->tcsr = 0; m->tcsr_seen = 0;
    m->p21_out = 0;
    m->rmcr = 0; m->trcsr = 0; m->trcsr_seen = 0; m->tdr_full = 0; m->tx_busy = 0;
    m->wai = 0; m->nmi_pending = 0;
    m->rx_due = ~0ull;
    m->pc = rd16(m, 0xFFFE);
}

/* ------------------------------------------------------------- flags */
#define SETNZ8(v)  m->cc = (uint8_t)((m->cc & ~(NF | ZF)) | (((v) & 0x80) ? NF : 0) | (((v) & 0xFF) ? 0 : ZF))
#define SETNZ16(v) m->cc = (uint8_t)((m->cc & ~(NF | ZF)) | (((v) & 0x8000) ? NF : 0) | (((v) & 0xFFFF) ? 0 : ZF))

static uint8_t add8(M6801 *m, uint8_t a, uint8_t b, int c)
{
    unsigned r = (unsigned)a + b + c;
    m->cc = (uint8_t)(m->cc & ~(HF | NF | ZF | VF | CF));
    m->cc |= (uint8_t)((((a ^ b ^ r) & 0x10) << 1) | ((((~(a ^ b)) & (a ^ r)) & 0x80) >> 6) | ((r >> 8) & 1));
    SETNZ8(r);
    return (uint8_t)r;
}
static uint8_t sub8(M6801 *m, uint8_t a, uint8_t b, int c)
{
    unsigned r = (unsigned)a - b - c;
    m->cc = (uint8_t)(m->cc & ~(NF | ZF | VF | CF));
    m->cc |= (uint8_t)(((((a ^ b) & (a ^ r)) & 0x80) >> 6) | ((r >> 8) & 1));
    SETNZ8(r);
    return (uint8_t)r;
}
static uint16_t add16(M6801 *m, uint16_t a, uint16_t b)
{
    uint32_t r = (uint32_t)a + b;
    m->cc = (uint8_t)(m->cc & ~(NF | ZF | VF | CF));
    m->cc |= (uint8_t)(((((~(a ^ b)) & (a ^ r)) & 0x8000) >> 14) | ((r >> 16) & 1));
    SETNZ16(r);
    return (uint16_t)r;
}
static uint16_t sub16(M6801 *m, uint16_t a, uint16_t b)
{
    uint32_t r = (uint32_t)a - b;
    m->cc = (uint8_t)(m->cc & ~(NF | ZF | VF | CF));
    m->cc |= (uint8_t)(((((a ^ b) & (a ^ r)) & 0x8000) >> 14) | ((r >> 16) & 1));
    SETNZ16(r);
    return (uint16_t)r;
}
static void logic8(M6801 *m, uint8_t v) { m->cc &= (uint8_t)~VF; SETNZ8(v); }

/* shift/rotate/unary ops of the 0x40-0x7F rows; lo = low nibble */
static uint8_t unary(M6801 *m, int lo, uint8_t v)
{
    uint8_t r = v, c;
    switch (lo) {
    case 0x0:
        r = (uint8_t)(0 - v);
        m->cc &= (uint8_t)~(VF | CF);
        if (r == 0x80) m->cc |= VF;
        if (r) m->cc |= CF;
        SETNZ8(r);
        return r;
    case 0x3: r = (uint8_t)~v; m->cc = (uint8_t)((m->cc & ~VF) | CF); SETNZ8(r); return r;
    case 0x4: c = v & 1; r = v >> 1; break;
    case 0x6: c = v & 1; r = (uint8_t)((v >> 1) | ((m->cc & CF) << 7)); break;
    case 0x7: c = v & 1; r = (uint8_t)((v >> 1) | (v & 0x80)); break;
    case 0x8: c = v >> 7; r = (uint8_t)(v << 1); break;
    case 0x9: c = v >> 7; r = (uint8_t)((v << 1) | (m->cc & CF)); break;
    case 0xA: r = (uint8_t)(v - 1); m->cc &= (uint8_t)~VF; if (v == 0x80) m->cc |= VF; SETNZ8(r); return r;
    case 0xC: r = (uint8_t)(v + 1); m->cc &= (uint8_t)~VF; if (v == 0x7F) m->cc |= VF; SETNZ8(r); return r;
    case 0xD: m->cc &= (uint8_t)~(VF | CF); SETNZ8(v); return v;
    case 0xF: m->cc = (uint8_t)((m->cc & ~(NF | VF | CF)) | ZF); return 0;
    default: return v;
    }
    m->cc = (uint8_t)((m->cc & ~CF) | c);
    SETNZ8(r);
    { int n = (r & 0x80) != 0; m->cc = (uint8_t)((m->cc & ~VF) | ((n ^ c) ? VF : 0)); }
    return r;
}

static int branch_taken(M6801 *m, int c)
{
    int C = m->cc & CF, Z = (m->cc & ZF) != 0, N = (m->cc & NF) != 0, V = (m->cc & VF) != 0;
    switch (c) {
    case 0x0: return 1; case 0x1: return 0;
    case 0x2: return !(C || Z); case 0x3: return C || Z;
    case 0x4: return !C; case 0x5: return C;
    case 0x6: return !Z; case 0x7: return Z;
    case 0x8: return !V; case 0x9: return V;
    case 0xA: return !N; case 0xB: return N;
    case 0xC: return !(N ^ V); case 0xD: return N ^ V;
    case 0xE: return !Z && !(N ^ V); default: return Z || (N ^ V);
    }
}

static void take_interrupt(M6801 *m, uint16_t vec)
{
    if (!m->wai) {
        push16(m, m->pc); push16(m, m->x);
        push8(m, m->a); push8(m, m->b); push8(m, m->cc);
    }
    m->wai = 0;
    m->cc |= IF;
    m->pc = rd16(m, vec);
}

static uint16_t pending_vector(M6801 *m)
{
    if (m->cc & IF) return 0;
    if (m->irq1 || ((m->p3csr & (P3_IS3IE | P3_IS3F)) == (P3_IS3IE | P3_IS3F))) return 0xFFF8;
    if ((m->tcsr & (TCSR_ICF | TCSR_EICI)) == (TCSR_ICF | TCSR_EICI)) return 0xFFF6;
    if ((m->tcsr & (TCSR_OCF | TCSR_EOCI)) == (TCSR_OCF | TCSR_EOCI)) return 0xFFF4;
    if ((m->tcsr & (TCSR_TOF | TCSR_ETOI)) == (TCSR_TOF | TCSR_ETOI)) return 0xFFF2;
    if ((m->trcsr & SCI_RIE) && (m->trcsr & (SCI_RDRF | SCI_ORFE))) return 0xFFF0;
    if ((m->trcsr & SCI_TIE) && !m->tdr_full) return 0xFFF0;
    return 0;
}

/* ------------------------------------------------------------- execute */
static int exec(M6801 *m, uint8_t op)
{
    if (op >= 0x80) {
        int mode = (op >> 4) & 3, lo = op & 0x0F, isb = op & 0x40;
        uint8_t *r = isb ? &m->b : &m->a;
        uint16_t ea = 0;
        /* 16-bit and control-flow columns */
        if (lo == 0x3 || lo == 0xC || lo == 0xE) {
            uint16_t v;
            if (mode == 0) v = fetch16(m);
            else { ea = mode == 1 ? fetch(m) : mode == 2 ? (uint16_t)(m->x + fetch(m)) : fetch16(m); v = rd16(m, ea); }
            uint16_t d = (uint16_t)(m->a << 8 | m->b);
            if (lo == 0x3) { d = isb ? add16(m, d, v) : sub16(m, d, v); m->a = (uint8_t)(d >> 8); m->b = (uint8_t)d; }
            else if (lo == 0xC) { if (isb) { m->a = (uint8_t)(v >> 8); m->b = (uint8_t)v; m->cc &= (uint8_t)~VF; SETNZ16(v); } else sub16(m, m->x, v); }
            else { if (isb) m->x = v; else m->sp = v; m->cc &= (uint8_t)~VF; SETNZ16(v); }
            if (lo == 0xE || (lo == 0xC && isb)) return mode == 0 ? 3 : mode == 1 ? 4 : 5;  /* loads */
            return mode == 0 ? 4 : mode == 1 ? 5 : 6;                                      /* ADDD SUBD CPX */
        }
        if (lo == 0xD || lo == 0xF) {
            if (op == 0x8D) { int8_t d = (int8_t)fetch(m); push16(m, m->pc); m->pc = (uint16_t)(m->pc + d); return 6; }
            if (mode == 0) return 2; /* illegal immediate store */
            ea = mode == 1 ? fetch(m) : mode == 2 ? (uint16_t)(m->x + fetch(m)) : fetch16(m);
            if (lo == 0xD && !isb) { push16(m, m->pc); m->pc = ea; return mode == 1 ? 5 : 6; }
            uint16_t v = lo == 0xD ? (uint16_t)(m->a << 8 | m->b) : isb ? m->x : m->sp;
            wr16(m, ea, v); m->cc &= (uint8_t)~VF; SETNZ16(v);
            return mode == 1 ? 4 : 5;
        }
        if (lo == 0x7) {
            if (mode == 0) return 2;
            ea = mode == 1 ? fetch(m) : mode == 2 ? (uint16_t)(m->x + fetch(m)) : fetch16(m);
            wr(m, ea, *r); logic8(m, *r);
            return mode == 1 ? 3 : 4;
        }
        uint8_t v;
        if (mode == 0) v = fetch(m);
        else { ea = mode == 1 ? fetch(m) : mode == 2 ? (uint16_t)(m->x + fetch(m)) : fetch16(m); v = rd(m, ea); }
        switch (lo) {
        case 0x0: *r = sub8(m, *r, v, 0); break;
        case 0x1: sub8(m, *r, v, 0); break;
        case 0x2: *r = sub8(m, *r, v, m->cc & CF); break;
        case 0x4: *r &= v; logic8(m, *r); break;
        case 0x5: logic8(m, (uint8_t)(*r & v)); break;
        case 0x6: *r = v; logic8(m, v); break;
        case 0x8: *r ^= v; logic8(m, *r); break;
        case 0x9: *r = add8(m, *r, v, m->cc & CF); break;
        case 0xA: *r |= v; logic8(m, *r); break;
        case 0xB: *r = add8(m, *r, v, 0); break;
        }
        return mode == 0 ? 2 : mode == 1 ? 3 : 4;
    }
    if (op >= 0x40) {
        int lo = op & 0x0F, row = op >> 4;
        if (lo == 0x1 || lo == 0x2 || lo == 0x5 || lo == 0xB) return 2; /* illegal */
        if (row == 4) { m->a = unary(m, lo, m->a); return 2; }
        if (row == 5) { if (lo == 0xE) return 2; m->b = unary(m, lo, m->b); return 2; }
        uint16_t ea = row == 6 ? (uint16_t)(m->x + fetch(m)) : fetch16(m);
        if (lo == 0xE) { m->pc = ea; return 3; }
        if (lo == 0xD) { unary(m, lo, rd(m, ea)); return row == 6 ? 6 : 6; }
        if (lo == 0xF) { unary(m, lo, 0); wr(m, ea, 0); return 6; }
        wr(m, ea, unary(m, lo, rd(m, ea)));
        return 6;
    }
    if (op >= 0x20 && op < 0x30) {
        int8_t d = (int8_t)fetch(m);
        if (branch_taken(m, op & 0x0F)) m->pc = (uint16_t)(m->pc + d);
        return 3;
    }
    switch (op) {
    case 0x01: return 2;
    case 0x04: { uint16_t d = (uint16_t)(m->a << 8 | m->b); uint8_t c = d & 1; d >>= 1;
        m->a = (uint8_t)(d >> 8); m->b = (uint8_t)d;
        m->cc = (uint8_t)((m->cc & ~(NF | ZF | VF | CF)) | c | (c ? VF : 0) | (d ? 0 : ZF)); return 3; }
    case 0x05: { uint16_t d = (uint16_t)(m->a << 8 | m->b); uint8_t c = d >> 15; d = (uint16_t)(d << 1);
        m->a = (uint8_t)(d >> 8); m->b = (uint8_t)d;
        m->cc = (uint8_t)((m->cc & ~CF) | c); SETNZ16(d);
        m->cc = (uint8_t)((m->cc & ~VF) | ((((d >> 15) & 1) ^ c) ? VF : 0)); return 3; }
    case 0x06: m->cc = (uint8_t)(m->a | 0xC0); return 2;
    case 0x07: m->a = m->cc; return 2;
    case 0x08: m->x++; m->cc = (uint8_t)((m->cc & ~ZF) | (m->x ? 0 : ZF)); return 3;
    case 0x09: m->x--; m->cc = (uint8_t)((m->cc & ~ZF) | (m->x ? 0 : ZF)); return 3;
    case 0x0A: m->cc &= (uint8_t)~VF; return 2;
    case 0x0B: m->cc |= VF; return 2;
    case 0x0C: m->cc &= (uint8_t)~CF; return 2;
    case 0x0D: m->cc |= CF; return 2;
    case 0x0E: m->cc &= (uint8_t)~IF; return 2;
    case 0x0F: m->cc |= IF; return 2;
    case 0x10: m->a = sub8(m, m->a, m->b, 0); return 2;
    case 0x11: sub8(m, m->a, m->b, 0); return 2;
    case 0x16: m->b = m->a; logic8(m, m->b); return 2;
    case 0x17: m->a = m->b; logic8(m, m->a); return 2;
    case 0x19: {
        uint8_t a = m->a, cf = 0, lsn = a & 0x0F, msn = a >> 4;
        if ((m->cc & HF) || lsn > 9) cf |= 0x06;
        if ((m->cc & CF) || msn > 9 || (msn > 8 && lsn > 9)) cf |= 0x60;
        unsigned r = (unsigned)a + cf;
        m->a = (uint8_t)r;
        m->cc = (uint8_t)(m->cc & ~(VF));
        if ((cf & 0x60) || r > 0xFF) m->cc |= CF;
        SETNZ8(m->a);
        return 2; }
    case 0x1B: m->a = add8(m, m->a, m->b, 0); return 2;
    case 0x30: m->x = (uint16_t)(m->sp + 1); return 3;
    case 0x31: m->sp++; return 3;
    case 0x32: m->a = pull8(m); return 4;
    case 0x33: m->b = pull8(m); return 4;
    case 0x34: m->sp--; return 3;
    case 0x35: m->sp = (uint16_t)(m->x - 1); return 3;
    case 0x36: push8(m, m->a); return 3;
    case 0x37: push8(m, m->b); return 3;
    case 0x38: m->x = pull16(m); return 5;
    case 0x39: m->pc = pull16(m); return 5;
    case 0x3A: m->x = (uint16_t)(m->x + m->b); return 3;
    case 0x3B: m->cc = (uint8_t)(pull8(m) | 0xC0); m->b = pull8(m); m->a = pull8(m);
               m->x = pull16(m); m->pc = pull16(m); return 10;
    case 0x3C: push16(m, m->x); return 4;
    case 0x3D: { uint16_t d = (uint16_t)(m->a * m->b); m->a = (uint8_t)(d >> 8); m->b = (uint8_t)d;
               m->cc = (uint8_t)((m->cc & ~CF) | ((d & 0x80) ? CF : 0)); return 10; }
    case 0x3E: push16(m, m->pc); push16(m, m->x); push8(m, m->a); push8(m, m->b); push8(m, m->cc);
               m->wai = 1; return 9;
    case 0x3F: push16(m, m->pc); push16(m, m->x); push8(m, m->a); push8(m, m->b); push8(m, m->cc);
               m->cc |= IF; m->pc = rd16(m, 0xFFFA); return 12;
    }
    return 2; /* illegal opcode: treat as NOP */
}

int m6801_step(M6801 *m)
{
    int n;
    if (m->nmi_pending) {
        m->nmi_pending = 0;
        int w = m->wai;
        take_interrupt(m, 0xFFFC);
        n = w ? 4 : 12;
    } else {
        uint16_t vec = pending_vector(m);
        if (vec) { int w = m->wai; take_interrupt(m, vec); n = w ? 4 : 12; }
        else if (m->wai) n = 1;
        else n = exec(m, fetch(m));
    }
    m->cycles += (uint64_t)n;
    timer_advance(m, (unsigned)n);
    sci_update(m);
    if (m->cycles >= m->rx_due && m->sci_poll) m->sci_poll(m->ctx);
    return n;
}

/* ------------------------------------------------------------- disassembler */
enum { D_INH, D_IMM8, D_IMM16, D_DIR, D_IDX, D_EXT, D_REL, D_BAD };
static const char *const rmw_names[16] = { "NEG", 0, 0, "COM", "LSR", 0, "ROR", "ASR", "ASL", "ROL", "DEC", 0, "INC", "TST", "JMP", "CLR" };
static const char *const alu_names[16] = { "SUB", "CMP", "SBC", 0, "AND", "BIT", "LDA", "STA", "EOR", "ADC", "ORA", "ADD", 0, 0, 0, 0 };
static const char *const br_names[16] = { "BRA", "BRN", "BHI", "BLS", "BCC", "BCS", "BNE", "BEQ", "BVC", "BVS", "BPL", "BMI", "BGE", "BLT", "BGT", "BLE" };

static int decode(uint8_t op, char *name, int *mode)
{
    static const struct { uint8_t op; const char *n; } inh[] = {
        {0x01,"NOP"},{0x04,"LSRD"},{0x05,"ASLD"},{0x06,"TAP"},{0x07,"TPA"},{0x08,"INX"},{0x09,"DEX"},
        {0x0A,"CLV"},{0x0B,"SEV"},{0x0C,"CLC"},{0x0D,"SEC"},{0x0E,"CLI"},{0x0F,"SEI"},{0x10,"SBA"},
        {0x11,"CBA"},{0x16,"TAB"},{0x17,"TBA"},{0x19,"DAA"},{0x1B,"ABA"},{0x30,"TSX"},{0x31,"INS"},
        {0x32,"PULA"},{0x33,"PULB"},{0x34,"DES"},{0x35,"TXS"},{0x36,"PSHA"},{0x37,"PSHB"},{0x38,"PULX"},
        {0x39,"RTS"},{0x3A,"ABX"},{0x3B,"RTI"},{0x3C,"PSHX"},{0x3D,"MUL"},{0x3E,"WAI"},{0x3F,"SWI"} };
    *mode = D_BAD; strcpy(name, "???");
    if (op < 0x20 || (op >= 0x30 && op < 0x40)) {
        for (unsigned i = 0; i < sizeof inh / sizeof inh[0]; i++)
            if (inh[i].op == op) { strcpy(name, inh[i].n); *mode = D_INH; }
        return *mode != D_BAD;
    }
    if (op < 0x30) { strcpy(name, br_names[op & 15]); *mode = D_REL; return 1; }
    int lo = op & 15;
    if (op < 0x80) {
        if (!rmw_names[lo]) return 0;
        int row = op >> 4;
        if (lo == 0xE && row < 6) return 0;
        sprintf(name, "%s%s", rmw_names[lo], row == 4 ? "A" : row == 5 ? "B" : "");
        *mode = row < 6 ? D_INH : row == 6 ? D_IDX : D_EXT;
        return 1;
    }
    int md = (op >> 4) & 3, isb = op & 0x40;
    static const int modes8[4] = { D_IMM8, D_DIR, D_IDX, D_EXT };
    static const int modes16[4] = { D_IMM16, D_DIR, D_IDX, D_EXT };
    if (op == 0x8D) { strcpy(name, "BSR"); *mode = D_REL; return 1; }
    if (lo == 0x3) { strcpy(name, isb ? "ADDD" : "SUBD"); *mode = modes16[md]; return 1; }
    if (lo == 0xC) { strcpy(name, isb ? "LDD" : "CPX"); *mode = modes16[md]; return 1; }
    if (lo == 0xE) { strcpy(name, isb ? "LDX" : "LDS"); *mode = modes16[md]; return 1; }
    if (lo == 0xD) { if (!md) return 0; strcpy(name, isb ? "STD" : "JSR"); *mode = modes16[md]; return 1; }
    if (lo == 0xF) { if (!md) return 0; strcpy(name, isb ? "STX" : "STS"); *mode = modes16[md]; return 1; }
    if (!alu_names[lo] || (lo == 7 && !md)) return 0;
    sprintf(name, "%s%s", alu_names[lo], isb ? "B" : "A");
    *mode = modes8[md];
    return 1;
}

int m6801_disasm(M6801 *m, uint16_t pc, char *buf, int bufsize)
{
    char name[16];
    int mode;
    uint8_t op = m->read(m->ctx, pc);
    uint8_t b1 = m->read(m->ctx, (uint16_t)(pc + 1)), b2 = m->read(m->ctx, (uint16_t)(pc + 2));
    decode(op, name, &mode);
    switch (mode) {
    case D_INH: snprintf(buf, (size_t)bufsize, "%s", name); return 1;
    case D_IMM8: snprintf(buf, (size_t)bufsize, "%s #$%02X", name, b1); return 2;
    case D_IMM16: snprintf(buf, (size_t)bufsize, "%s #$%04X", name, b1 << 8 | b2); return 3;
    case D_DIR: snprintf(buf, (size_t)bufsize, "%s <$%02X", name, b1); return 2;
    case D_IDX: snprintf(buf, (size_t)bufsize, "%s $%02X,X", name, b1); return 2;
    case D_EXT: snprintf(buf, (size_t)bufsize, "%s $%04X", name, b1 << 8 | b2); return 3;
    case D_REL: snprintf(buf, (size_t)bufsize, "%s $%04X", name, (uint16_t)(pc + 2 + (int8_t)b1)); return 2;
    default: snprintf(buf, (size_t)bufsize, "DB $%02X", op); return 1;
    }
}
