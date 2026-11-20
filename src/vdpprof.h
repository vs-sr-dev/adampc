/* vdpprof.h - VDP / frame-time profiler: what a TMS9918A program does that a forgiving
 * emulator would not show.
 *
 * VRAM access spacing (TMS9918A/28A/29A Data Manual, Nov 1982, 2.1.5 and Table 2-2): after a
 * VRAM transfer request the VDP needs about 2 µs, plus the wait for a CPU access window.
 * Total worst case: Graphics I/II active display 8 µs, Text 3.1 µs, Multicolor 3.5 µs; with
 * the screen blanked (R1 bit 6 = 0) or within 4300 µs after the frame interrupt, 2 µs.
 * An access closer than 2 µs to the previous one is always too fast ("hard"); one closer than
 * the worst case of the mode is not guaranteed and depends on where the access windows fall
 * in the line ("risky"). The exact slot positions are not modelled.
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
 * C as 0). */
#ifndef VDPPROF_H
#define VDPPROF_H
#include <stdint.h>
#include <stdio.h>

enum { VP_DATA_W, VP_DATA_R, VP_SETUP_R, VP_SETUP_W, VP_REG_W, VP_STATUS_R, VP_STATUS_F /* read found F = 1 */, VP_CTRL_1ST };

#define VP_SITES 64

typedef struct VpSite { uint32_t key; uint32_t hard, risky; double min_gap_us; } VpSite;

typedef struct VdpProf {
    int enabled;
    FILE *csv;
    int log_left;                  /* violations still printed one by one */

    /* VRAM spacing */
    uint64_t last_ps;              /* time of the last transfer request */
    double last_limit_us;          /* worst case the VDP needs after it */
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
    int addr_clobbered;            /* handler moved the address under the main program */
    uint16_t clobber_main_pc;

    /* per-interval counters (frame flag to frame flag) */
    uint32_t n_access, n_hard, n_risky, n_race, n_async;
    double min_gap_us;
    uint32_t n_5s_lines, n_dropped;

    /* totals */
    uint64_t intervals, overruns;
    double busy_sum, busy_max, nmi_max_us;
    uint64_t t_access, t_hard, t_risky, t_race, t_async, t_5s_lines, t_dropped, t_5s_frames;
    double t_min_gap;
    VpSite sites[VP_SITES]; int nsites;
    VpSite races[16]; int nraces;
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
/* a VDP port access at time t (ps) by the instruction at pc (bank = MegaCart bank, -1 none) */
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
