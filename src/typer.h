/* typer.h - types text on the emulated ADAM keyboard, one key per few frames. */
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

#endif
