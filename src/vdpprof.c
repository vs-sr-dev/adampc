/* vdpprof.c - VDP / frame-time profiler. See vdpprof.h. */
#include "vdpprof.h"
#include "tms9918.h"
#include "vdpslots.h"
#include <math.h>
#include <string.h>

#define PS_PER_VDP   (279365.0 / 1.5)          /* VDP cycle: 5.369 MHz, 1.5 per Z80 cycle */
#define LINE_VDP     342
#define FFLAG_CYCLE  319                       /* end of the active display: the frame flag */
#define ALIGN        (LINE_VDP - FFLAG_CYCLE)  /* adamPC's line start is VDP cycle 319 of the line before */

static const int delays[3] = { 8, 11, 13 };   /* "2 us" after a request: range and best guess */

/* next[pattern][cycle]: first CPU slot starting at or after that cycle of the line, -1 none */
enum { PAT_BORDER, PAT_GRAPHICS, PAT_MULTICOLOR, PAT_TEXT };
static int16_t next_slot[4][LINE_VDP + 1];

static void build_slots(void)
{
    char mc[172];
    /* Multicolor reads no colour table: the article and the manual's "at least once out of
     * every four memory cycles" give those reads to the CPU */
    for (int i = 0; i < 171; i++) mc[i] = vdp_slots_character[i] == 'c' ? 'C' : vdp_slots_character[i];
    const char *pat[4] = { vdp_slots_border, vdp_slots_character, mc, vdp_slots_text };
    for (int k = 0; k < 4; k++) {
        int nx = -1;
        next_slot[k][LINE_VDP] = -1;
        for (int c = LINE_VDP - 1; c >= 0; c--) {
            /* entry i starts at cycle 2i + 1 (cycle 341 = entry 170) */
            if ((c & 1) && pat[k][(c - 1) / 2] == 'C') nx = c;
            next_slot[k][c] = (int16_t)nx;
        }
    }
}

static int line_pattern(const TMS9918 *v, long line)
{
    long l = ((line % 262) + 262) % 262;
    if (!(v->reg[1] & 0x40)) return PAT_BORDER;                /* display off */
    if (l >= 192 && l != 261) return PAT_BORDER;
    if (v->reg[1] & 0x10) return PAT_TEXT;
    if (v->reg[1] & 0x08) return PAT_MULTICOLOR;
    return PAT_GRAPHICS;
}

/* time of the first CPU slot at or after time t (ps) */
static uint64_t slot_after(const TMS9918 *v, uint64_t t, uint64_t frame_start)
{
    double u = ((double)t - (double)frame_start) / PS_PER_VDP - ALIGN;  /* VDP cycles from line 0, cycle 0 */
    long cyc = (long)ceil(u);
    for (int k = 0; k < 3; k++) {
        long line = (long)floor((double)cyc / LINE_VDP);
        int c = (int)(cyc - line * LINE_VDP);
        int s = next_slot[line_pattern(v, line)][c];
        if (s >= 0) return frame_start + (uint64_t)llround(((double)(line * LINE_VDP + s) + ALIGN) * PS_PER_VDP);
        cyc = (line + 1) * LINE_VDP;
    }
    return t;
}

void vdpprof_init(VdpProf *p)
{
    FILE *csv = p->csv;
    int log = p->log_left;
    uint32_t *pcc = p->pc_cycles;
    memset(p, 0, sizeof *p);
    p->csv = csv; p->log_left = log; p->enabled = 1; p->pc_cycles = pcc;
    p->min_gap_us = p->t_min_gap = 1e9;
    p->cur_bank = -1;
    build_slots();
    if (p->csv)
        fprintf(p->csv, "frame,interval_us,busy_us,busy_pct,overrun,nmi_us,vram_accesses,lost,likely_lost,marginal,"
                        "min_gap_us,races,async_status,lines_over4,sprites_dropped,bank_races\n");
}

static VpSite *site(VpSite *tab, int *n, int cap, uint32_t key)
{
    for (int i = 0; i < *n; i++) if (tab[i].key == key) return &tab[i];
    if (*n == cap) return NULL;
    VpSite *s = &tab[(*n)++];
    s->key = key; s->n = s->likely = s->marginal = 0; s->min_gap_us = 1e9;
    return s;
}

