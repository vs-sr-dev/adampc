/* vdpprof.c - VDP / frame-time profiler. See vdpprof.h. */
#include "vdpprof.h"
#include "tms9918.h"
#include <string.h>

#define LINE_US   (228.0 / 3.579545)          /* one scanline in µs */
#define ACTIVE_US (192.0 * LINE_US)           /* frame start to the frame flag */
#define VBLANK_FREE_US 4300.0                 /* data manual: no waiting for 4300 µs after INT */

void vdpprof_init(VdpProf *p)
{
    FILE *csv = p->csv;
    int log = p->log_left;
    uint32_t *pcc = p->pc_cycles;
    memset(p, 0, sizeof *p);
    p->csv = csv; p->log_left = log; p->enabled = 1; p->pc_cycles = pcc;
    p->min_gap_us = p->t_min_gap = 1e9;
    if (p->csv)
        fprintf(p->csv, "frame,interval_us,busy_us,busy_pct,overrun,nmi_us,vram_accesses,hard,risky,min_gap_us,"
                        "races,async_status,lines_over4,sprites_dropped\n");
}

/* worst case wait after a transfer request made at time t (µs into the frame) */
static double limit_us(const TMS9918 *v, double tf)
{
    if (!(v->reg[1] & 0x40)) return 2.0;                         /* blanked */
    if (tf >= ACTIVE_US && tf < ACTIVE_US + VBLANK_FREE_US) return 2.0;
    if (v->reg[1] & 0x10) return 3.1;                            /* text */
    if (v->reg[1] & 0x08) return 3.5;                            /* multicolor */
    return 8.0;                                                  /* graphics I / II */
}

static VpSite *site(VpSite *tab, int *n, int cap, uint32_t key)
{
    for (int i = 0; i < *n; i++) if (tab[i].key == key) return &tab[i];
    if (*n == cap) return NULL;
    VpSite *s = &tab[(*n)++];
    s->key = key; s->hard = s->risky = 0; s->min_gap_us = 1e9;
    return s;
}

static uint32_t key_of(uint16_t pc, int bank) { return (uint32_t)(bank + 1) << 16 | pc; }

static void race(VdpProf *p, uint16_t pc, int bank)
{
    p->n_race++;
    VpSite *s = site(p->races, &p->nraces, 16, key_of(pc, bank));
    if (s) s->hard++;
}

void vdpprof_access(VdpProf *p, const TMS9918 *v, int kind, uint64_t t, uint64_t frame_start, uint16_t pc, int bank)
{
    double tf = (double)(t - frame_start) / 1e6;

    /* --- races with the NMI handler --- */
    if (p->in_nmi) {
        if (kind != VP_DATA_W && kind != VP_DATA_R) {
            if (p->nmi_latch_half) { race(p, p->nmi_main_pc, bank); p->nmi_latch_half = 0; }
            if (kind != VP_STATUS_R && kind != VP_STATUS_F) p->nmi_touched_ctrl = 1;
        }
    } else if (p->addr_clobbered) {
        if (kind == VP_DATA_W || kind == VP_DATA_R) race(p, pc, bank);
        p->addr_clobbered = 0;
    }
    if (kind == VP_STATUS_F && !p->in_nmi && (v->reg[1] & 0x20)) {
        p->n_async++;
        VpSite *s = site(p->asyncs, &p->nasync, 16, key_of(pc, bank));
        if (s) s->hard++;
    }

    /* --- VRAM transfer spacing --- */
    if (kind != VP_DATA_W && kind != VP_DATA_R && kind != VP_SETUP_R) return;
    p->n_access++;
    if (p->have_last) {
        double gap = (double)(t - p->last_ps) / 1e6;
        if (gap < p->min_gap_us) p->min_gap_us = gap;
        int hard = gap < 2.0, risky = !hard && gap < p->last_limit_us;
        if (hard || risky) {
            if (hard) p->n_hard++; else p->n_risky++;
            VpSite *s = site(p->sites, &p->nsites, VP_SITES, key_of(pc, bank));
            if (s) { if (hard) s->hard++; else s->risky++; if (gap < s->min_gap_us) s->min_gap_us = gap; }
            if (p->log_left > 0) {
                p->log_left--;
                fprintf(stderr, "[vdp] %s %s at line %3d: %.2f us after the previous transfer (needs %.1f)  PC=%04X%s",
                        hard ? "HARD " : "risky", kind == VP_DATA_W ? "write" : kind == VP_DATA_R ? "read " : "setup",
                        (int)(tf / LINE_US), gap, hard ? 2.0 : p->last_limit_us, pc, bank >= 0 ? "" : "\n");
                if (bank >= 0) fprintf(stderr, " bank %d\n", bank);
            }
        }
    }
    p->last_ps = t; p->last_limit_us = limit_us(v, tf); p->have_last = 1;
}

