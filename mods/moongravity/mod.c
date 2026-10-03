/*
 * moongravity - less (or more) gravity (docs/MODS.md, mods/moongravity/README.md). The game gives a
 * wrestler GRAVITY (8000h, 16.16: half a pixel per tick per tick) on every new animation;
 * mods/moongravity/gen.txt gives `mg_gravity` instead when it is not 0, and this mod sets it to
 * N tenths of the game's.
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"

extern const wwf_mod mod_moongravity;

#define MG_GRAVITY 0x8000u /* GAME.EQU GRAVITY */

static int mg_init(wolf *w, void **state, char *err, size_t err_len)
{
    uint32_t *addr = malloc(sizeof *addr);
    if (!addr) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    *addr = wolf_symbol_addr(w, "mg_gravity");
    if (!*addr) {
        free(addr);
        snprintf(err, err_len, "not in this build (regenerate with the mod in place)");
        return 0;
    }
    *state = addr;
    return 1;
}

static void mg_frame_begin(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 32, MG_GRAVITY * (uint32_t)mods_arg(w, &mod_moongravity) / 10u);
}

static void mg_shutdown(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 32, 0);
    free(state);
}

const wwf_mod mod_moongravity = {
    "moongravity",
    "gravity in tenths of the original: 5 = half (twice the hang time), 10 = the original, 20 = double",
    mg_init,
    mg_frame_begin,
    NULL,
    mg_shutdown,
    2, 20, 5, "GRAVITY X10%", 1,
};
