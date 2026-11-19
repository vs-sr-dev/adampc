/* wd2793.h - Western Digital WD2793 floppy disk controller + a 5.25" drive with a disk image.
 *
 * Event-driven at the controller clock (1 MHz on the ADAM disk board = the 6801 E clock):
 * the disk turns at 300 rpm (200 ms per turn), MFM data runs at 250 kbit/s (one byte every
 * 32 µs; 500 kbit/s = 16 µs on HD media), the head steps at the programmed rate. INTRQ and
 * DRQ come out as levels. Side and data rate come from the board (side/hd fields): a read
 * on a side the disk does not have, or at the wrong data rate, finds no sector.
 * Disk image: raw sectors, cylinder by cylinder, side 0 then side 1, sector IDs 1..N in order.
 */
#ifndef WD2793_H
#define WD2793_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t *data;
    size_t size;
    int tracks, sides, sectors, secsize;
    int hd;                     /* high-density media (1.44M): 500 kbit/s */
    int present, dirty, wprot;
    char path[1024];
} Floppy;

typedef struct WD2793 {
    Floppy *disk;
    uint8_t cmd, track, sector, data;
    uint8_t st;                 /* status flags other than BUSY/DRQ/TR00/INDEX */
    uint8_t busy, drq, intrq;
    int type;                   /* type of the last command (1..4) */
    int cyl, dir;               /* head position, last step direction */
    int side;                   /* head selected by the drive board (WD2793 has no SSO pin) */
    int hd;                     /* data rate selected by the board: 1 = 500 kbit/s */
    int state;
    uint64_t next;              /* E cycle of the next state-machine event */
    int pos, target, revs;
    uint8_t *ptr;               /* sector being transferred */
    uint8_t idbuf[6];
    uint8_t *wtrack; int wtlen;  /* write track capture */
    void *ctx;
    void (*intrq_changed)(void *ctx, int level);
    int trace;
} WD2793;

void wd_reset(WD2793 *w);
uint8_t wd_read(WD2793 *w, int reg, uint64_t now);      /* reg: 0 status 1 track 2 sector 3 data */
void wd_write(WD2793 *w, int reg, uint8_t v, uint64_t now);
void wd_update(WD2793 *w, uint64_t now);                 /* run the state machine up to now */

int  floppy_load(Floppy *f, const char *path);
int  floppy_save(Floppy *f);
void floppy_free(Floppy *f);

#endif
