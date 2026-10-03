/*
 * chair - the cut folding chair (docs/MODS.md, mods/chair/README.md). mods/chair/gen.txt adds the move:
 * Down, Down, Block, at the left or right side of the ring, picks it up and any button swings it; with the mod's number
 * at 1 he keeps it after a swing and can swing it again.
 * This mod is its switch; the game clears its variables while it starts, so the words are written every frame.
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"

extern const wwf_mod mod_chair;

typedef struct {
    uint32_t enabled, keep;
} chair_state;

static int chair_init(wolf *w, void **state, char *err, size_t err_len)
{
    chair_state *s = malloc(sizeof *s);
    if (!s) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    s->enabled = wolf_symbol_addr(w, "chair_enabled");
    s->keep = wolf_symbol_addr(w, "chair_keep");
    if (!s->enabled || !s->keep) {
        free(s);
        snprintf(err, err_len, "not in this build (regenerate with the mod in place)");
        return 0;
    }
    *state = s;
    return 1;
}

static void chair_frame_begin(wolf *w, void *state)
{
    chair_state *s = state;

    gsp_write(&w->cpu, s->enabled, 16, 1);
    gsp_write(&w->cpu, s->keep, 16, (uint32_t)mods_arg(w, &mod_chair));
}

static void chair_shutdown(wolf *w, void *state)
{
    chair_state *s = state;

    gsp_write(&w->cpu, s->enabled, 16, 0);
    gsp_write(&w->cpu, s->keep, 16, 0);
    free(s);
}

const wwf_mod mod_chair = {
    "chair",
    "the cut folding chair: Down, Down, Block at the side of the ring picks it up, any button swings it (1 = keep it, swing again)",
    chair_init,
    chair_frame_begin,
    NULL,
    chair_shutdown,
    0, 1, 0, "KEEP", 0,
};
