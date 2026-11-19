/* z80.c - Zilog Z80 CPU core. See z80.h. */
#include "z80.h"
#include <string.h>

enum { FC = 0x01, FN = 0x02, FP = 0x04, FX = 0x08, FH = 0x10, FY = 0x20, FZ = 0x40, FS = 0x80 };

static uint8_t sz53[256], sz53p[256];
static int tables_ready;

static void init_tables(void)
{
    for (int i = 0; i < 256; i++) {
        int p = 0;
        for (int b = 0; b < 8; b++) p ^= (i >> b) & 1;
        sz53[i] = (i & (FS | FY | FX)) | (i == 0 ? FZ : 0);
        sz53p[i] = sz53[i] | (p ? 0 : FP);
    }
    tables_ready = 1;
}

#define A  z->af.b.h
#define F  z->af.b.l
#define B  z->bc.b.h
#define C  z->bc.b.l
#define D  z->de.b.h
#define E  z->de.b.l
#define H  z->hl.b.h
#define L  z->hl.b.l
#define AF z->af.w
#define BC z->bc.w
#define DE z->de.w
#define HL z->hl.w
#define SP z->sp.w
#define PC z->pc.w
#define WZ z->wz.w

#define SETF(v) do { F = (uint8_t)(v); z->qnext = 1; } while (0)

static inline uint8_t rd(Z80 *z, uint16_t a) { return z->read(z->ctx, a); }
static inline void wr(Z80 *z, uint16_t a, uint8_t v) { z->write(z->ctx, a, v); }
static inline uint8_t fetch(Z80 *z) { return rd(z, PC++); }
static inline uint16_t fetch16(Z80 *z) { uint8_t lo = fetch(z); return (uint16_t)(lo | fetch(z) << 8); }
static inline uint16_t rd16(Z80 *z, uint16_t a) { uint8_t lo = rd(z, a); return (uint16_t)(lo | rd(z, (uint16_t)(a + 1)) << 8); }
static inline void wr16(Z80 *z, uint16_t a, uint16_t v) { wr(z, a, (uint8_t)v); wr(z, (uint16_t)(a + 1), (uint8_t)(v >> 8)); }
static inline void push(Z80 *z, uint16_t v) { SP--; wr(z, SP, (uint8_t)(v >> 8)); SP--; wr(z, SP, (uint8_t)v); }
static inline uint16_t pop(Z80 *z) { uint8_t lo = rd(z, SP++); return (uint16_t)(lo | rd(z, SP++) << 8); }
static inline void inc_r(Z80 *z) { z->r = (uint8_t)((z->r & 0x80) | ((z->r + 1) & 0x7F)); }

void z80_init(Z80 *z)
{
    if (!tables_ready) init_tables();
    void *ctx = z->ctx;
    uint8_t (*r)(void *, uint16_t) = z->read;
    void (*w)(void *, uint16_t, uint8_t) = z->write;
    uint8_t (*i)(void *, uint16_t) = z->in;
    void (*o)(void *, uint16_t, uint8_t) = z->out;
    memset(z, 0, sizeof *z);
    z->ctx = ctx; z->read = r; z->write = w; z->in = i; z->out = o;
    z80_reset(z);
}

void z80_reset(Z80 *z)
{
    PC = 0; z->i = 0; z->r = 0; z->iff1 = z->iff2 = 0; z->im = 0;
    z->halted = 0; z->ei_delay = 0; z->nmi_pending = 0;
    AF = 0xFFFF; SP = 0xFFFF; z->int_data = 0xFF; z->q = z->qnext = 0;
}

void z80_nmi(Z80 *z) { z->nmi_pending = 1; }
void z80_set_int(Z80 *z, int level) { z->int_line = (uint8_t)(level != 0); }

