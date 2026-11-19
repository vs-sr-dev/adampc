/* eosvol.c - fresh EOS volumes for new media. See eosvol.h.
 *
 * EOS INIT_TAPE_DIR (jump table FCBD -> F323 in RAM, EOS chip offset 0x1323) takes
 * A = device, C = directory blocks, DE = volume blocks, HL = name ending in 0x03. It copies
 * a 0x68-byte template from the EOS chip (offset 0x13DA: volume entry with the 55 AA 00 FF
 * signature, then BOOT, DIRECTORY and BLOCKS LEFT), writes the name (12 bytes at most, the
 * last one replaced by 0x03 if it is that long), OR's 0x80 into the directory size, stores
 * the volume size, sets DIRECTORY alloc/used = C and BLOCKS LEFT = start C+1, alloc
 * size-(C+1), then writes directory block 1 and zeroes blocks 2..C. Block 0 is left to the
 * caller: whoever initialises the medium writes its own boot block there.
 */
#include "eosvol.h"
#include <string.h>

#define TEMPLATE_OFS  0x13DA
#define TEMPLATE_LEN  0x68

/* boot block for an empty volume: make the RAM NMI vector a RETN, then EOS GOTO_WP
 * (FCE7): booting an empty medium lands in SmartWriter */
static const uint8_t boot_to_wp[] = {
    0x01, 0xED, 0x45,           /* LD BC,45EDh   (ED 45 = RETN)  */
    0xED, 0x43, 0x66, 0x00,     /* LD (0066h),BC                 */
    0xC3, 0xE7, 0xFC,           /* JP FCE7h      (GOTO_WP)       */
};

int eos_new_volume(const uint8_t *eos_chip, uint8_t *blocks, int nblocks, int dirsize, const char *name)
{
    if (nblocks < dirsize + 2 || dirsize < 1 || dirsize > 0x7F) return 0;
    memset(blocks, 0, (size_t)nblocks * 1024);
    memcpy(blocks, boot_to_wp, sizeof boot_to_wp);

    uint8_t *d = blocks + 1024;
    memcpy(d, eos_chip + TEMPLATE_OFS, TEMPLATE_LEN);
    if (d[0x0D] != 0x55 || d[0x0E] != 0xAA) return 0;      /* not the EOS chip we know */
    int i = 0;
    for (; i < 12 && name[i] && name[i] != 0x03; i++) d[i] = (uint8_t)name[i];
    d[i < 12 ? i : 11] = 0x03;
    d[0x0C] = (uint8_t)(dirsize | 0x80);
    d[0x11] = (uint8_t)nblocks; d[0x12] = (uint8_t)(nblocks >> 8);
    d[0x45] = d[0x47] = (uint8_t)dirsize;                      /* DIRECTORY alloc / used */
    d[0x5B] = (uint8_t)(dirsize + 1);                          /* BLOCKS LEFT start */
    int left = nblocks - (dirsize + 1);
    d[0x5F] = (uint8_t)left; d[0x60] = (uint8_t)(left >> 8);   /* BLOCKS LEFT alloc */
    return 1;
}

/* disk drive ROM: block b = track b/4, logical sectors (b%4)*2+1, +2 -> IDs through 1,6,3,8,5,2,7,4 */
void eos_blocks_to_disk(const uint8_t *blocks, int nblocks, uint8_t *dsk)
{
    static const int skew[8] = { 1, 6, 3, 8, 5, 2, 7, 4 };
    for (int b = 0; b < nblocks; b++)
        for (int k = 0; k < 2; k++) {
            int t = b / 4, id = skew[(b % 4) * 2 + k];
            memcpy(dsk + (size_t)(t * 8 + id - 1) * 512, blocks + (size_t)b * 1024 + k * 512, 512);
        }
}
