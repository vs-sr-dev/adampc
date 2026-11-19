/* ddp.h - Digital Data Pack: tape drives as seen by the tape 6801.
 *
 * The tape is modelled as flux on two tracks: a sequence of bit cells along the tape.
 * Position unit = 1 µs of travel at 20 ips, so a 70 µs bit cell is 70 units and at
 * 80 ips the tape moves 4 units per µs. The head returns the flux level at the current
 * position (bi-phase mark: a transition at every cell boundary, another mid-cell for a 1).
 * The image file (.ddp) holds the 256 logical 1K blocks; it is laid out on the tape with
 * the factory format the tape firmware expects (header record + data record per block).
 */
#ifndef DDP_H
#define DDP_H
#include <stdint.h>

#define DDP_BLOCKS        256
#define DDP_BLOCK_SIZE    1024
#define DDP_CELL          70          /* position units per bit cell */
#define DDP_PITCH_CELLS   12800       /* block pitch: 17.92 in */
#define DDP_LEADER_CELLS  20000
#define DDP_TRACK_CELLS   (2 * DDP_LEADER_CELLS + 128 * DDP_PITCH_CELLS)

typedef struct {
    int present;
    int format_he;                    /* 1 = "HE" layout (block 0 mid-tape), 0 = "GW" */
    uint8_t image[DDP_BLOCKS * DDP_BLOCK_SIZE];
    uint8_t *cells[2];                /* per track: bit0 level, bit1 flux, bit2 mid transition */
    int64_t pos;                      /* head position (units) */
    int vel;                          /* units per µs: 0, +-1, +-4 */
    uint64_t last;                    /* E cycle of the last position update */
    int stalled;                      /* pushed against an end of the tape */
    int dirty;
    char path[1024];
    size_t file_size;                 /* bytes in the image file (short block images allowed) */
    /* write session */
    int writing, wtrack;
    int64_t wstart;
    int64_t *wtrans; int nwtrans, capwtrans;
} Cassette;

typedef struct {
    Cassette drive[2];
    uint8_t p1, p2;                   /* last control outputs */
    uint8_t ram[1024];                /* 2114 x2 block buffer at 0x0400 */
} DDP;

void ddp_init(DDP *d);
int  ddp_insert(DDP *d, int drive, const char *path, int create_blank);
void ddp_eject(DDP *d, int drive);
int  ddp_save(DDP *d, int drive);                 /* write the image back if modified */
/* 6801 port hooks (now = E cycle of the access) */
uint8_t ddp_port_in(DDP *d, int port, uint64_t now);
void ddp_port_out(DDP *d, int port, uint8_t data, uint8_t ddr, uint64_t now);
int  ddp_motor_on(DDP *d);                        /* any drive moving (for turbo) */
extern int ddp_trace;                             /* log the tape mechanics */

#endif
