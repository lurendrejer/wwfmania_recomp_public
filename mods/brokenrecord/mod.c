/*
 * brokenrecord - the commentators only ever say one line (docs/MODS.md, mods/brokenrecord/README.md).
 * mods/brokenrecord/gen.txt swaps every commentator line for `brec_line` as it is said; this mod switches
 * that on and, when its number is not 0, picks the line. With 0 the first line said after the game
 * starts is the one (the game clears the variable when it starts).
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"

extern const wwf_mod mod_brokenrecord;

typedef struct {
    uint32_t on, line;
} brec_state;

/* The mod's number 1..: triple_sndtab indices (SOUND.EQU) */
static const uint16_t brec_lines[] = {
    0x18F, /* HELLO */
    0x0F1, /* OH_MY */
    0x18E, /* GOODNIGHT */
    0x184, /* TO_THE_FACE */
    0x18B, /* WHAT_A_BLOW */
};

static int brec_init(wolf *w, void **state, char *err, size_t err_len)
{
    brec_state *s = malloc(sizeof *s);
    if (!s) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    s->on = wolf_symbol_addr(w, "brec_on");
    s->line = wolf_symbol_addr(w, "brec_line");
    if (!s->on || !s->line) {
        free(s);
        snprintf(err, err_len, "not in this build (regenerate with the mod in place)");
        return 0;
    }
    *state = s;
    return 1;
}

static void brec_frame_begin(wolf *w, void *state)
{
    brec_state *s = state;
    int n = mods_arg(w, &mod_brokenrecord);

    gsp_write(&w->cpu, s->on, 16, 1);
    if (n > 0)
        gsp_write(&w->cpu, s->line, 16, brec_lines[n - 1]);
}

static void brec_shutdown(wolf *w, void *state)
{
    brec_state *s = state;

    gsp_write(&w->cpu, s->on, 16, 0);
    gsp_write(&w->cpu, s->line, 16, 0);
    free(s);
}

const wwf_mod mod_brokenrecord = {
    "brokenrecord",
    "the commentators only ever say one line: 0 = the first one they say, 1-5 = hello, oh my, goodnight, "
    "to the face, what a blow",
    brec_init,
    brec_frame_begin,
    NULL,
    brec_shutdown,
    0, 5, 0, "LINE", 1,
};
