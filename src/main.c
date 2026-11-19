/* main.c - adamPC: Coleco ADAM emulator front end.
 *
 *   adampc [options]
 *     --roms DIR        ROM directory (default: roms) with adam/, adam_kb/ ...
 *     --headless        no window: run --frames frames, then write --png
 *     --frames N        frames to run headless (default 300)
 *     --png FILE        screenshot at the end (headless)
 *     --shots N         with --png: also save FILE.<frame>.png every N frames
 *     --type TEXT       type TEXT on the ADAM keyboard after --type-at frames
 *     --type-at N       frame when typing starts (default 240)
 *     --type-file FILE  type a plain text file (e.g. a BASIC listing) after --type
 *     --line-wait N     frames to wait after each line of --type-file (default 30)
 *     --type-after TEXT typed after --type-file, same escapes as --type
 *     --wav FILE        write the sound output (headless)
 *     --ddp FILE        data pack in tape drive 1 (--ddp2: drive 2); written back when modified
 *     --ddp-new         create missing --ddp files as empty EOS volumes (--disk-new: --disk files)
 *     --printer FILE    text file that receives what the printer puts on paper (+ FILE.png)
 *     --no-printer-sound  do not mix the printer mechanism noise into the audio
 *     --xram KB         memory expander: 64K, or more in 64K banks (port 0x42)
 *     --fdc-rom FILE    another 4K disk controller ROM for drive 1 (--fdc-rom2: drive 2)
 *     --cart FILE       ColecoVision cartridge: boots with a game reset (OS-7 + cartridge)
 *     --trace-net       log every AdamNet byte
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "adam.h"
#include "png.h"
#include "typer.h"
#include "eosvol.h"

static Adam adam;

/* ---------------------------------------------------------- typing automation */
int typer_busy(const Typer *t) { return (t->text && t->text[t->pos]) || t->then || t->after; }

void typer_step(Adam *a, Typer *t)
{
    if ((!t->text || !t->text[t->pos]) && t->phase == 0) {
        if (t->then) { t->text = t->then; t->then = NULL; t->pos = 0; t->raw = 1; }
        else if (t->after) { t->text = t->after; t->after = NULL; t->pos = 0; t->raw = 0; }
    }
    if (!t->text || !t->text[t->pos]) return;
    if (t->wait > 0) { t->wait--; return; }
    char ch = t->text[t->pos];
    if (t->raw && (ch == '\r' || ch == '\t') && t->phase == 0) { t->pos++; return; }
    if (!t->raw && ch == '|' && t->phase == 0) { t->wait = 60; t->pos++; return; }   /* | = 1 s pause */
    uint8_t code = (uint8_t)(ch == '\n' ? 0x0D : ch);
    int len = 1;
    /* ~XX = ADAM key code in hex (~A2 = down arrow, ~0D = RETURN, ~81 = SmartKey I) */
    if (!t->raw && ch == '~' && t->text[t->pos + 1] && t->text[t->pos + 2]) {
        char hx[3] = { t->text[t->pos + 1], t->text[t->pos + 2], 0 };
        code = (uint8_t)strtol(hx, NULL, 16);
        len = 3;
    }
    int sh = 0, ct = 0;
    int idx = adam_key_for_code(a, code, &sh, &ct);
    /* like a typist: modifiers down, key down, key up, modifiers up */
    switch (t->phase) {
    case 0:
        if (sh) adam_key(a, KEY_SHIFT, 1);
        if (ct) adam_key(a, KEY_CONTROL, 1);
        t->phase = 1; t->wait = (sh || ct) ? 2 : 0;
        break;
    case 1:
        if (idx >= 0) adam_key(a, idx, 1);
        t->phase = 2; t->wait = 3;
        break;
    case 2:
        if (idx >= 0) adam_key(a, idx, 0);
        t->phase = 3; t->wait = 1;
        break;
    default:
        adam_key(a, KEY_SHIFT, 0); adam_key(a, KEY_CONTROL, 0);
        t->phase = 0; t->wait = (t->raw && code == 0x0D) ? t->line_wait : 2; t->pos += len;
        break;
    }
}

static char *read_text_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = malloc((size_t)n + 1);
    if (s) { s[fread(s, 1, (size_t)n, f)] = 0; }
    fclose(f);
    return s;
}

static void frame_rgb(Adam *a, uint8_t *rgb)
{
    for (int i = 0; i < VDP_W * VDP_H; i++) {
        uint32_t c = tms9918_palette[a->vdp.fb[i] & 15];
        rgb[i * 3] = (uint8_t)(c >> 16); rgb[i * 3 + 1] = (uint8_t)(c >> 8); rgb[i * 3 + 2] = (uint8_t)c;
    }
}

