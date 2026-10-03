/*
 * bamfire - Bam Bam's unused fireball throw (docs/MODS.md, mods/bamfire/README.md).
 *
 * IMG/BAMSPEC.IMG holds a bowling-style throw (BAMBOWL) that no animation script
 * uses, and SPECIAL.ASM has the fireball it was meant to launch (bam_fireball),
 * which nothing starts. mods/bamfire/gen.txt adds the images, an animation and a
 * secret move (Down, Toward, Punch); this mod is the switch for `bowl_enabled`
 * that the move checks.
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"

static int bowl_init(wolf *w, void **state, char *err, size_t err_len)
{
    uint32_t *addr = malloc(sizeof *addr);
    if (!addr) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    *addr = wolf_symbol_addr(w, "bowl_enabled");
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
static void bowl_frame_begin(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, 1);
}

static void bowl_shutdown(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, 0);
    free(state);
}

const wwf_mod mod_bamfire = {
    "bamfire",
    "Bam Bam's unused fireball throw: Down, Toward, Punch (experimental)",
    bowl_init,
    bowl_frame_begin,
    NULL,
    bowl_shutdown,
    0, 0, 0, NULL, 1,
};