/* ---------------------------------------------------------------- ALU */
static void add8(Z80 *z, uint8_t v, int carry)
{
    unsigned r = A + v + carry;
    SETF(sz53[r & 0xFF] | ((A ^ v ^ r) & FH) | ((((A ^ ~v) & (A ^ r)) & 0x80) >> 5) | (r >> 8));
    A = (uint8_t)r;
}
static uint8_t sub8f(Z80 *z, uint8_t v, int carry)
{
    unsigned r = (unsigned)A - v - carry;
    SETF(sz53[r & 0xFF] | FN | ((A ^ v ^ r) & FH) | ((((A ^ v) & (A ^ r)) & 0x80) >> 5) | ((r >> 8) & 1));
    return (uint8_t)r;
}
static void alu(Z80 *z, int op, uint8_t v)
{
    switch (op) {
    case 0: add8(z, v, 0); break;
    case 1: add8(z, v, F & FC); break;
    case 2: A = sub8f(z, v, 0); break;
    case 3: A = sub8f(z, v, F & FC); break;
    case 4: A &= v; SETF(sz53p[A] | FH); break;
    case 5: A ^= v; SETF(sz53p[A]); break;
    case 6: A |= v; SETF(sz53p[A]); break;
    case 7: sub8f(z, v, 0); F = (uint8_t)((F & ~(FX | FY)) | (v & (FX | FY))); break;
    }
}
static uint8_t inc8(Z80 *z, uint8_t v)
{
    uint8_t r = (uint8_t)(v + 1);
    SETF((F & FC) | sz53[r] | (r == 0x80 ? FP : 0) | ((r & 0x0F) == 0 ? FH : 0));
    return r;
}
static uint8_t dec8(Z80 *z, uint8_t v)
{
    uint8_t r = (uint8_t)(v - 1);
    SETF((F & FC) | FN | sz53[r] | (r == 0x7F ? FP : 0) | ((r & 0x0F) == 0x0F ? FH : 0));
    return r;
}
static uint16_t add16(Z80 *z, uint16_t a, uint16_t v)
{
    unsigned r = a + v;
    WZ = (uint16_t)(a + 1);
    SETF((F & (FS | FZ | FP)) | ((r >> 8) & (FX | FY)) | (((a ^ v ^ r) >> 8) & FH) | (r >> 16));
    return (uint16_t)r;
}
static void adc16(Z80 *z, uint16_t v)
{
    unsigned a = HL, r = a + v + (F & FC);
    WZ = (uint16_t)(a + 1);
    SETF(((r >> 8) & (FS | FX | FY)) | ((r & 0xFFFF) ? 0 : FZ) | (((a ^ v ^ r) >> 8) & FH) |
         (((~(a ^ v) & (a ^ r)) & 0x8000) >> 13) | ((r >> 16) & 1));
    HL = (uint16_t)r;
}
static void sbc16(Z80 *z, uint16_t v)
{
    unsigned a = HL, r = a - v - (F & FC);
    WZ = (uint16_t)(a + 1);
    SETF(FN | ((r >> 8) & (FS | FX | FY)) | ((r & 0xFFFF) ? 0 : FZ) | (((a ^ v ^ r) >> 8) & FH) |
         ((((a ^ v) & (a ^ r)) & 0x8000) >> 13) | ((r >> 16) & 1));
    HL = (uint16_t)r;
}
/* CB-page rotate/shift, op = 0..7 (RLC RRC RL RR SLA SRA SLL SRL) */
static uint8_t rot(Z80 *z, int op, uint8_t v)
{
    uint8_t c, r;
    switch (op) {
    case 0: c = v >> 7; r = (uint8_t)(v << 1 | c); break;
    case 1: c = v & 1; r = (uint8_t)(v >> 1 | c << 7); break;
    case 2: c = v >> 7; r = (uint8_t)(v << 1 | (F & FC)); break;
    case 3: c = v & 1; r = (uint8_t)(v >> 1 | (F & FC) << 7); break;
    case 4: c = v >> 7; r = (uint8_t)(v << 1); break;
    case 5: c = v & 1; r = (uint8_t)((v >> 1) | (v & 0x80)); break;
    case 6: c = v >> 7; r = (uint8_t)(v << 1 | 1); break;
    default: c = v & 1; r = v >> 1; break;
    }
    SETF(sz53p[r] | c);
    return r;
}
static void bit(Z80 *z, int n, uint8_t v, uint8_t xy)
{
    uint8_t f = (F & FC) | FH | (xy & (FX | FY));
    if (v & (1 << n)) { if (n == 7) f |= FS; }
    else f |= FZ | FP;
    SETF(f);
}
static int cond(Z80 *z, int c)
{
    switch (c) {
    case 0: return !(F & FZ);
    case 1: return F & FZ;
    case 2: return !(F & FC);
    case 3: return F & FC;
    case 4: return !(F & FP);
    case 5: return F & FP;
    case 6: return !(F & FS);
    default: return F & FS;
    }
}

