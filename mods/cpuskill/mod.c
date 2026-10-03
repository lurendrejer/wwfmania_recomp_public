/*
 * cpuskill - CPU difficulty (docs/MODS.md, mods/cpuskill/README.md). mods/cpuskill/gen.txt adds the code and
 * the variable `cpu_skill_add`; this mod is the switch for it (the game clears its variables while it
 * starts, so the word is written every frame).
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"

extern const wwf_mod mod_cpuskill;

static int cpuskill_init(wolf *w, void **state, char *err, size_t err_len)
{
    uint32_t *addr = malloc(sizeof *addr);
    if (!addr) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    *addr = wolf_symbol_addr(w, "cpu_skill_add");
    if (!*addr) {
        free(addr);
        snprintf(err, err_len, "not in this build (regenerate with the mod in place)");
        return 0;
    }
    *state = addr;
    return 1;
}

static void cpuskill_frame_begin(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, (uint32_t)(2 * mods_arg(w, &mod_cpuskill)) & 0xFFFFu);
}

static void cpuskill_shutdown(wolf *w, void *state)
{
    gsp_write(&w->cpu, *(uint32_t *)state, 16, 0);
    free(state);
}

const wwf_mod mod_cpuskill = {
    "cpuskill",
    "CPU opponents easier (negative) or harder (positive), each step is one difficulty level (experimental)",
    cpuskill_init,
    cpuskill_frame_begin,
    NULL,
    cpuskill_shutdown,
    -5, 5, 3, "CPU SKILL", 1,
};
