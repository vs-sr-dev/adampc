/* typer.h - input automation: types text on the emulated ADAM keyboard, plays joystick scripts. */
#ifndef TYPER_H
#define TYPER_H
#include "adam.h"

typedef struct {
    const char *text;
    int pos, phase, wait;
    int raw;            /* 1 = plain text file: no ~XX / | escapes, CR skipped, pause after each line */
    int line_wait;      /* raw mode: frames to wait after RETURN (the program stores the line) */
    const char *then;   /* raw text typed after text (e.g. --type "~03|NEW~0D" then a listing) */
    const char *after;  /* escaped text typed after then (answers to the program's prompts) */
} Typer;

void typer_step(Adam *a, Typer *t);   /* call once per frame */
int  typer_busy(const Typer *t);

/* joystick script (--joy FILE): "FRAME tokens..." per line, the state holds until the next
 * line. Tokens: up down left right fire1 (left button) fire2 (right button) k0-k9 k* k#,
 * "-" = nothing pressed; "p2" before the tokens = controller 2; # starts a comment. */
typedef struct { int frame, player; uint8_t joy; int8_t keypad; } JoyEvent;
typedef struct { JoyEvent *ev; int n, next; } JoyScript;

int  joy_script_load(JoyScript *s, const char *path);
void joy_script_step(Adam *a, JoyScript *s, int frame);   /* call once per frame */

#endif