/* register access by 3-bit code; idx selects HL/IX/IY for codes 4/5 */
static inline z80_pair *idxreg(Z80 *z, int idx) { return idx == 0 ? &z->hl : idx == 1 ? &z->ix : &z->iy; }
static uint8_t getr(Z80 *z, int r, int idx)
{
    switch (r) {
    case 0: return B; case 1: return C; case 2: return D; case 3: return E;
    case 4: return idxreg(z, idx)->b.h;
    case 5: return idxreg(z, idx)->b.l;
    default: return A;
    }
}
static void setr(Z80 *z, int r, int idx, uint8_t v)
{
    switch (r) {
    case 0: B = v; break; case 1: C = v; break; case 2: D = v; break; case 3: E = v; break;
    case 4: idxreg(z, idx)->b.h = v; break;
    case 5: idxreg(z, idx)->b.l = v; break;
    default: A = v; break;
    }
}
/* address of the (HL)/(IX+d)/(IY+d) operand, fetches d when indexed */
static uint16_t memaddr(Z80 *z, int idx)
{
    if (!idx) return HL;
    int8_t d = (int8_t)fetch(z);
    uint16_t a = (uint16_t)(idxreg(z, idx)->w + d);
    WZ = a;
    return a;
}
static uint16_t *rp(Z80 *z, int p, int idx) /* BC DE HL SP */
{
    switch (p) {
    case 0: return &BC; case 1: return &DE;
    case 2: return &idxreg(z, idx)->w;
    default: return &SP;
    }
}

/* ---------------------------------------------------------------- CB page */
static int exec_cb(Z80 *z, int idx)
{
    if (idx) {
        uint16_t a = memaddr(z, idx);
        uint8_t op = fetch(z);
        int r = op & 7, y = (op >> 3) & 7;
        uint8_t v = rd(z, a), res;
        switch (op >> 6) {
        case 0: res = rot(z, y, v); break;
        case 1: bit(z, y, v, (uint8_t)(a >> 8)); return 20;
        case 2: res = (uint8_t)(v & ~(1 << y)); break;
        default: res = (uint8_t)(v | (1 << y)); break;
        }
        wr(z, a, res);
        if (r != 6) setr(z, r, 0, res);
        return 23;
    }
    uint8_t op = fetch(z);
    inc_r(z);
    int r = op & 7, y = (op >> 3) & 7;
    if (r == 6) {
        uint8_t v = rd(z, HL);
        switch (op >> 6) {
        case 0: wr(z, HL, rot(z, y, v)); return 15;
        case 1: bit(z, y, v, z->wz.b.h); return 12;
        case 2: wr(z, HL, (uint8_t)(v & ~(1 << y))); return 15;
        default: wr(z, HL, (uint8_t)(v | (1 << y))); return 15;
        }
    }
    uint8_t v = getr(z, r, 0);
    switch (op >> 6) {
    case 0: setr(z, r, 0, rot(z, y, v)); break;
    case 1: bit(z, y, v, v); break;
    case 2: setr(z, r, 0, (uint8_t)(v & ~(1 << y))); break;
    default: setr(z, r, 0, (uint8_t)(v | (1 << y))); break;
    }
    return 8;
}

/* ---------------------------------------------------------------- ED page */
static void blk_io_flags(Z80 *z, uint8_t v, unsigned k)
{
    uint8_t f = sz53[B] | ((v & 0x80) ? FN : 0);
    if (k > 0xFF) f |= FH | FC;
    f |= sz53p[(uint8_t)((k & 7) ^ B)] & FP;
    SETF(f);
}