static void write_wav(const char *path, const int16_t *s, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t data = (uint32_t)(n * 2), rate = AUDIO_RATE, byterate = AUDIO_RATE * 2;
    uint32_t riff = 36 + data, fmtlen = 16;
    uint16_t pcm = 1, ch = 1, align = 2, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmtlen, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&byterate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f); fwrite(s, 2, n, f);
    fclose(f);
}

/* --ddp-new / --disk-new: a missing image file becomes an empty EOS volume, the state of a
 * medium initialised by EOS (block 0 boots back into SmartWriter, directory in block 1) */
static int create_eos_media(const char *path, int is_disk)
{
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }                      /* exists: use it as it is */
    static uint8_t blocks[256 * 1024], dsk[160 * 1024];
    int n = is_disk ? 160 : 256;
    if (!eos_new_volume(adam.eos, blocks, n, 1, "BLANK")) { fprintf(stderr, "EOS chip: no directory template\n"); return 0; }
    const uint8_t *out = blocks;
    if (is_disk) { eos_blocks_to_disk(blocks, n, dsk); out = dsk; }
    f = fopen(path, "wb");
    if (!f || fwrite(out, 1, (size_t)n * 1024, f) != (size_t)n * 1024) { fprintf(stderr, "cannot create %s\n", path); if (f) fclose(f); return 0; }
    fclose(f);
    fprintf(stderr, "created %s: empty EOS volume, %d blocks\n", path, n);
    return 1;
}

#ifdef ADAM_SDL
int run_sdl(Adam *a, Typer *t);
#endif

int main(int argc, char **argv)
{
    const char *romdir = "roms", *png = NULL, *type = NULL, *cart = NULL, *wav = NULL, *ddp[2] = { NULL, NULL }, *disk[2] = { NULL, NULL };
    int ddp_new = 0, disk_new = 0, line_wait = 30, no_prn_sound = 0, xram_kb = 0;
    const char *type_file = NULL, *type_after = NULL, *fdcrom[2] = { NULL, NULL };
    int headless = 0, frames = 300, type_at = 240, shots = 0;
    const char *dumpram = NULL;
    int watch = -1, trz_from = -1, trz_to = -1;
    for (int i = 1; i < argc; i++) {
        const char *s = argv[i];
        const char *nx = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(s, "--roms") && nx) { romdir = nx; i++; }
        else if (!strcmp(s, "--headless")) headless = 1;
        else if (!strcmp(s, "--frames") && nx) { frames = atoi(nx); i++; }
        else if (!strcmp(s, "--png") && nx) { png = nx; i++; }
        else if (!strcmp(s, "--type") && nx) { type = nx; i++; }
        else if (!strcmp(s, "--type-at") && nx) { type_at = atoi(nx); i++; }
        else if (!strcmp(s, "--type-file") && nx) { type_file = nx; i++; }
        else if (!strcmp(s, "--type-after") && nx) { type_after = nx; i++; }
        else if (!strcmp(s, "--line-wait") && nx) { line_wait = atoi(nx); i++; }
        else if (!strcmp(s, "--wav") && nx) { wav = nx; i++; }
        else if (!strcmp(s, "--shots") && nx) { shots = atoi(nx); i++; }
        else if (!strcmp(s, "--ddp") && nx) { ddp[0] = nx; i++; }
        else if (!strcmp(s, "--ddp2") && nx) { ddp[1] = nx; i++; }
        else if (!strcmp(s, "--disk") && nx) { disk[0] = nx; i++; }
        else if (!strcmp(s, "--disk2") && nx) { disk[1] = nx; i++; }
        else if (!strcmp(s, "--fdc-rom") && nx) { fdcrom[0] = nx; i++; }
        else if (!strcmp(s, "--xram") && nx) { xram_kb = atoi(nx); i++; }
        else if (!strcmp(s, "--fdc-rom2") && nx) { fdcrom[1] = nx; i++; }
        else if (!strcmp(s, "--trace-wd")) { adam.wd[0].trace = 1; }
        else if (!strcmp(s, "--printer") && nx) { snprintf(adam.printer.path, sizeof adam.printer.path, "%s", nx); i++; }
        else if (!strcmp(s, "--trace-prn")) adam.printer.trace = 1;
        else if (!strcmp(s, "--no-printer-sound")) no_prn_sound = 1;
        else if (!strcmp(s, "--dump-ram") && nx) { dumpram = nx; i++; }
        else if (!strcmp(s, "--trace-eos")) adam.trace_eos = 1;
        else if (!strcmp(s, "--trace-z80") && nx && i + 2 < argc) { trz_from = atoi(nx); trz_to = atoi(argv[i + 2]); i += 2; }
        else if (!strcmp(s, "--watch") && nx) { watch = (int)strtol(nx, NULL, 16); i++; }
        else if (!strcmp(s, "--ddp-new")) ddp_new = 1;
        else if (!strcmp(s, "--disk-new")) disk_new = 1;
        else if (!strcmp(s, "--trace-ddp")) ddp_trace = 1;
        else if (!strcmp(s, "--trace-ddp2")) ddp_trace = 2;
        else if (!strcmp(s, "--cart") && nx) { cart = nx; i++; }
        else if (!strcmp(s, "--trace-net")) adam.net.trace = 1;
        else { fprintf(stderr, "unknown option %s\n", s); return 2; }
    }
    int trace = adam.net.trace, wdtrace = adam.wd[0].trace;
    adam_init(&adam);
    adam.net.trace = trace;
    adam.wd[0].trace = adam.wd[1].trace = wdtrace;
    adam.watch = watch;
    if (no_prn_sound) adam.printer.sound = 0;
    if (!adam_load_roms(&adam, romdir)) return 1;
    if (xram_kb && !adam_set_xram(&adam, xram_kb)) { fprintf(stderr, "--xram: 64, 128, ... 16384 (KB)\n"); return 1; }
    for (int d = 0; d < 2; d++)
        if (fdcrom[d] && !adam_set_fdc_rom(&adam, d, fdcrom[d])) { fprintf(stderr, "cannot load 4K ROM %s\n", fdcrom[d]); return 1; }
    if (disk[1]) { adam_attach_disk_drive(&adam, 1); adam.wd[1].trace = wdtrace; }
    for (int d = 0; d < 2; d++) {
        if (disk[d] && disk_new && !create_eos_media(disk[d], 1)) return 1;
        if (disk[d] && !adam_insert_disk(&adam, d, disk[d])) { fprintf(stderr, "cannot load %s\n", disk[d]); return 1; }
    }
    for (int d = 0; d < 2; d++) {
        if (ddp[d] && ddp_new && !create_eos_media(ddp[d], 0)) return 1;
        if (ddp[d] && !ddp_insert(&adam.ddp, d, ddp[d], 0)) { fprintf(stderr, "cannot load %s\n", ddp[d]); return 1; }
    }
    adam_power_on(&adam);
    if (cart) {
        if (!adam_load_cart(&adam, cart)) { fprintf(stderr, "cannot load %s\n", cart); return 1; }
        adam_game_reset(&adam);
    }

    Typer typer = { type, 0, 0, 0, 0, line_wait, NULL, type_after };
    if (type_file) {
        typer.then = read_text_file(type_file);
        if (!typer.then) { fprintf(stderr, "cannot read %s\n", type_file); return 1; }
    }
