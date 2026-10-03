/*
 * sansring - no ropes: the ring is left as soon as a wrestler pushes against the edge (docs/MODS.md,
 * mods/sansring/README.md). mods/sansring/gen.txt makes the climb-out immediate and possible without an opponent
 * outside (`sans_on`); the ropes are hidden by the machine (wolf.hide_ropes); here RING_TIME (the count of ticks
 * outside the ring that finally hurts) is held at 1 for every wrestler, as the option "no ring-out timer" does.
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"

#define NUM_WRESTLERS 6                 /* the wrestler processes 0 to 5; the last of 7 is the referee's */
#define RING_TIME_BIT_OFFSET 0xC40      /* PLYR.EQU RING_TIME in the wrestler process */

typedef struct {
    uint32_t on, ptrs;
} sr_addr;

static int sr_init(wolf *w, void **state, char *err, size_t err_len)
{
    sr_addr *a = malloc(sizeof *a);
    if (!a) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    a->on = wolf_symbol_addr(w, "sans_on");
    a->ptrs = wolf_symbol_addr(w, "process_ptrs");
    if (!a->on || !a->ptrs) {
        free(a);
        snprintf(err, err_len, "not in this build (regenerate with the mod in place)");
        return 0;
    }
    *state = a;
    return 1;
}

/* The game clears its variables while it starts, so everything is written every frame. */
static void sr_frame_begin(wolf *w, void *state)
{
    const sr_addr *a = state;
    gsp_write(&w->cpu, a->on, 16, 1);
    w->hide_ropes = 1;
    for (int i = 0; i < NUM_WRESTLERS; i++) {
        uint32_t p = gsp_read(&w->cpu, a->ptrs + 32u * (unsigned)i, 32);
        if (p)
            gsp_write(&w->cpu, p + RING_TIME_BIT_OFFSET, 16, 1);
    }
}

static void sr_shutdown(wolf *w, void *state)
{
    const sr_addr *a = state;
    gsp_write(&w->cpu, a->on, 16, 0);
    w->hide_ropes = 0;
    free(state);
}

const wwf_mod mod_sansring = {
    "sansring",
    "no ropes: leave the ring at once by walking against the edge (experimental)",
    sr_init,
    sr_frame_begin,
    NULL,
    sr_shutdown,
    0, 0, 0, NULL, 1,
};