static int exec_ed(Z80 *z)
{
    uint8_t op = fetch(z);
    inc_r(z);
    int y = (op >> 3) & 7, p = y >> 1;
    if (op >= 0x40 && op < 0x80) {
        switch (op & 7) {
        case 0: { /* IN r,(C) */
            uint8_t v = z->in(z->ctx, BC);
            WZ = (uint16_t)(BC + 1);
            SETF((F & FC) | sz53p[v]);
            if (y != 6) setr(z, y, 0, v);
            return 12; }
        case 1: /* OUT (C),r */
            z->out(z->ctx, BC, y == 6 ? 0 : getr(z, y, 0));
            WZ = (uint16_t)(BC + 1);
            return 12;
        case 2:
            if (y & 1) adc16(z, *rp(z, p, 0)); else sbc16(z, *rp(z, p, 0));
            return 15;
        case 3: {
            uint16_t a = fetch16(z);
            if (y & 1) *rp(z, p, 0) = rd16(z, a); else wr16(z, a, *rp(z, p, 0));
            WZ = (uint16_t)(a + 1);
            return 20; }
        case 4: { /* NEG */
            uint8_t v = A; A = 0; A = sub8f(z, v, 0); return 8; }
        case 5: /* RETN / RETI */
            z->iff1 = z->iff2;
            PC = pop(z); WZ = PC;
            return 14;
        case 6: {
            static const uint8_t ims[8] = { 0, 0, 1, 2, 0, 0, 1, 2 };
            z->im = ims[y]; return 8; }
        default:
            switch (y) {
            case 0: z->i = A; return 9;
            case 1: z->r = A; return 9;
            case 2: A = z->i; SETF((F & FC) | sz53[A] | (z->iff2 ? FP : 0)); return 9;
            case 3: A = z->r; SETF((F & FC) | sz53[A] | (z->iff2 ? FP : 0)); return 9;
            case 4: { /* RRD */
                uint8_t v = rd(z, HL);
                wr(z, HL, (uint8_t)((A << 4) | (v >> 4)));
                A = (uint8_t)((A & 0xF0) | (v & 0x0F));
                SETF((F & FC) | sz53p[A]); WZ = (uint16_t)(HL + 1);
                return 18; }
            case 5: { /* RLD */
                uint8_t v = rd(z, HL);
                wr(z, HL, (uint8_t)((v << 4) | (A & 0x0F)));
                A = (uint8_t)((A & 0xF0) | (v >> 4));
                SETF((F & FC) | sz53p[A]); WZ = (uint16_t)(HL + 1);
                return 18; }
            default: return 8; /* ED 77 / ED 7F: NOP */
            }
        }
    }
    if (op >= 0xA0 && op <= 0xBB && (op & 7) <= 3 && y >= 4) {
        int dec = y & 1, rep = y & 2;
        switch (op & 3) {
        case 0: { /* LDI/LDD */
            uint8_t v = rd(z, HL);
            wr(z, DE, v);
            HL = dec ? HL - 1 : HL + 1; DE = dec ? DE - 1 : DE + 1; BC--;
            uint8_t n = (uint8_t)(v + A);
            SETF((F & (FS | FZ | FC)) | (BC ? FP : 0) | (n & FX) | ((n << 4) & FY));
            if (rep && BC) { PC -= 2; WZ = (uint16_t)(PC + 1); return 21; }
            return 16; }
        case 1: { /* CPI/CPD */
            uint8_t v = rd(z, HL);
            uint8_t r = (uint8_t)(A - v);
            HL = dec ? HL - 1 : HL + 1; BC--;
            WZ = dec ? WZ - 1 : WZ + 1;
            uint8_t f = (F & FC) | FN | (sz53[r] & (FS | FZ)) | ((A ^ v ^ r) & FH) | (BC ? FP : 0);
            uint8_t n = (uint8_t)(r - ((f & FH) ? 1 : 0));
            f |= (n & FX) | ((n << 4) & FY);
            SETF(f);
            if (rep && BC && !(f & FZ)) { PC -= 2; WZ = (uint16_t)(PC + 1); return 21; }
            return 16; }
        case 2: { /* INI/IND */
            uint8_t v = z->in(z->ctx, BC);
            WZ = dec ? BC - 1 : BC + 1;
            wr(z, HL, v);
            B--; HL = dec ? HL - 1 : HL + 1;
            blk_io_flags(z, v, (unsigned)v + (uint8_t)(dec ? C - 1 : C + 1));
            if (rep && B) { PC -= 2; return 21; }
            return 16; }
        default: { /* OUTI/OUTD */
            uint8_t v = rd(z, HL);
            B--;
            WZ = dec ? BC - 1 : BC + 1;
            z->out(z->ctx, BC, v);
            HL = dec ? HL - 1 : HL + 1;
            blk_io_flags(z, v, (unsigned)v + L);
            if (rep && B) { PC -= 2; return 21; }
            return 16; }
        }
    }
    return 8; /* undefined ED: 8-cycle NOP */
}

