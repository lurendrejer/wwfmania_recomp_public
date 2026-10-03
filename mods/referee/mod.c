/*
 * referee - brings the unused referee back (docs/MODS.md, mods/referee/README.md).
 *
 * The original game never shipped him: orig/REF.ASM, REFSEQ1.ASM and the art in
 * IMG/REF.LOD were left out of the build. mods/referee/gen.txt has the generator
 * link them in (inactive) and fill wrestler number 9 in the wrestler tables; a
 * copy of WRESTLE.ASM in memory creates him as a wrestler process (type
 * PTYPE_REFEREE) when the wrestlers are created, but only when `ref_enabled`
 * is not zero. This mod is the switch for that word.
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"

static int ref_init(wolf *w, void **state, char *err, size_t err_len)
{
    uint32_t *addr = malloc(sizeof *addr);
    if (!addr) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    *addr = wolf_symbol_addr(w, "ref_enabled");
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
static void ref_frame_begin(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, 1);
}

static void ref_shutdown(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, 0);
    free(state);
}

const wwf_mod mod_referee = {
    "referee",
    "the unused referee (experimental)",
    ref_init,
    ref_frame_begin,
    NULL,
    ref_shutdown,
    0, 0, 0, NULL, 0,
};
