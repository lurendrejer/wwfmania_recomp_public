/*
 * bottombuckles - climb the near turnbuckles too (docs/MODS.md, mods/bottombuckles/README.md).
 * mods/bottombuckles/gen.txt adds the climb at the bottom edge; this mod switches it on (`bb_on`) every frame,
 * since the game clears its variables while it starts.
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"

extern const wwf_mod mod_bottombuckles;

/* bb_on and fc_only_out, the game's variables this mod sets */
typedef struct {
    uint32_t on, only_out;
} bb_state;

static int bb_init(wolf *w, void **state, char *err, size_t err_len)
{
    bb_state *st = malloc(sizeof *st);
    if (!st) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    st->on = wolf_symbol_addr(w, "bb_on");
    st->only_out = wolf_symbol_addr(w, "fc_only_out");
    if (!st->on || !st->only_out) {
        free(st);
        snprintf(err, err_len, "not in this build (regenerate with the mod in place)");
        return 0;
    }
    *state = st;
    return 1;
}

static void bb_frame_begin(wolf *w, void *state)
{
    const bb_state *st = state;
    gsp_write(&w->cpu, st->on, 16, 1);
    gsp_write(&w->cpu, st->only_out, 16, mods_arg(w, &mod_bottombuckles) == 2);
}

static void bb_shutdown(wolf *w, void *state)
{
    const bb_state *st = state;
    gsp_write(&w->cpu, st->on, 16, 0);
    gsp_write(&w->cpu, st->only_out, 16, 0);
    free(state);
}

const wwf_mod mod_bottombuckles = {
    "bottombuckles",
    "climb the near turnbuckles (Down + Left/Right at the bottom edge) and the crowd fence's corners (Up / Down + Left/Right there) too. Number: 1 = a leap from the fence flies to anyone, 2 = only at those outside the ring, else he hops in place (experimental)",
    bb_init,
    bb_frame_begin,
    NULL,
    bb_shutdown,
    1, 2, 1, "FENCE LEAP", 0,
};