#ifdef ADAM_SDL
    if (!headless) { int r = run_sdl(&adam, &typer); adam_shutdown(&adam); return r; }
#endif
    (void)headless;
    int16_t *snd = wav ? malloc((size_t)frames * 2048 * 2) : NULL;
    size_t nsnd = 0;
    for (int f = 0; f < frames; f++) {
        if (f >= type_at) typer_step(&adam, &typer);
        adam.trace_z80 = trz_from >= 0 && f >= trz_from && f <= trz_to;
        adam_run_frame(&adam);
        if (snd) { memcpy(snd + nsnd, adam.audio, (size_t)adam.audio_n * 2); nsnd += (size_t)adam.audio_n; }
        if (shots > 0 && png && (f + 1) % shots == 0) {
            static uint8_t rgb[VDP_W * VDP_H * 3];
            char name[1100];
            snprintf(name, sizeof name, "%s.%06d.png", png, f + 1);
            frame_rgb(&adam, rgb);
            png_write_rgb(name, rgb, VDP_W, VDP_H);
        }
    }
    if (snd) {
        int peak = 0;
        for (size_t i = 0; i < nsnd; i++) if (abs(snd[i]) > peak) peak = abs(snd[i]);
        fprintf(stderr, "audio: %zu samples, peak %d\n", nsnd, peak);
        write_wav(wav, snd, nsnd);
        free(snd);
    }
    fprintf(stderr, "ran %d frames, Z80 PC=%04X map=%X, master PC=%04X, kbd PC=%04X, tape PC=%04X\n",
            frames, adam.z80.pc.w, adam.memmap, adam.master.cpu.pc, adam.kbd.cpu.pc, adam.tape.cpu.pc);
    adam_shutdown(&adam);
    if (dumpram) {
        FILE *f = fopen(dumpram, "wb");
        if (f) { fwrite(adam.ram, 1, sizeof adam.ram, f); fclose(f); }
    }
    if (png) {
        static uint8_t rgb[VDP_W * VDP_H * 3];
        frame_rgb(&adam, rgb);
        if (!png_write_rgb(png, rgb, VDP_W, VDP_H)) { fprintf(stderr, "cannot write %s\n", png); return 1; }
    }
    return 0;
}
