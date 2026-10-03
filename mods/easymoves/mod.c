/*
 * easymoves - Toward, Toward + button for the special moves (docs/MODS.md,
 * mods/easymoves/README.md). mods/easymoves/gen.txt adds the extra records to the
 * wrestlers' secret move tables; this mod is the switch for `easy_enabled`.
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"

static int easy_init(wolf *w, void **state, char *err, size_t err_len)
{
    uint32_t *addr = malloc(sizeof *addr);
    if (!addr) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    *addr = wolf_symbol_addr(w, "easy_enabled");
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
static void easy_frame_begin(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, 1);
}

static void easy_shutdown(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, 0);
    free(state);
}

const wwf_mod mod_easymoves = {
    "easymoves",
    "every special move as Toward, Toward + button (experimental)",
    easy_init,
    easy_frame_begin,
    NULL,
    easy_shutdown,
    0, 0, 0, NULL, 1,
};
