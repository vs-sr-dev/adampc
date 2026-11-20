/* vdpprof.h - VDP / frame-time profiler: what a TMS9918A program does that a forgiving
 * emulator would not show.
 *
 * VRAM transfers, slot model. The VDP gives the CPU fixed access slots on every line
 * (src/vdpslots.h, from the openMSX timing diagram built on TI's 1978 master timing chart):
 * Graphics I/II one slot every 16 memory cycles at worst (32 VDP cycles, the "once every 16
 * memory cycles" of the TMS9918A Data Manual 2.1.5), Text one in three, the vertical border
 * and a blanked screen one in two. After a transfer request (a data port read or write, or an
 * address setup for reading) the VDP needs a delay before it can use a slot: the manual's
 * "2 µs", which openMSX reads as 8-13 VDP cycles, 11 most likely. The transfer happens in the
 * first CPU slot after that delay. A new request that arrives before that slot finds the
 * previous transfer still pending:
 *   lost        before the slot even with an 8-cycle delay
 *   likely lost before the slot with the 11-cycle delay (the best guess)
 *   marginal    only with a 12-13 cycle delay
 * The model gives 43 VDP cycles = 28.7 Z80 cycles as the Graphics worst case and 11.3 for Text,
 * the usual "29 cycles" and "12 cycles" rules. What the real chip does with the earlier byte is
 * not documented, so adamPC only reports it and keeps every write.
 * Line alignment: the frame flag is set at VDP cycle 319 of line 191 (end of the active
 * display), which is where adamPC ends its line 191. Lines 0-191 and 261 (sprite prefetch for
 * line 0) use the mode's pattern, the other lines and a blanked screen the border pattern.
 *
 * Frame time: the profiler watches for the program's idle loop (a short backward jump taken
 * again and again with no memory or I/O write in between, or HALT). The busy time of a frame
 * is from the frame flag (end of line 191) to the start of the idle loop the Z80 is in at the
 * next frame flag; no idle loop there = the frame overran.
 *
 * Races: an NMI handler that touches the VDP control port while the main program had
 * written only the first address byte, or that moves the address while the main program is
 * in the middle of a data transfer; status register reads outside the NMI handler that find
 * F = 1 while the interrupt is enabled (data manual 2.3: asynchronous reads take the flag; on a
 * ColecoVision the NMI is edge-triggered and still fires, but its handler then reads F, 5S and
 * C as 0); an NMI handler that returns with another MegaCart bank mapped than it found. */
#ifndef VDPPROF_H
#define VDPPROF_H
#include <stdint.h>
#include <stdio.h>

enum { VP_DATA_W, VP_DATA_R, VP_SETUP_R, VP_SETUP_W, VP_REG_W, VP_STATUS_R, VP_STATUS_F /* read found F = 1 */, VP_CTRL_1ST };

#define VP_SITES 64

/* a program location and what happened there: n = lost (or the count, for races) */
typedef struct VpSite { uint32_t key; uint32_t n, likely, marginal; double min_gap_us; } VpSite;

typedef struct VdpProf {
    int enabled;
    FILE *csv;
    int log_left;                  /* problems still printed one by one */

    /* VRAM transfers */
    uint64_t last_ps;              /* time of the last transfer request */
    uint64_t pend_ps[3];           /* its slot with an 8, 11, 13-cycle delay */
    int have_last;

    /* frame time */
    uint64_t fflag_ps;             /* last frame flag */
    uint64_t idle_start_ps;
    int idle;                      /* in an idle loop now */
    uint16_t loop_target;
    int loop_streak;
    uint64_t loop_first_ps;
    uint32_t writes, loop_writes;  /* memory/I/O writes seen (idle detection) */

    /* NMI handler */
    int in_nmi;
    uint16_t nmi_ret_pc, nmi_ret_sp, nmi_main_pc;
    uint64_t nmi_start_ps, nmi_ps;
    int nmi_latch_half;            /* NMI arrived between the two control bytes */
    int nmi_touched_ctrl;
    int cur_bank;                  /* MegaCart bank now (-1 = no MegaCart), set by the machine */
    int nmi_bank;                  /* bank the NMI interrupted */
    int addr_clobbered;            /* handler moved the address under the main program */
    uint16_t clobber_main_pc;

    /* per-interval counters (frame flag to frame flag) */
    uint32_t n_access, n_lost, n_likely, n_marginal, n_race, n_async, n_bankrace;
    double min_gap_us;
    uint32_t n_5s_lines, n_dropped;

    /* totals */
    uint64_t intervals, overruns;
    double busy_sum, busy_max, nmi_max_us;
    uint64_t t_access, t_lost, t_likely, t_marginal, t_race, t_async, t_5s_lines, t_dropped, t_5s_frames, t_bankrace;
    double t_min_gap;
    VpSite sites[VP_SITES]; int nsites;
    VpSite races[16]; int nraces;
    VpSite bankraces[16]; int nbankraces;   /* key: interrupted PC + the bank it had */
    VpSite asyncs[16]; int nasync;

    /* --prof-pc: Z80 cycles per instruction address; 0x0000-0xBFFF direct, then the
     * MegaCart window 0xC000-0xFFFF once per bank (64 x 16K) */
    uint32_t *pc_cycles;
} VdpProf;

#define VP_PC_SLOTS (0xC000 + 64 * 0x4000)
static inline void vdpprof_pc(VdpProf *p, uint16_t pc, int bank, int cycles)
{
    if (!p->pc_cycles) return;
    size_t i = pc < 0xC000 || bank < 0 ? pc : 0xC000 + (size_t)bank * 0x4000 + (pc - 0xC000);
    if (i < VP_PC_SLOTS) p->pc_cycles[i] += (uint32_t)cycles;
}
/* write the histogram: "PC BANK CYCLES" lines, BANK = - outside the MegaCart window */
int vdpprof_write_pc(VdpProf *p, const char *path, int banked);

struct TMS9918;
void vdpprof_init(VdpProf *p);
/* a VDP port access at time t (ps) by the instruction at pc (bank = MegaCart bank, -1 none);
 * frame_start = start of adamPC's line 0 */
void vdpprof_access(VdpProf *p, const struct TMS9918 *v, int kind, uint64_t t, uint64_t frame_start,
                    uint16_t pc, int bank);
/* after every instruction: pc before/after, sp after */
void vdpprof_step(VdpProf *p, uint16_t pc0, uint16_t pc1, uint16_t sp1, int halted, uint64_t t);
/* the Z80 accepted an NMI that interrupted main_pc; sp1 = SP after the push */
void vdpprof_nmi(VdpProf *p, const struct TMS9918 *v, uint16_t main_pc, uint16_t sp1, uint64_t t);
static inline void vdpprof_write(VdpProf *p) { p->writes++; }
/* end of line 191: the frame flag is set; closes the previous interval */
void vdpprof_frame_flag(VdpProf *p, struct TMS9918 *v, uint64_t t, uint64_t frame_no);
void vdpprof_summary(VdpProf *p, FILE *out);

#endif
