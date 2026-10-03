/*
 * dink - Dink, Doink the Clown's little sidekick: Doink drawn smaller (docs/MODS.md,
 * mods/dink/README.md).
 *
 * He is wrestler 6, Doink, in every table: his moves, his frames, his voice. The mod
 * fills square B of the select screen's extra squares (src/wolf/asm/XSQUARE.ASM) with
 * him, marked small; the select screen then marks the player who takes him
 * (xsq_smallp), and mods/dink/gen.txt has the game draw that player's wrestler
 * smaller and show his own name. `dink_enabled` is the switch for those edits.
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"

#define SQUARE 1   /* square B */

typedef struct {
    uint32_t enabled, on, wnum, small, crut, mug, name;   /* bit addresses of the variables */
    uint32_t crut_img, mug_list, name_img;                /* what goes into them */
} dink_state;

static void fill(wolf *w, const dink_state *s)
{
    gsp_write(&w->cpu, s->enabled, 16, 1);
    gsp_write(&w->cpu, s->on, 16, gsp_read(&w->cpu, s->on, 16) | 1u << SQUARE);
    gsp_write(&w->cpu, s->wnum + 16 * SQUARE, 16, 6);
    gsp_write(&w->cpu, s->small + 16 * SQUARE, 16, 1);
    gsp_write(&w->cpu, s->crut + 32 * SQUARE, 32, s->crut_img);
    gsp_write(&w->cpu, s->mug + 32 * SQUARE, 32, s->mug_list);
    gsp_write(&w->cpu, s->name + 32 * SQUARE, 32, s->name_img);
}

static int dink_init(wolf *w, void **state, char *err, size_t err_len)
{
    dink_state *s = malloc(sizeof *s);
    if (!s) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    s->enabled = wolf_symbol_addr(w, "dink_enabled");
    s->on = wolf_symbol_addr(w, "xsq_on");
    s->wnum = wolf_symbol_addr(w, "xsq_wnum");
    s->small = wolf_symbol_addr(w, "xsq_small");
    s->crut = wolf_symbol_addr(w, "xsq_crut");
    s->mug = wolf_symbol_addr(w, "xsq_mug");
    s->name = wolf_symbol_addr(w, "xsq_name");
    s->crut_img = wolf_symbol_addr(w, "CRUT_DI");
    s->mug_list = wolf_symbol_addr(w, "dink_mug");
    s->name_img = wolf_symbol_addr(w, "NAM_DINK");
    if (!s->enabled || !s->on || !s->wnum || !s->small || !s->crut || !s->mug || !s->name || !s->crut_img ||
        !s->mug_list || !s->name_img) {
        free(s);
        snprintf(err, err_len, "not in this build (regenerate with the mod in place)");
        return 0;
    }
    fill(w, s);
    *state = s;
    return 1;
}

/* The game clears its variables while it starts, so they are set every frame. */
static void dink_frame_begin(wolf *w, void *state)
{
    fill(w, state);
}

static void dink_shutdown(wolf *w, void *state)
{
    const dink_state *s = state;
    gsp_write(&w->cpu, s->enabled, 16, 0);
    gsp_write(&w->cpu, s->on, 16, gsp_read(&w->cpu, s->on, 16) & ~(1u << SQUARE));
    free(state);
}

const wwf_mod mod_dink = {
    "dink",
    "Dink, Doink's little sidekick: Doink drawn smaller",
    dink_init,
    dink_frame_begin,
    NULL,
    dink_shutdown,
    0, 0, 0, NULL, 0,
};