void vdpprof_nmi(VdpProf *p, const TMS9918 *v, uint16_t main_pc, uint16_t sp1, uint64_t t)
{
    p->writes++;                             /* the return address push */
    if (p->in_nmi) return;                   /* NMI inside the handler: keep the outer one */
    p->in_nmi = 1;
    p->nmi_ret_pc = main_pc; p->nmi_ret_sp = (uint16_t)(sp1 + 2); p->nmi_main_pc = main_pc;
    p->nmi_start_ps = t;
    p->nmi_latch_half = v->latch_full;
    p->nmi_touched_ctrl = 0;
}

void vdpprof_step(VdpProf *p, uint16_t pc0, uint16_t pc1, uint16_t sp1, int halted, uint64_t t)
{
    if (p->in_nmi) {
        if (pc1 == p->nmi_ret_pc && sp1 == p->nmi_ret_sp) {
            p->in_nmi = 0;
            p->nmi_ps += t - p->nmi_start_ps;
            if (p->nmi_touched_ctrl) { p->addr_clobbered = 1; p->clobber_main_pc = p->nmi_main_pc; }
            p->loop_streak = 0; p->idle = 0;
        }
        return;
    }
    if (halted) {
        if (!p->idle) { p->idle = 2; p->idle_start_ps = t; p->loop_writes = p->writes; }
        return;
    }
    if (p->idle == 2) p->idle = 0;
    if (pc1 < pc0 && pc0 - pc1 <= 32) {            /* short backward jump */
        if (pc1 == p->loop_target && p->writes == p->loop_writes) {
            if (++p->loop_streak >= 2 && !p->idle) { p->idle = 1; p->idle_start_ps = p->loop_first_ps; }
        } else {
            p->loop_target = pc1; p->loop_streak = 0; p->loop_first_ps = t; p->idle = 0;
        }
        p->loop_writes = p->writes;
    }
}

void vdpprof_frame_flag(VdpProf *p, TMS9918 *v, uint64_t t, uint64_t frame_no)
{
    p->n_5s_lines += v->prof_5s_lines; p->n_dropped += v->prof_dropped;
    v->prof_5s_lines = v->prof_dropped = 0;
    if (p->fflag_ps) {
        double interval = (double)(t - p->fflag_ps) / 1e6;
        int idle_now = p->idle && p->writes == p->loop_writes;
        double busy = interval;
        if (idle_now) busy = p->idle_start_ps > p->fflag_ps ? (double)(p->idle_start_ps - p->fflag_ps) / 1e6 : 0.0;
        double pct = busy * 100.0 / interval, nmi_us = (double)p->nmi_ps / 1e6;
        p->intervals++;
        if (!idle_now) p->overruns++;
        p->busy_sum += pct;
        if (pct > p->busy_max) p->busy_max = pct;
        if (nmi_us > p->nmi_max_us) p->nmi_max_us = nmi_us;
        p->t_access += p->n_access; p->t_hard += p->n_hard; p->t_risky += p->n_risky;
        p->t_race += p->n_race; p->t_async += p->n_async;
        p->t_5s_lines += p->n_5s_lines; p->t_dropped += p->n_dropped;
        if (p->n_5s_lines) p->t_5s_frames++;
        if (p->min_gap_us < p->t_min_gap) p->t_min_gap = p->min_gap_us;
        if (p->csv)
            fprintf(p->csv, "%llu,%.0f,%.0f,%.1f,%d,%.0f,%u,%u,%u,%.2f,%u,%u,%u,%u\n",
                    (unsigned long long)frame_no, interval, busy, pct, !idle_now, nmi_us, p->n_access, p->n_hard,
                    p->n_risky, p->min_gap_us < 1e8 ? p->min_gap_us : -1.0, p->n_race, p->n_async,
                    p->n_5s_lines, p->n_dropped);
    }
    p->fflag_ps = t;
    p->nmi_ps = 0;
    p->n_access = p->n_hard = p->n_risky = p->n_race = p->n_async = p->n_5s_lines = p->n_dropped = 0;
    p->min_gap_us = 1e9;
}

