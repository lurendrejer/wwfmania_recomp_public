/*
 * morebuddies - two buddies for each player in buddy mode (docs/MODS.md,
 * mods/morebuddies/README.md). mods/morebuddies/gen.txt makes WRESTLE.ASM create the extra
 * ones; this mod sets `more_buddies`, how many buddies each side has.
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"



extern const wwf_mod mod_morebuddies;

static int mb_init(wolf *w, void **state, char *err, size_t err_len)
{
    uint32_t *addr = malloc(sizeof *addr);
    if (!addr) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    *addr = wolf_symbol_addr(w, "more_buddies");
    if (!*addr) {
        free(addr);
        snprintf(err, err_len, "not in this build (regenerate with the mod in place)");
        return 0;
    }
    gsp_write(&w->cpu, *addr, 16, (uint32_t)mods_arg(w, &mod_morebuddies));
    *state = addr;
    return 1;
}

/* The game clears its variables while it starts, so the word is set every frame. */
static void mb_frame_begin(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, (uint32_t)mods_arg(w, &mod_morebuddies));
}

static void mb_shutdown(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, 0);
    free(state);
}

const wwf_mod mod_morebuddies = {
    "morebuddies",
    "a second buddy for each player in buddy mode (experimental)",
    mb_init,
    mb_frame_begin,
    NULL,
    mb_shutdown,
    1, 2, 2, "BUDDIES", 0,
};
