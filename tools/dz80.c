/* dz80 - disassemble a Z80 binary.  usage: dz80 FILE LOADADDR START [END]  (hex) */
#include <stdio.h>
#include <stdlib.h>
#include "../src/z80.h"

static uint8_t mem[65536];
static uint8_t rd(void *c, uint16_t a) { (void)c; return mem[a]; }

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: dz80 FILE LOADADDR START [END]\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    unsigned load = (unsigned)strtoul(argv[2], NULL, 16);
    size_t n = fread(mem + load, 1, 65536 - load, f);
    fclose(f);
    unsigned pc = (unsigned)strtoul(argv[3], NULL, 16);
    unsigned end = argc > 4 ? (unsigned)strtoul(argv[4], NULL, 16) : (unsigned)(load + n);
    Z80 z = { 0 };
    z.read = rd;
    while (pc < end) {
        char buf[64];
        int len = z80_disasm(&z, (uint16_t)pc, buf, sizeof buf);
        printf("%04X  ", pc);
        for (int i = 0; i < 4; i++) { if (i < len) printf("%02X ", mem[(pc + i) & 0xFFFF]); else printf("   "); }
        printf(" %s\n", buf);
        pc += (unsigned)len;
    }
    return 0;
}
