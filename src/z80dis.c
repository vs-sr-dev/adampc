/* z80dis.c - Z80 disassembler (documented + common undocumented forms). */
#include "z80.h"
#include <stdio.h>
#include <string.h>

static const char *const r8[8] = { "B", "C", "D", "E", "H", "L", "(HL)", "A" };
static const char *const rp[4] = { "BC", "DE", "HL", "SP" };
static const char *const rp2[4] = { "BC", "DE", "HL", "AF" };
static const char *const cc[8] = { "NZ", "Z", "NC", "C", "PO", "PE", "P", "M" };
static const char *const alu[8] = { "ADD A,", "ADC A,", "SUB ", "SBC A,", "AND ", "XOR ", "OR ", "CP " };
static const char *const rot[8] = { "RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL" };

typedef struct { uint8_t (*rd)(void *, uint16_t); void *ctx; uint16_t pc; } Src;
static uint8_t nb(Src *s) { return s->rd(s->ctx, s->pc++); }
static uint16_t nw(Src *s) { uint8_t l = nb(s); return (uint16_t)(l | nb(s) << 8); }

/* name of an 8-bit register with index prefix applied; for (HL) the displacement is fetched */
static void reg(Src *s, int r, int idx, char *out, int allow_half)
{
    const char *ix = idx == 1 ? "IX" : "IY";
    if (r == 6 && idx) { int8_t d = (int8_t)nb(s); sprintf(out, "(%s%c%02Xh)", ix, d < 0 ? '-' : '+', d < 0 ? -d : d); }
    else if (idx && allow_half && (r == 4 || r == 5)) sprintf(out, "%s%c", ix, r == 4 ? 'H' : 'L');
    else strcpy(out, r8[r]);
}