/* ---------------------------------------------------------------- main page */
static int exec_main(Z80 *z, uint8_t op, int idx)
{
    int x = op >> 6, y = (op >> 3) & 7, zz = op & 7, p = y >> 1;
    z80_pair *hx = idxreg(z, idx);
    int xc = idx ? 4 : 0; /* extra cycles for the prefix itself are added by the caller */
    (void)xc;

    if (x == 1) {
        if (op == 0x76) { z->halted = 1; return 4; }
        if (zz == 6) { uint16_t a = memaddr(z, idx); setr(z, y, 0, rd(z, a)); return idx ? 15 : 7; }
        if (y == 6) { uint16_t a = memaddr(z, idx); wr(z, a, getr(z, zz, 0)); return idx ? 15 : 7; }
        setr(z, y, idx, getr(z, zz, idx));
        return 4;
    }
    if (x == 2) {
        if (zz == 6) { uint16_t a = memaddr(z, idx); alu(z, y, rd(z, a)); return idx ? 15 : 7; }
        alu(z, y, getr(z, zz, idx));
        return 4;
    }
    switch (op) {
    case 0x00: return 4;
    case 0x08: { uint16_t t = AF; AF = z->af_.w; z->af_.w = t; return 4; }
    case 0x10: { int8_t d = (int8_t)fetch(z); if (--B) { PC = (uint16_t)(PC + d); WZ = PC; return 13; } return 8; }
    case 0x18: { int8_t d = (int8_t)fetch(z); PC = (uint16_t)(PC + d); WZ = PC; return 12; }
    case 0x20: case 0x28: case 0x30: case 0x38: {
        int8_t d = (int8_t)fetch(z);
        if (cond(z, y - 4)) { PC = (uint16_t)(PC + d); WZ = PC; return 12; }
        return 7; }
    case 0x01: case 0x11: case 0x21: case 0x31: *rp(z, p, idx) = fetch16(z); return 10;
    case 0x09: case 0x19: case 0x29: case 0x39: hx->w = add16(z, hx->w, *rp(z, p, idx)); return 11;
    case 0x02: wr(z, BC, A); WZ = (uint16_t)((A << 8) | ((BC + 1) & 0xFF)); return 7;
    case 0x12: wr(z, DE, A); WZ = (uint16_t)((A << 8) | ((DE + 1) & 0xFF)); return 7;
    case 0x0A: A = rd(z, BC); WZ = (uint16_t)(BC + 1); return 7;
    case 0x1A: A = rd(z, DE); WZ = (uint16_t)(DE + 1); return 7;
    case 0x22: { uint16_t a = fetch16(z); wr16(z, a, hx->w); WZ = (uint16_t)(a + 1); return 16; }
    case 0x2A: { uint16_t a = fetch16(z); hx->w = rd16(z, a); WZ = (uint16_t)(a + 1); return 16; }
    case 0x32: { uint16_t a = fetch16(z); wr(z, a, A); WZ = (uint16_t)((A << 8) | ((a + 1) & 0xFF)); return 13; }
    case 0x3A: { uint16_t a = fetch16(z); A = rd(z, a); WZ = (uint16_t)(a + 1); return 13; }
    case 0x03: case 0x13: case 0x23: case 0x33: (*rp(z, p, idx))++; return 6;
    case 0x0B: case 0x1B: case 0x2B: case 0x3B: (*rp(z, p, idx))--; return 6;
    case 0x34: { uint16_t a = memaddr(z, idx); wr(z, a, inc8(z, rd(z, a))); return idx ? 19 : 11; }
    case 0x35: { uint16_t a = memaddr(z, idx); wr(z, a, dec8(z, rd(z, a))); return idx ? 19 : 11; }
    case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x3C:
        setr(z, y, idx, inc8(z, getr(z, y, idx))); return 4;
    case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x3D:
        setr(z, y, idx, dec8(z, getr(z, y, idx))); return 4;
    case 0x36: { uint16_t a = memaddr(z, idx); wr(z, a, fetch(z)); return idx ? 15 : 10; }
    case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x3E:
        setr(z, y, idx, fetch(z)); return 7;
    case 0x07: A = (uint8_t)(A << 1 | A >> 7); SETF((F & (FS | FZ | FP)) | (A & (FX | FY | FC))); return 4;
    case 0x0F: { uint8_t c = A & 1; A = (uint8_t)(A >> 1 | c << 7); SETF((F & (FS | FZ | FP)) | (A & (FX | FY)) | c); return 4; }
    case 0x17: { uint8_t c = A >> 7; A = (uint8_t)(A << 1 | (F & FC)); SETF((F & (FS | FZ | FP)) | (A & (FX | FY)) | c); return 4; }
    case 0x1F: { uint8_t c = A & 1; A = (uint8_t)(A >> 1 | (F & FC) << 7); SETF((F & (FS | FZ | FP)) | (A & (FX | FY)) | c); return 4; }
    case 0x27: { /* DAA */
        uint8_t a = A, diff = 0, c = F & FC, n = F & FN, h;
        if ((F & FH) || (a & 0x0F) > 9) diff |= 0x06;
        if (c || a > 0x99) { diff |= 0x60; c = FC; }
        if (n) { h = ((F & FH) && (a & 0x0F) < 6) ? FH : 0; A = (uint8_t)(a - diff); }
        else { h = ((a & 0x0F) > 9) ? FH : 0; A = (uint8_t)(a + diff); }
        SETF(sz53p[A] | h | n | c);
        return 4; }
    case 0x2F: A = (uint8_t)~A; SETF((F & (FS | FZ | FP | FC)) | FH | FN | (A & (FX | FY))); return 4;
    case 0x37: SETF((F & (FS | FZ | FP)) | (((z->q ^ F) | A) & (FX | FY)) | FC); return 4;
    case 0x3F: SETF(((F & (FS | FZ | FP | FC)) | ((F & FC) << 4) | (((z->q ^ F) | A) & (FX | FY))) ^ FC); return 4;
    /* x == 3 */
    case 0xC0: case 0xC8: case 0xD0: case 0xD8: case 0xE0: case 0xE8: case 0xF0: case 0xF8:
        if (cond(z, y)) { PC = pop(z); WZ = PC; return 11; }
        return 5;
    case 0xC9: PC = pop(z); WZ = PC; return 10;
    case 0xC1: case 0xD1: case 0xE1: *rp(z, p, idx) = pop(z); return 10;
    case 0xF1: AF = pop(z); return 10;
    case 0xC5: case 0xD5: case 0xE5: push(z, *rp(z, p, idx)); return 11;
    case 0xF5: push(z, AF); return 11;
    case 0xC2: case 0xCA: case 0xD2: case 0xDA: case 0xE2: case 0xEA: case 0xF2: case 0xFA: {
        uint16_t a = fetch16(z); WZ = a;
        if (cond(z, y)) PC = a;
        return 10; }
    case 0xC3: PC = fetch16(z); WZ = PC; return 10;
    case 0xC4: case 0xCC: case 0xD4: case 0xDC: case 0xE4: case 0xEC: case 0xF4: case 0xFC: {
        uint16_t a = fetch16(z); WZ = a;
        if (cond(z, y)) { push(z, PC); PC = a; return 17; }
        return 10; }
    case 0xCD: { uint16_t a = fetch16(z); WZ = a; push(z, PC); PC = a; return 17; }
    case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:
        alu(z, y, fetch(z)); return 7;
    case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF:
        push(z, PC); PC = (uint16_t)(y * 8); WZ = PC; return 11;
    case 0xCB: return exec_cb(z, idx) - (idx ? 4 : 0); /* DDCB counts include the DD prefix */
    case 0xD3: { uint8_t n = fetch(z); z->out(z->ctx, (uint16_t)(A << 8 | n), A); WZ = (uint16_t)((A << 8) | ((n + 1) & 0xFF)); return 11; }
    case 0xDB: { uint8_t n = fetch(z); uint16_t port = (uint16_t)(A << 8 | n); A = z->in(z->ctx, port); WZ = (uint16_t)(port + 1); return 11; }
    case 0xD9: { uint16_t t;
        t = BC; BC = z->bc_.w; z->bc_.w = t;
        t = DE; DE = z->de_.w; z->de_.w = t;
        t = HL; HL = z->hl_.w; z->hl_.w = t; return 4; }
    case 0xE3: { uint16_t v = rd16(z, SP); wr16(z, SP, hx->w); hx->w = v; WZ = v; return 19; }
    case 0xE9: PC = hx->w; return 4;
    case 0xEB: { uint16_t t = DE; DE = HL; HL = t; return 4; }
    case 0xF3: z->iff1 = z->iff2 = 0; return 4;
    case 0xFB: z->iff1 = z->iff2 = 1; z->ei_delay = 2; return 4;
    case 0xF9: SP = hx->w; return 6;
    case 0xED: return exec_ed(z) - (idx ? 4 : 0);
    }
    return 4;
}

