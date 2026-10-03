/*
 * wrongnames - the announcer calls every wrestler by someone else's name (docs/MODS.md,
 * mods/wrongnames/README.md). mods/wrongnames/gen.txt runs every wrestler number the announcer picks a
 * line by through `wn_map`; this mod fills the map: each wrestler gets the name of the one N places
 * further on in the list below.
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"

extern const wwf_mod mod_wrongnames;

typedef struct {
    uint32_t on, map;
} wn_state;

/* Wrestler numbers with speech (DCSSOUND.ASM WHICH_WRESTLER): Bret, Razor, Taker, Yoko, Shawn, Bam Bam,
 * Doink, Lex. 7 is the spare slot, with no lines. */
static const int wn_order[] = {0, 1, 2, 3, 4, 5, 6, 8};
#define WN_COUNT ((int)(sizeof wn_order / sizeof wn_order[0]))

static int wn_init(wolf *w, void **state, char *err, size_t err_len)
{
    wn_state *s = malloc(sizeof *s);
    if (!s) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    s->on = wolf_symbol_addr(w, "wn_on");
    s->map = wolf_symbol_addr(w, "wn_map");
    if (!s->on || !s->map) {
        free(s);
        snprintf(err, err_len, "not in this build (regenerate with the mod in place)");
        return 0;
    }
    *state = s;
    return 1;
}

static void wn_frame_begin(wolf *w, void *state)
{
    wn_state *s = state;
    int k = mods_arg(w, &mod_wrongnames);

    gsp_write(&w->cpu, s->map + 7 * 16, 16, 7);
    for (int i = 0; i < WN_COUNT; i++)
        gsp_write(&w->cpu, s->map + (uint32_t)wn_order[i] * 16, 16, (uint32_t)wn_order[(i + k) % WN_COUNT]);
    gsp_write(&w->cpu, s->on, 16, 1);
}

static void wn_shutdown(wolf *w, void *state)
{
    wn_state *s = state;

    gsp_write(&w->cpu, s->on, 16, 0);
    free(s);
}

const wwf_mod mod_wrongnames = {
    "wrongnames",
    "the announcer calls every wrestler by someone else's name (the one N places further on the roster)",
    wn_init,
    wn_frame_begin,
    NULL,
    wn_shutdown,
    1, 7, 1, "SHIFT", 1,
};