static void print_sites(FILE *out, const char *title, VpSite *tab, int n, int with_gap)
{
    if (!n) return;
    /* most frequent first */
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (tab[j].hard + tab[j].risky > tab[i].hard + tab[i].risky) { VpSite x = tab[i]; tab[i] = tab[j]; tab[j] = x; }
    fprintf(out, "  %s\n", title);
    for (int i = 0; i < n && i < 12; i++) {
        uint16_t pc = (uint16_t)tab[i].key;
        int bank = (int)(tab[i].key >> 16) - 1;
        char b[16] = "";
        if (bank >= 0) snprintf(b, sizeof b, " bank %2d", bank);
        if (with_gap)
            fprintf(out, "    PC %04X%s: %u hard, %u risky, closest %.2f us\n", pc, b, tab[i].hard, tab[i].risky, tab[i].min_gap_us);
        else
            fprintf(out, "    PC %04X%s: %u\n", pc, b, tab[i].hard);
    }
}

void vdpprof_summary(VdpProf *p, FILE *out)
{
    if (!p->intervals) { fprintf(out, "vdp profile: no complete frame\n"); return; }
    fprintf(out, "vdp profile over %llu frames\n", (unsigned long long)p->intervals);
    fprintf(out, "  frame time: average %.1f%% busy, worst %.1f%%, %llu frames overran (no idle loop at the next frame flag)\n",
            p->busy_sum / (double)p->intervals, p->busy_max, (unsigned long long)p->overruns);
    fprintf(out, "  NMI handler: longest %.0f us per frame\n", p->nmi_max_us);
    fprintf(out, "  VRAM transfers: %llu, %llu hard (< 2 us apart), %llu risky (inside the mode's worst case), closest %.2f us\n",
            (unsigned long long)p->t_access, (unsigned long long)p->t_hard, (unsigned long long)p->t_risky,
            p->t_min_gap < 1e8 ? p->t_min_gap : 0.0);
    print_sites(out, "where (instruction of the too-early access):", p->sites, p->nsites, 1);
    fprintf(out, "  sprites: %llu frames with lines over 4 sprites, %llu such lines, %llu sprite-lines lost\n",
            (unsigned long long)p->t_5s_frames, (unsigned long long)p->t_5s_lines, (unsigned long long)p->t_dropped);
    fprintf(out, "  NMI races on the VDP address/latch: %llu\n", (unsigned long long)p->t_race);
    print_sites(out, "where (main-program PC):", p->races, p->nraces, 0);
    fprintf(out, "  status reads outside the NMI handler that took a set frame flag: %llu\n", (unsigned long long)p->t_async);
    print_sites(out, "where:", p->asyncs, p->nasync, 0);
}

int vdpprof_write_pc(VdpProf *p, const char *path, int banked)
{
    if (!p->pc_cycles) return 0;
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    for (size_t i = 0; i < VP_PC_SLOTS; i++) {
        if (!p->pc_cycles[i]) continue;
        if (i < 0xC000 || !banked) fprintf(f, "%04X - %u\n", (unsigned)(i & 0xFFFF), p->pc_cycles[i]);
        else fprintf(f, "%04X %d %u\n", (unsigned)(0xC000 + (i - 0xC000) % 0x4000), (int)((i - 0xC000) / 0x4000), p->pc_cycles[i]);
    }
    fclose(f);
    return 1;
}
