/* eosvol.h - build an empty EOS volume (what a newly initialised data pack or disk holds). */
#ifndef EOSVOL_H
#define EOSVOL_H
#include <stdint.h>

/* blocks: nblocks x 1K logical blocks. eos_chip: the 16K EOS chip (u22), whose directory
 * template is used. Returns 0 if the chip does not hold the template where expected. */
int  eos_new_volume(const uint8_t *eos_chip, uint8_t *blocks, int nblocks, int dirsize, const char *name);
/* lay logical blocks out in a .dsk image (sector ID order, 40 tracks x 8 x 512) */
void eos_blocks_to_disk(const uint8_t *blocks, int nblocks, uint8_t *dsk);

#endif