static uint32_t key_of(uint16_t pc, int bank) { return (uint32_t)(bank + 1) << 16 | pc; }

static void race(VdpProf *p, uint16_t pc, int bank)
{
    p->n_race++;
    VpSite *s = site(p->races, &p->nraces, 16, key_of(pc, bank));
    if (s) s->n++;
}

void vdpprof_access(VdpProf *p, const TMS9918 *v, int kind, uint64_t t, uint64_t frame_start, uint16_t pc, int bank)
{
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
        if (s) s->n++;
    }

    /* --- VRAM transfer spacing --- */
    if (kind != VP_DATA_W && kind != VP_DATA_R && kind != VP_SETUP_R) return;
    p->n_access++;
    if (p->have_last) {
        double gap = (double)(t - p->last_ps) / 1e6;
        if (gap < p->min_gap_us) p->min_gap_us = gap;
        /* the previous transfer is still pending if its slot has not started yet */
        int level = t < p->pend_ps[0] ? 3 : t < p->pend_ps[1] ? 2 : t < p->pend_ps[2] ? 1 : 0;
        if (level) {
            if (level == 3) p->n_lost++; else if (level == 2) p->n_likely++; else p->n_marginal++;
            VpSite *s = site(p->sites, &p->nsites, VP_SITES, key_of(pc, bank));
            if (s) {
                if (level == 3) s->n++; else if (level == 2) s->likely++; else s->marginal++;
                if (gap < s->min_gap_us) s->min_gap_us = gap;
            }
            if (p->log_left > 0) {
                static const char *what[4] = { "", "marginal   ", "likely lost", "lost       " };
                double u = ((double)t - (double)frame_start) / PS_PER_VDP - ALIGN;
                long line = (long)floor(u / LINE_VDP);
                p->log_left--;
                /* the slot of the previous transfer, with the delay that makes this request too early */
                fprintf(stderr, "[vdp] %s: %s %.2f us after the previous request, line %ld cycle %ld, the previous "
                        "transfer's slot (%d-cycle delay) %.2f us later  PC=%04X%s", what[level],
                        kind == VP_DATA_W ? "write" : kind == VP_DATA_R ? "read " : "setup", gap, line,
                        (long)(u - line * LINE_VDP), delays[3 - level],
                        ((double)p->pend_ps[3 - level] - (double)t) / 1e6, pc, bank >= 0 ? "" : "\n");
                if (bank >= 0) fprintf(stderr, " bank %d\n", bank);
            }
        }
    }
    for (int i = 0; i < 3; i++) p->pend_ps[i] = slot_after(v, t + (uint64_t)llround(delays[i] * PS_PER_VDP), frame_start);
    p->last_ps = t; p->have_last = 1;
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
    p->nmi_bank = p->cur_bank;
}

