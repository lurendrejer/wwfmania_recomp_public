/*
 * doinkpie - Doink throws the pie (docs/MODS.md, mods/doinkpie/README.md). The original has the
 * projectile (SPECIAL.ASM doink_pie) and its splat, but nothing throws it. mods/doinkpie/gen.txt adds
 * the move (Down, Down + kick); this mod is the switch for `pie_enabled`.
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"

static int pie_init(wolf *w, void **state, char *err, size_t err_len)
{
    uint32_t *addr = malloc(sizeof *addr);
    if (!addr) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    *addr = wolf_symbol_addr(w, "pie_enabled");
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
static void pie_frame_begin(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, 1);
}

static void pie_shutdown(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, 0);
    free(state);
}

const wwf_mod mod_doinkpie = {
    "doinkpie",
    "Doink throws his unused pie: Down, Down + kick (experimental)",
    pie_init,
    pie_frame_begin,
    NULL,
    pie_shutdown,
    0, 0, 0, NULL, 1,
};