int z80_disasm(Z80 *z, uint16_t pc, char *buf, int bufsize)
{
    Src s = { z->read, z->ctx, pc };
    char o[64], a[24], b[24];
    int idx = 0;
    uint8_t op = nb(&s);
    while (op == 0xDD || op == 0xFD) { idx = op == 0xDD ? 1 : 2; op = nb(&s); }
    const char *hl = idx == 1 ? "IX" : idx == 2 ? "IY" : "HL";
    int x = op >> 6, y = (op >> 3) & 7, zz = op & 7, p = y >> 1, q = y & 1;

    if (op == 0xCB) {
        int8_t d = 0;
        if (idx) d = (int8_t)nb(&s);
        uint8_t c = nb(&s);
        int cx = c >> 6, cy = (c >> 3) & 7, cz = c & 7;
        if (idx) sprintf(a, "(%s%c%02Xh)", hl, d < 0 ? '-' : '+', d < 0 ? -d : d);
        else strcpy(a, r8[cz]);
        if (cx == 0) sprintf(o, "%s %s", rot[cy], a);
        else sprintf(o, "%s %d,%s", cx == 1 ? "BIT" : cx == 2 ? "RES" : "SET", cy, a);
    } else if (op == 0xED) {
        uint8_t e = nb(&s);
        int ex = e >> 6, ey = (e >> 3) & 7, ez = e & 7, ep = ey >> 1, eq = ey & 1;
        if (ex == 1) {
            switch (ez) {
            case 0: sprintf(o, ey == 6 ? "IN (C)" : "IN %s,(C)", r8[ey]); break;
            case 1: sprintf(o, ey == 6 ? "OUT (C),0" : "OUT (C),%s", r8[ey]); break;
            case 2: sprintf(o, "%s HL,%s", eq ? "ADC" : "SBC", rp[ep]); break;
            case 3: { uint16_t w = nw(&s); if (eq) sprintf(o, "LD %s,(%04Xh)", rp[ep], w); else sprintf(o, "LD (%04Xh),%s", w, rp[ep]); break; }
            case 4: strcpy(o, "NEG"); break;
            case 5: strcpy(o, ey == 1 ? "RETI" : "RETN"); break;
            case 6: sprintf(o, "IM %d", (int[]){ 0, 0, 1, 2, 0, 0, 1, 2 }[ey]); break;
            default: { static const char *const t[8] = { "LD I,A", "LD R,A", "LD A,I", "LD A,R", "RRD", "RLD", "NOP*", "NOP*" }; strcpy(o, t[ey]); }
            }
        } else if (ex == 2 && ez <= 3 && ey >= 4) {
            static const char *const bl[4][4] = { { "LDI", "CPI", "INI", "OUTI" }, { "LDD", "CPD", "IND", "OUTD" },
                                                 { "LDIR", "CPIR", "INIR", "OTIR" }, { "LDDR", "CPDR", "INDR", "OTDR" } };
            strcpy(o, bl[ey - 4][ez]);
        } else sprintf(o, "DB EDh,%02Xh", e);
    } else if (x == 1) {
        if (op == 0x76) strcpy(o, "HALT");
        else {
            int mem = zz == 6 || y == 6;
            reg(&s, y, idx, a, !mem); reg(&s, zz, idx, b, !mem);
            sprintf(o, "LD %s,%s", a, b);
        }
    } else if (x == 2) {
        reg(&s, zz, idx, a, 1);
        sprintf(o, "%s%s", alu[y], a);
    } else if (x == 0) {
        switch (zz) {
        case 0:
            if (y == 0) strcpy(o, "NOP");
            else if (y == 1) strcpy(o, "EX AF,AF'");
            else { int8_t d = (int8_t)nb(&s); uint16_t t = (uint16_t)(s.pc + d);
                   if (y == 2) sprintf(o, "DJNZ %04Xh", t); else if (y == 3) sprintf(o, "JR %04Xh", t);
                   else sprintf(o, "JR %s,%04Xh", cc[y - 4], t); }
            break;
        case 1:
            if (!q) sprintf(o, "LD %s,%04Xh", p == 2 ? hl : rp[p], nw(&s));
            else sprintf(o, "ADD %s,%s", hl, p == 2 ? hl : rp[p]);
            break;
        case 2: {
            static const char *const t[8] = { "LD (BC),A", "LD A,(BC)", "LD (DE),A", "LD A,(DE)", 0, 0, 0, 0 };
            if (y < 4) strcpy(o, t[y]);
            else { uint16_t w = nw(&s);
                   if (y == 4) sprintf(o, "LD (%04Xh),%s", w, hl); else if (y == 5) sprintf(o, "LD %s,(%04Xh)", hl, w);
                   else if (y == 6) sprintf(o, "LD (%04Xh),A", w); else sprintf(o, "LD A,(%04Xh)", w); }
            break; }
        case 3: sprintf(o, "%s %s", q ? "DEC" : "INC", p == 2 ? hl : rp[p]); break;
        case 4: reg(&s, y, idx, a, 1); sprintf(o, "INC %s", a); break;
        case 5: reg(&s, y, idx, a, 1); sprintf(o, "DEC %s", a); break;
        case 6: reg(&s, y, idx, a, 1); sprintf(o, "LD %s,%02Xh", a, nb(&s)); break;
        default: { static const char *const t[8] = { "RLCA", "RRCA", "RLA", "RRA", "DAA", "CPL", "SCF", "CCF" }; strcpy(o, t[y]); }
        }
    } else {
        switch (zz) {
        case 0: sprintf(o, "RET %s", cc[y]); break;
        case 1:
            if (!q) sprintf(o, "POP %s", p == 2 ? hl : rp2[p]);
            else { static const char *const t[4] = { "RET", "EXX", 0, "LD SP," };
                   if (p == 2) sprintf(o, "JP (%s)", hl); else if (p == 3) sprintf(o, "LD SP,%s", hl); else strcpy(o, t[p]); }
            break;
        case 2: sprintf(o, "JP %s,%04Xh", cc[y], nw(&s)); break;
        case 3:
            switch (y) {
            case 0: sprintf(o, "JP %04Xh", nw(&s)); break;
            case 2: sprintf(o, "OUT (%02Xh),A", nb(&s)); break;
            case 3: sprintf(o, "IN A,(%02Xh)", nb(&s)); break;
            case 4: sprintf(o, "EX (SP),%s", hl); break;
            case 5: strcpy(o, "EX DE,HL"); break;
            case 6: strcpy(o, "DI"); break;
            default: strcpy(o, "EI"); break;
            }
            break;
        case 4: sprintf(o, "CALL %s,%04Xh", cc[y], nw(&s)); break;
        case 5:
            if (!q) sprintf(o, "PUSH %s", p == 2 ? hl : rp2[p]);
            else sprintf(o, "CALL %04Xh", nw(&s));
            break;
        case 6: sprintf(o, "%s%02Xh", alu[y], nb(&s)); break;
        default: sprintf(o, "RST %02Xh", y * 8); break;
        }
    }
    snprintf(buf, (size_t)bufsize, "%s", o);
    return (uint16_t)(s.pc - pc);
}