void vdpprof_step(VdpProf *p, uint16_t pc0, uint16_t pc1, uint16_t sp1, int halted, uint64_t t)
{
    if (p->in_nmi) {
        if (pc1 == p->nmi_ret_pc && sp1 == p->nmi_ret_sp) {
            p->in_nmi = 0;
            p->nmi_ps += t - p->nmi_start_ps;
            if (p->nmi_touched_ctrl) { p->addr_clobbered = 1; p->clobber_main_pc = p->nmi_main_pc; }
            /* the handler switched MegaCart banks and did not put back the one it found: the
             * main program resumes reading from another bank */
            if (p->nmi_bank >= 0 && p->cur_bank != p->nmi_bank) {
                p->n_bankrace++;
                VpSite *s = site(p->bankraces, &p->nbankraces, 16, key_of(p->nmi_main_pc, p->nmi_bank));
                if (s) s->n++;
                if (p->log_left > 0) {
                    p->log_left--;
                    fprintf(stderr, "[nmi] returned to PC=%04X with bank %d mapped, it interrupted bank %d\n",
                            p->nmi_main_pc, p->cur_bank, p->nmi_bank);
                }
            }
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
        p->t_access += p->n_access; p->t_lost += p->n_lost; p->t_likely += p->n_likely; p->t_marginal += p->n_marginal;
        p->t_race += p->n_race; p->t_async += p->n_async; p->t_bankrace += p->n_bankrace;
        p->t_5s_lines += p->n_5s_lines; p->t_dropped += p->n_dropped;
        if (p->n_5s_lines) p->t_5s_frames++;
        if (p->min_gap_us < p->t_min_gap) p->t_min_gap = p->min_gap_us;
        if (p->csv)
            fprintf(p->csv, "%llu,%.0f,%.0f,%.1f,%d,%.0f,%u,%u,%u,%u,%.2f,%u,%u,%u,%u,%u\n",
                    (unsigned long long)frame_no, interval, busy, pct, !idle_now, nmi_us, p->n_access, p->n_lost,
                    p->n_likely, p->n_marginal, p->min_gap_us < 1e8 ? p->min_gap_us : -1.0, p->n_race, p->n_async,
                    p->n_5s_lines, p->n_dropped, p->n_bankrace);
    }
    p->fflag_ps = t;
    p->nmi_ps = 0;
    p->n_access = p->n_lost = p->n_likely = p->n_marginal = p->n_race = p->n_async = p->n_5s_lines = p->n_dropped = p->n_bankrace = 0;
    p->min_gap_us = 1e9;
}

static void print_sites(FILE *out, const char *title, VpSite *tab, int n, int with_gap)
{
    if (!n) return;
    /* most frequent first */
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (tab[j].n + tab[j].likely + tab[j].marginal > tab[i].n + tab[i].likely + tab[i].marginal) { VpSite x = tab[i]; tab[i] = tab[j]; tab[j] = x; }
    fprintf(out, "  %s\n", title);
    for (int i = 0; i < n && i < 12; i++) {
        uint16_t pc = (uint16_t)tab[i].key;
        int bank = (int)(tab[i].key >> 16) - 1;
        char b[16] = "";
        if (bank >= 0) snprintf(b, sizeof b, " bank %2d", bank);
        if (with_gap)
            fprintf(out, "    PC %04X%s: %u lost, %u likely lost, %u marginal, closest %.2f us\n", pc, b,
                    tab[i].n, tab[i].likely, tab[i].marginal, tab[i].min_gap_us);
        else
            fprintf(out, "    PC %04X%s: %u\n", pc, b, tab[i].n);
    }
}

void vdpprof_summary(VdpProf *p, FILE *out)
{
    if (!p->intervals) { fprintf(out, "vdp profile: no complete frame\n"); return; }
    fprintf(out, "vdp profile over %llu frames\n", (unsigned long long)p->intervals);
    fprintf(out, "  frame time: average %.1f%% busy, worst %.1f%%, %llu frames overran (no idle loop at the next frame flag)\n",
            p->busy_sum / (double)p->intervals, p->busy_max, (unsigned long long)p->overruns);
    fprintf(out, "  NMI handler: longest %.0f us per frame\n", p->nmi_max_us);
    fprintf(out, "  VRAM transfers: %llu, closest %.2f us apart; a request before the previous one's slot (delay 8/11/13 cycles):\n"
                 "    %llu lost, %llu likely lost, %llu marginal\n",
            (unsigned long long)p->t_access, p->t_min_gap < 1e8 ? p->t_min_gap : 0.0,
            (unsigned long long)p->t_lost, (unsigned long long)p->t_likely, (unsigned long long)p->t_marginal);
    print_sites(out, "where (instruction of the request that came too early):", p->sites, p->nsites, 1);
    fprintf(out, "  sprites: %llu frames with lines over 4 sprites, %llu such lines, %llu sprite-lines lost\n",
            (unsigned long long)p->t_5s_frames, (unsigned long long)p->t_5s_lines, (unsigned long long)p->t_dropped);
    fprintf(out, "  NMI races on the VDP address/latch: %llu\n", (unsigned long long)p->t_race);
    print_sites(out, "where (main-program PC):", p->races, p->nraces, 0);
    fprintf(out, "  status reads outside the NMI handler that took a set frame flag: %llu\n", (unsigned long long)p->t_async);
    print_sites(out, "where:", p->asyncs, p->nasync, 0);
    fprintf(out, "  NMI handler returned with another MegaCart bank mapped: %llu\n", (unsigned long long)p->t_bankrace);
    print_sites(out, "where (interrupted PC, bank it had mapped):", p->bankraces, p->nbankraces, 0);
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
