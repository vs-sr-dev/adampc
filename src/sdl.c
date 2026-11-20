/* sdl.c - SDL2 window, keyboard, gamepad and audio for adamPC.
 *
 * Host keyboard -> ADAM keyboard matrix (the key codes come from the keyboard ROM tables).
 *   F1-F6 SmartKeys I-VI, F7 WILD CARD, F8 UNDO, F9 MOVE/COPY, F10 STORE/GET,
 *   F11 CLEAR, F12 PRINT, Insert INSERT, Delete DELETE, Home HOME, Caps Lock LOCK,
 *   Esc ESCAPE/WP, arrows, Backspace, Tab, Enter.
 * Emulator (hold Left Alt): Alt+R computer reset, Alt+G game reset, Alt+P screenshot,
 *   Alt+T tape turbo on/off (runs ahead while a tape motor turns; on by default),
 *   drop a .ddp on the window to load it in tape drive 1, anything else as a cartridge,
 *   Alt+1..0 / Alt+- / Alt+= controller-1 keypad 1..0 * #.
 * Controller 1: gamepad, or numpad 8/4/6/2 + numpad 0 (left fire) + numpad Enter (right fire);
 *   with --coleco (no ADAM keyboard) also the arrow keys + Z (left fire) / X (right fire).
 */
#ifdef ADAM_SDL
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include "adam.h"
#include "png.h"
#include "typer.h"

static int code_to_index(Adam *a, uint8_t code)
{
    int sh, ct;
    int i = adam_key_for_code(a, code, &sh, &ct);
    return (i >= 0 && !sh && !ct) ? i : -1;
}

static int host_key_index(Adam *a, SDL_Keycode k)
{
    switch (k) {
    case SDLK_LSHIFT: case SDLK_RSHIFT: return KEY_SHIFT;
    case SDLK_LCTRL: case SDLK_RCTRL: return KEY_CONTROL;
    case SDLK_CAPSLOCK: return KEY_LOCK;
    case SDLK_HOME: return KEY_HOME;
    case SDLK_UP: return code_to_index(a, 0xA0);
    case SDLK_RIGHT: return code_to_index(a, 0xA1);
    case SDLK_DOWN: return code_to_index(a, 0xA2);
    case SDLK_LEFT: return code_to_index(a, 0xA3);
    case SDLK_F1: case SDLK_F2: case SDLK_F3: case SDLK_F4: case SDLK_F5: case SDLK_F6:
        return code_to_index(a, (uint8_t)(0x81 + (k - SDLK_F1)));
    case SDLK_F7: return code_to_index(a, 0x90);
    case SDLK_F8: return code_to_index(a, 0x91);
    case SDLK_F9: return code_to_index(a, 0x92);
    case SDLK_F10: return code_to_index(a, 0x93);
    case SDLK_F11: return code_to_index(a, 0x96);
    case SDLK_F12: return code_to_index(a, 0x95);
    case SDLK_INSERT: return code_to_index(a, 0x94);
    case SDLK_DELETE: return code_to_index(a, 0x97);
    case SDLK_BACKSPACE: return code_to_index(a, 0x08);
    case SDLK_TAB: return code_to_index(a, 0x09);
    case SDLK_RETURN: return code_to_index(a, 0x0D);
    case SDLK_ESCAPE: return code_to_index(a, 0x1B);
    case SDLK_SPACE: return code_to_index(a, 0x20);
    default: break;
    }
    if (k >= 0x21 && k < 0x7F) return code_to_index(a, (uint8_t)k);
    return -1;
}

static void screenshot(Adam *a)
{
    static uint8_t rgb[VDP_W * VDP_H * 3];
    for (int i = 0; i < VDP_W * VDP_H; i++) {
        uint32_t c = tms9918_palette[a->vdp.fb[i] & 15];
        rgb[i * 3] = (uint8_t)(c >> 16); rgb[i * 3 + 1] = (uint8_t)(c >> 8); rgb[i * 3 + 2] = (uint8_t)c;
    }
    char name[64];
    snprintf(name, sizeof name, "adampc_%06llu.png", (unsigned long long)a->frames);
    if (png_write_rgb(name, rgb, VDP_W, VDP_H)) fprintf(stderr, "saved %s\n", name);
}