int z80_step(Z80 *z)
{
    int cyc;
    z->qnext = 0;
    if (z->nmi_pending) {
        z->nmi_pending = 0;
        if (z->halted) z->halted = 0;
        inc_r(z);
        z->iff1 = 0;
        push(z, PC); PC = 0x0066; WZ = PC;
        z->cycles += 11; z->q = 0;
        return 11;
    }
    if (z->int_line && z->iff1 && !z->ei_delay) {
        z->halted = 0;
        inc_r(z);
        z->iff1 = z->iff2 = 0;
        if (z->im == 2) {
            uint16_t v = (uint16_t)(z->i << 8 | z->int_data);
            push(z, PC); PC = rd16(z, v); WZ = PC; cyc = 19;
        } else if (z->im == 1) {
            push(z, PC); PC = 0x38; WZ = PC; cyc = 13;
        } else {
            /* IM 0: only RST n is supported on the data bus (0xFF = RST 38) */
            uint8_t opc = z->int_data;
            push(z, PC);
            PC = (opc & 0xC7) == 0xC7 ? (uint16_t)(opc & 0x38) : 0x38;
            WZ = PC; cyc = 13;
        }
        z->cycles += (uint64_t)cyc; z->q = 0;
        return cyc;
    }
    if (z->ei_delay) z->ei_delay--;
    if (z->halted) { inc_r(z); z->cycles += 4; z->q = 0; return 4; }

    uint8_t op = fetch(z);
    inc_r(z);
    int idx = 0;
    cyc = 0;
    while (op == 0xDD || op == 0xFD) {
        idx = op == 0xDD ? 1 : 2;
        cyc += 4;
        op = fetch(z);
        inc_r(z);
    }
    if (op == 0xED) idx = 0;
    cyc += exec_main(z, op, idx);
    z->q = z->qnext ? F : 0;
    z->cycles += (uint64_t)cyc;
    return cyc;
}
