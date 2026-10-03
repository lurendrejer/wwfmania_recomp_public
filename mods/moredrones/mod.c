/*
 * moredrones - more computer opponents in a one-player match (docs/MODS.md,
 * mods/moredrones/README.md). mods/moredrones/gen.txt makes WRESTLE.ASM create them;
 * this mod sets `more_drones`, how many extra ones there are.
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"



extern const wwf_mod mod_moredrones;

static int more_init(wolf *w, void **state, char *err, size_t err_len)
{
    uint32_t *addr = malloc(sizeof *addr);
    if (!addr) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    *addr = wolf_symbol_addr(w, "more_drones");
    if (!*addr) {
        free(addr);
        snprintf(err, err_len, "not in this build (regenerate with the mod in place)");
        return 0;
    }
    gsp_write(&w->cpu, *addr, 16, (uint32_t)mods_arg(w, &mod_moredrones));
    *state = addr;
    return 1;
}

/* The game clears its variables while it starts, so the word is set every frame. */
static void more_frame_begin(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, (uint32_t)mods_arg(w, &mod_moredrones));
}

static void more_shutdown(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, 0);
    free(state);
}

const wwf_mod mod_moredrones = {
    "moredrones",
    "several computer opponents at once in a one-player or cooperative match (experimental)",
    more_init,
    more_frame_begin,
    NULL,
    more_shutdown,
    1, 4, 3, "OPPONENTS", 0,
};
