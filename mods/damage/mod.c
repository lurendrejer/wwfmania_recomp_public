/*
 * damage - a factor on all damage (docs/MODS.md, mods/damage/README.md). The game already scales every hit
 * by `speed_adjustment` (LIFEBAR.ASM, 16.16 fixed point, set from the game's speed adjustment when the
 * match's life data is created). The mod multiplies that value: it notices the game setting a new one (the value
 * is not what the mod wrote last) and writes base * factor from then on.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"

extern const wwf_mod mod_damage;

typedef struct {
    uint32_t addr;
    uint32_t base, written;
    int have;
} dmg_state;

static int dmg_init(wolf *w, void **state, char *err, size_t err_len)
{
    dmg_state *s = calloc(1, sizeof *s);
    if (!s) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    s->addr = wolf_symbol_addr(w, "speed_adjustment");
    if (!s->addr) {
        free(s);
        snprintf(err, err_len, "speed_adjustment not found");
        return 0;
    }
    *state = s;
    return 1;
}

static void dmg_frame_begin(wolf *w, void *state)
{
    dmg_state *s = state;
    uint32_t cur = gsp_read(&w->cpu, s->addr, 32);

    if (!s->have || cur != s->written) {         /* the game set it (or first look): this is the base */
        if (cur == 0)
            return;
        s->base = cur;
        s->have = 1;
    }
    s->written = (uint32_t)((uint64_t)s->base * (uint64_t)(mods_arg(w, &mod_damage) * 10) / 100);
    gsp_write(&w->cpu, s->addr, 32, s->written);
}

static void dmg_shutdown(wolf *w, void *state)
{
    dmg_state *s = state;

    if (s->have && gsp_read(&w->cpu, s->addr, 32) == s->written)
        gsp_write(&w->cpu, s->addr, 32, s->base);
    free(s);
}

const wwf_mod mod_damage = {
    "damage",
    "a factor on all damage, in steps of 10 percent (5 = half, 10 = the original, 30 = triple)",
    dmg_init,
    dmg_frame_begin,
    NULL,
    dmg_shutdown,
    2, 40, 15, "DAMAGE X10%", 1,
};
