/*
 * matchtime - the length of a round (docs/MODS.md, mods/matchtime/README.md). The game starts every
 * round with the clock at 99 (`match_time`: tens, ones, fraction, 16 bits each; WRESTLE.ASM). When the
 * mod sees that start value it writes its own; the clock then counts down from there as usual.
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"

extern const wwf_mod mod_matchtime;

static int mt_init(wolf *w, void **state, char *err, size_t err_len)
{
    uint32_t *addr = malloc(sizeof *addr);
    if (!addr) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    *addr = wolf_symbol_addr(w, "match_time");
    if (!*addr) {
        free(addr);
        snprintf(err, err_len, "match_time not found");
        return 0;
    }
    *state = addr;
    return 1;
}

static void mt_frame_begin(wolf *w, void *state)
{
    uint32_t a = *(uint32_t *)state;
    int secs = mods_arg(w, &mod_matchtime);

    if (getenv("WWF_DEBUG_TIME") && w->frames % 30 == 0)
        fprintf(stderr, "frame %llu time %u %u %u\n", (unsigned long long)w->frames, (unsigned)gsp_read(&w->cpu, a, 16),
                (unsigned)gsp_read(&w->cpu, a + 16, 16), (unsigned)gsp_read(&w->cpu, a + 32, 16));
    if (gsp_read(&w->cpu, a, 16) == 9 && gsp_read(&w->cpu, a + 16, 16) == 9 && gsp_read(&w->cpu, a + 32, 16) == 0) {
        gsp_write(&w->cpu, a, 16, (uint32_t)(secs / 10));
        gsp_write(&w->cpu, a + 16, 16, (uint32_t)(secs % 10));
    }
}

const wwf_mod mod_matchtime = {
    "matchtime",
    "the length of a round in seconds (10 to 99)",
    mt_init,
    mt_frame_begin,
    NULL,
    NULL,
    10, 99, 60, "SECONDS", 1,
};