int run_sdl(Adam *a, Typer *t, JoyScript *js)
{
    SDL_SetMainReady();
    if (!a->printer.path[0]) snprintf(a->printer.path, sizeof a->printer.path, "adampc_printout.txt");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow(a->coleco ? "adamPC - ColecoVision" : "adamPC - Coleco ADAM", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       VDP_W * 3, VDP_H * 3, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(ren, VDP_W * 4, VDP_H * 3);   /* ~4:3 pixel aspect */
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, VDP_W, VDP_H);

    SDL_AudioSpec want = { 0 }, have;
    want.freq = AUDIO_RATE; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 512;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev) SDL_PauseAudioDevice(dev, 0);

    SDL_GameController *pad = NULL;
    static uint32_t pix[VDP_W * VDP_H];
    int running = 1, turbo = 1;
    uint8_t kjoy = 0;          /* controller 1 bits held on the host keyboard */
    uint64_t perf = SDL_GetPerformanceFrequency(), next = SDL_GetPerformanceCounter();
    const double frame_s = 262.0 * 228.0 / Z80_HZ;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: running = 0; break;
            case SDL_CONTROLLERDEVICEADDED: if (!pad) pad = SDL_GameControllerOpen(e.cdevice.which); break;
            case SDL_DROPFILE: {
                /* .ddp -> tape drive 1, anything else -> cartridge + game reset */
                char *f = e.drop.file;
                size_t n = strlen(f);
                if (n > 4 && (!SDL_strcasecmp(f + n - 4, ".dsk"))) {
                    if (adam_insert_disk(a, 0, f)) fprintf(stderr, "disk 1: %s\n", f);
                } else if (n > 4 && (!SDL_strcasecmp(f + n - 4, ".ddp"))) {
                    ddp_eject(&a->ddp, 0);
                    if (ddp_insert(&a->ddp, 0, f, 0)) fprintf(stderr, "tape 1: %s\n", f);
                } else if (adam_load_cart(a, f)) {
                    adam_game_reset(a);
                    fprintf(stderr, "cartridge: %s\n", f);
                }
                SDL_free(f);
                break;
            }
            case SDL_KEYDOWN: case SDL_KEYUP: {
                int down = e.type == SDL_KEYDOWN;
                SDL_Keycode k = e.key.keysym.sym;
                if (e.key.keysym.mod & KMOD_LALT) {
                    if (!down || e.key.repeat) break;
                    if (k == SDLK_r) adam_power_on(a);
                    else if (k == SDLK_g) adam_game_reset(a);
                    else if (k == SDLK_p) screenshot(a);
                    else if (k == SDLK_t) { turbo = !turbo; fprintf(stderr, "tape turbo %s\n", turbo ? "on" : "off"); }
                    break;
                }
                {   /* controller 1 on the numpad (and arrows/Z/X on a ColecoVision) */
                    uint8_t bit = 0;
                    switch (k) {
                    case SDLK_KP_8: bit = 1; break;
                    case SDLK_KP_6: bit = 2; break;
                    case SDLK_KP_2: bit = 4; break;
                    case SDLK_KP_4: bit = 8; break;
                    case SDLK_KP_0: bit = 0x10; break;
                    case SDLK_KP_ENTER: bit = 0x20; break;
                    case SDLK_UP: if (a->coleco) bit = 1; break;
                    case SDLK_RIGHT: if (a->coleco) bit = 2; break;
                    case SDLK_DOWN: if (a->coleco) bit = 4; break;
                    case SDLK_LEFT: if (a->coleco) bit = 8; break;
                    case SDLK_z: if (a->coleco) bit = 0x10; break;
                    case SDLK_x: if (a->coleco) bit = 0x20; break;
                    default: break;
                    }
                    if (bit) { kjoy = (uint8_t)(down ? kjoy | bit : kjoy & ~bit); continue; }
                }
                if (e.key.repeat) break;
                int idx = host_key_index(a, k);
                if (idx >= 0) adam_key(a, idx, down);
                break;
            }
            }
        }
        /* Alt+digit: controller 1 keypad */
        {
            const Uint8 *ks = SDL_GetKeyboardState(NULL);
            int8_t kp = -1;
            if (ks[SDL_SCANCODE_LALT]) {
                for (int d = 0; d < 10; d++) {
                    SDL_Scancode sc = d == 0 ? SDL_SCANCODE_0 : (SDL_Scancode)(SDL_SCANCODE_1 + d - 1);
                    if (ks[sc]) kp = (int8_t)d;
                }
                if (ks[SDL_SCANCODE_MINUS]) kp = 10;
                if (ks[SDL_SCANCODE_EQUALS]) kp = 11;
            }
            /* the state is rebuilt every frame from what is held now: releasing a key or a
             * pad button releases the controller line */
            uint8_t j = kjoy;
            if (pad) {
                if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP) || SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) < -16000) j |= 1;
                if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) > 16000) j |= 2;
                if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) > 16000) j |= 4;
                if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) < -16000) j |= 8;
                if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A)) j |= 0x10;
                if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B)) j |= 0x20;
                if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X)) kp = 1;
                if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_Y)) kp = 2;
                if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK)) kp = 10;
                if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START)) kp = 11;
            }
            if (!js || js->next >= js->n) { a->joy[0] = j; a->keypad[0] = kp; }   /* a --joy script has the controller until it ends */
        }

        typer_step(a, t);
        if (js) joy_script_step(a, js, (int)a->frames);
        if (a->prof.enabled && a->frames % 30 == 0 && a->prof.intervals) {
            /* profile in the title bar: running totals */
            char title[160];
            snprintf(title, sizeof title, "adamPC - busy avg %.0f%% max %.0f%%, overruns %llu, VRAM hard %llu risky %llu, sprite lines lost %llu",
                     a->prof.busy_sum / (double)a->prof.intervals, a->prof.busy_max, (unsigned long long)a->prof.overruns,
                     (unsigned long long)a->prof.t_hard, (unsigned long long)a->prof.t_risky, (unsigned long long)a->prof.t_5s_lines);
            SDL_SetWindowTitle(win, title);
        }
        adam_run_frame(a);

        if (turbo && ddp_motor_on(&a->ddp)) {
            /* tape turbo: run ahead while a tape motor runs, no sound, no pacing */
            uint64_t t0 = SDL_GetPerformanceCounter();
            while (ddp_motor_on(&a->ddp) && SDL_GetPerformanceCounter() - t0 < perf * 14 / 1000) adam_run_frame(a);
            if (dev) SDL_ClearQueuedAudio(dev);
            next = SDL_GetPerformanceCounter();
        } else if (dev) {
            SDL_QueueAudio(dev, a->audio, (Uint32)(a->audio_n * 2));
            /* audio-paced: keep about 3 frames queued */
            while (SDL_GetQueuedAudioSize(dev) > (Uint32)(AUDIO_RATE * 2 * 3 / 60)) SDL_Delay(1);
        } else {
            next += (uint64_t)(frame_s * (double)perf);
            uint64_t now = SDL_GetPerformanceCounter();
            if (next > now) SDL_Delay((Uint32)((next - now) * 1000 / perf));
        }

        for (int i = 0; i < VDP_W * VDP_H; i++) pix[i] = 0xFF000000u | tms9918_palette[a->vdp.fb[i] & 15];
        SDL_UpdateTexture(tex, NULL, pix, VDP_W * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }
    if (dev) SDL_CloseAudioDevice(dev);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
#endif
