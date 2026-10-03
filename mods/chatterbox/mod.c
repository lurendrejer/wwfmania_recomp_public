/*
 * chatterbox - the commentators comment on everything (docs/MODS.md, mods/chatterbox/README.md).
 * mods/chatterbox/gen.txt makes every chance of a comment 100 percent while `chatter_on` is set.
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"

static int chatter_init(wolf *w, void **state, char *err, size_t err_len)
{
    uint32_t *addr = malloc(sizeof *addr);
    if (!addr) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    *addr = wolf_symbol_addr(w, "chatter_on");
    if (!*addr) {
        free(addr);
        snprintf(err, err_len, "not in this build (regenerate with the mod in place)");
        return 0;
    }
    *state = addr;
    return 1;
}

static void chatter_frame_begin(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, 1);
}

static void chatter_shutdown(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, 0);
    free(state);
}

const wwf_mod mod_chatterbox = {
    "chatterbox",
    "the commentators comment on every hit and every move, not one in five",
    chatter_init,
    chatter_frame_begin,
    NULL,
    chatter_shutdown,
    0, 0, 0, NULL, 1,
};
