/*
 * coopladder - two people play the one-player championship ladder together (docs/MODS.md,
 * mods/coopladder/README.md). mods/coopladder/gen.txt makes the machine start the ladder when
 * COOP is chosen on the question screen; this mod only hands it the mod's number (`coop_arg`).
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"

extern const wwf_mod mod_coopladder;

static int coop_init(wolf *w, void **state, char *err, size_t err_len)
{
    uint32_t *addr = malloc(sizeof *addr);
    if (!addr) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    *addr = wolf_symbol_addr(w, "coop_arg");
    if (!*addr || !wolf_symbol_addr(w, "coop_ladder")) {
        free(addr);
        snprintf(err, err_len, "not in this build (regenerate with the mod in place)");
        return 0;
    }
    gsp_write(&w->cpu, *addr, 16, (uint32_t)mods_arg(w, &mod_coopladder));
    *state = addr;
    return 1;
}

/* The game clears its variables while it starts, so the word is set every frame. */
static void coop_frame_begin(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, (uint32_t)mods_arg(w, &mod_coopladder));
}

static void coop_shutdown(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, 0);
    free(state);
}

const wwf_mod mod_coopladder = {
    "coopladder",
    "two people play the championship ladder together (2 players only): COOP starts it instead of the royal rumble",
    coop_init,
    coop_frame_begin,
    NULL,
    coop_shutdown,
    0, 3, 3, "LADDER", 0,
};
