/*
 * outsidedive - dives to the outside (docs/MODS.md, mods/outsidedive/README.md). A running attack started in the
 * ring at a target outside it goes over the ropes and comes down on him: the attacks on a standing opponent and on a
 * lying one, every wrestler's. mods/outsidedive/gen.txt and asm/OUTSIDEDIVE.ASM do it; this mod is the switch for
 * `dive_on`.
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"

static int dive_init(wolf *w, void **state, char *err, size_t err_len)
{
    uint32_t *addr = malloc(sizeof *addr);
    if (!addr) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    *addr = wolf_symbol_addr(w, "dive_on");
    if (!*addr) {
        free(addr);
        snprintf(err, err_len, "not in this build (regenerate with the mod in place)");
        return 0;
    }
    gsp_write(&w->cpu, *addr, 16, 1);
    *state = addr;
    return 1;
}

/* The game clears its variables while it starts, so the word is set every frame. */
static void dive_frame_begin(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, 1);
}

static void dive_shutdown(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, 0);
    free(state);
}

const wwf_mod mod_outsidedive = {
    "outsidedive",
    "running attacks at an opponent outside the ring go over the ropes and land on him (experimental)",
    dive_init,
    dive_frame_begin,
    NULL,
    dive_shutdown,
    0, 0, 0, NULL, 1,
};
