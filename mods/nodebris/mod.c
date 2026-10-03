/*
 * nodebris - no blood and no debris (docs/MODS.md, mods/nodebris/README.md). The game has the switch
 * itself: `no_debris` (WRESTLE.ASM), which stops the ring dust, the debris of the finishers and the blood of
 * the broken arm (BROKEN_ARM_BLOOD). Some moves clear it around their own effects, so it is held every frame.
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"

static int nd_init(wolf *w, void **state, char *err, size_t err_len)
{
    uint32_t *addr = malloc(sizeof *addr);
    if (!addr) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    *addr = wolf_symbol_addr(w, "no_debris");
    if (!*addr) {
        free(addr);
        snprintf(err, err_len, "no_debris not found");
        return 0;
    }
    *state = addr;
    return 1;
}

static void nd_frame_begin(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, 1);
}

static void nd_shutdown(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, 0);
    free(state);
}

const wwf_mod mod_nodebris = {
    "nodebris",
    "no blood and no debris",
    nd_init,
    nd_frame_begin,
    NULL,
    nd_shutdown,
    0, 0, 0, NULL, 1,
};
