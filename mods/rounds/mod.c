/*
 * rounds - wins needed to win a match (docs/MODS.md, mods/rounds/README.md). mods/rounds/gen.txt adds the code and
 * the variable `rounds_needed`; this mod is the switch for it (the game clears its variables while it
 * starts, so the word is written every frame).
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"

extern const wwf_mod mod_rounds;

static int rounds_init(wolf *w, void **state, char *err, size_t err_len)
{
    uint32_t *addr = malloc(sizeof *addr);
    if (!addr) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    *addr = wolf_symbol_addr(w, "rounds_needed");
    if (!*addr) {
        free(addr);
        snprintf(err, err_len, "not in this build (regenerate with the mod in place)");
        return 0;
    }
    *state = addr;
    return 1;
}

static void rounds_frame_begin(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, (uint32_t)(mods_arg(w, &mod_rounds)) & 0xFFFFu);
}

static void rounds_shutdown(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, 0);
    free(state);
}

const wwf_mod mod_rounds = {
    "rounds",
    "wins needed for a match: 1 = one round, 2 = best of 3 (original), 3 = best of 5 (experimental)",
    rounds_init,
    rounds_frame_begin,
    NULL,
    rounds_shutdown,
    1, 3, 3, "WINS NEEDED", 0,
};
