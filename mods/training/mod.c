/*
 * training - practice mode (docs/MODS.md, mods/training/README.md). No game code is changed: every
 * frame the mod reads and writes the game's variables, then draws the collision boxes on the
 * finished display page.
 *
 *  - the match clock is held where it is
 *  - number 1: Start 1 refills life and turbo of everybody
 *    number 2: human wrestlers never lose life or turbo
 *    number 3: nobody does
 *  - boxes: the body box of every wrestler (green) and the attack box while a move can hit (red)
 *
 * The offsets are bit offsets into the wrestler process (orig/PLYR.EQU); they are checked against
 * a running game (see the README).
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"

#define RAM_LO 0x01000000u
#define NUM_SLOTS 7

enum {
    O_XPOSINT = 0x110, O_YPOSINT = 0x130, O_ZPOSINT = 0x150,
    O_COLLX1 = 0x1E0, O_COLLX2 = 0x1F0, O_COLLY1 = 0x200, O_COLLY2 = 0x210, O_COLLZ1 = 0x220, O_COLLZ2 = 0x230,
    O_ATTXOFF = 0x240, O_ATTYOFF = 0x250, O_ATTZOFF = 0x260, O_ATTWIDTH = 0x270, O_ATTHEIGHT = 0x280,
    O_ATTDEPTH = 0x290,
    O_PLYR_TYPE = 0x5A0, O_ANIMODE = 0x620, O_CONTROL = 0x660,
};
#define MODE_CHECKHIT 0x10
#define B_FLIPH 4
#define PLT_LIFE 0x00
#define PLT_CLIFE 0x10
#define PLT_TURBO 0x20
#define PLT_SIZE 0x40
#define LIFE_MAX 163
#define TURBO_MAX 0x5400
#define PAL 127
#define Z_SCALE 0x3566   /* world z to screen rows, 16 bit fraction (WRESTLE2.ASM) */
#define FEET_ADJ -14      /* rows between the projected feet and the ground the game draws on */

typedef struct {
    uint32_t life_data, match_time;
    int last_start;
    int held;                /* the clock's value, seconds (0 = not seen yet) */
} tr_state;

static int rd16s(wolf *w, uint32_t addr)
{
    return (int16_t)gsp_read(&w->cpu, addr, 16);
}

static int tr_init(wolf *w, void **state, char *err, size_t err_len)
{
    tr_state *s = calloc(1, sizeof *s);
    if (!s) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    s->life_data = wolf_symbol_addr(w, "life_data");
    s->match_time = wolf_symbol_addr(w, "match_time");
    if (!s->life_data || !s->match_time || !w->procptrs_addr || !w->worldtlx_addr || !w->worldtly_addr) {
        free(s);
        snprintf(err, err_len, "not in this build (symbols missing)");
        return 0;
    }
    *state = s;
    return 1;
}

static uint32_t slot_proc(wolf *w, int i)
{
    uint32_t p = gsp_read(&w->cpu, w->procptrs_addr + 32u * (unsigned)i, 32);
    return p >= RAM_LO && p < RAM_LO + 0x400000u ? p : 0;
}

static void refill(wolf *w, tr_state *s, int i)
{
    uint32_t d = s->life_data + (uint32_t)i * PLT_SIZE;
    gsp_write(&w->cpu, d + PLT_LIFE, 16, LIFE_MAX);
    gsp_write(&w->cpu, d + PLT_CLIFE, 16, LIFE_MAX);
    gsp_write(&w->cpu, d + PLT_TURBO, 16, TURBO_MAX);
}

static void tr_frame_begin(wolf *w, void *state)
{
    tr_state *s = state;
    int arg = mods_arg(w, mods_find("training"));
    int start = (w->coin_bits & WOLF_START1) != 0;
    int press = start && !s->last_start;

    s->last_start = start;
    /* the clock (match_time: tens, ones, fraction): the digits stay where they were; a new round starts
     * with 99, which becomes the held value */
    {
        int tens = (int)gsp_read(&w->cpu, s->match_time, 16), ones = (int)gsp_read(&w->cpu, s->match_time + 16, 16);

        if (tens == 9 && ones == 9 && gsp_read(&w->cpu, s->match_time + 32, 16) == 0)
            s->held = 99;
        else if (s->held == 0 && (tens || ones))
            s->held = tens * 10 + ones;
        if (s->held) {
            gsp_write(&w->cpu, s->match_time, 16, (uint32_t)(s->held / 10));
            gsp_write(&w->cpu, s->match_time + 16, 16, (uint32_t)(s->held % 10));
        }
    }
    for (int i = 0; i < NUM_SLOTS; i++) {
        uint32_t p = slot_proc(w, i);
        int type;

        if (!p)
            continue;
        type = rd16s(w, p + O_PLYR_TYPE);
        if (type != 0 && type != 1)
            continue; /* the referee */
        if (arg == 1 ? press : arg == 2 ? type == 0 : 1)
            refill(w, s, i);
    }
}

static void plot(wolf *w, int sx, int sy, uint16_t v)
{
    video_put_pixel(&w->v, w->v.view_x + sx, w->v.view_y + sy, v);
}

static void rect(wolf *w, int x1, int y1, int x2, int y2, uint16_t v)
{
    for (int x = x1; x <= x2; x++) {
        plot(w, x, y1, v);
        plot(w, x, y2, v);
    }
    for (int y = y1; y <= y2; y++) {
        plot(w, x1, y, v);
        plot(w, x2, y, v);
    }
}

/* A world box (x1..x2 across, y1..y2 up, z1..z2 in depth) as its front face at the middle depth,
 * plus the ground footprint's near edge. */
static void box(wolf *w, int tlx, int tly, int x1, int x2, int y1, int y2, int z1, int z2, uint16_t v)
{
    int zm = (z1 + z2) / 2;
    int base = ((zm * Z_SCALE) >> 16) - tly + FEET_ADJ;

    rect(w, x1 - tlx, base - y2, x2 - tlx, base - y1, v);
}

static void tr_frame_end(wolf *w, void *state)
{
    static const uint16_t pal[3] = {0x0000, 0x03E0, 0x7C00}; /* unused, green, red */
    int tlx = rd16s(w, w->worldtlx_addr + 16);
    int tly = rd16s(w, w->worldtly_addr + 16);

    (void)state;
    video_set_palette(&w->v, PAL, pal, 3);
    for (int i = 0; i < NUM_SLOTS; i++) {
        uint32_t p = slot_proc(w, i);
        int type, mode, ctl, x, y, z;

        if (!p)
            continue;
        type = rd16s(w, p + O_PLYR_TYPE);
        if (type != 0 && type != 1)
            continue;
        box(w, tlx, tly, rd16s(w, p + O_COLLX1), rd16s(w, p + O_COLLX2), rd16s(w, p + O_COLLY1),
            rd16s(w, p + O_COLLY2), rd16s(w, p + O_COLLZ1), rd16s(w, p + O_COLLZ2), (uint16_t)(PAL << 8 | 1));
        mode = rd16s(w, p + O_ANIMODE);
        if (mode & MODE_CHECKHIT) {
            int w_ = rd16s(w, p + O_ATTWIDTH), xo = rd16s(w, p + O_ATTXOFF), x1, x2;
            int y1, z1;

            ctl = rd16s(w, p + O_CONTROL);
            x = rd16s(w, p + O_XPOSINT);
            y = rd16s(w, p + O_YPOSINT);
            z = rd16s(w, p + O_ZPOSINT);
            if (ctl & (1 << B_FLIPH)) {
                x2 = x - xo;
                x1 = x2 - w_;
            } else {
                x1 = x + xo;
                x2 = x1 + w_;
            }
            y1 = y + rd16s(w, p + O_ATTYOFF);
            z1 = z + rd16s(w, p + O_ATTZOFF);
            box(w, tlx, tly, x1, x2, y1, y1 + rd16s(w, p + O_ATTHEIGHT), z1, z1 + rd16s(w, p + O_ATTDEPTH),
                (uint16_t)(PAL << 8 | 2));
        }
    }
}

static void tr_shutdown(wolf *w, void *state)
{
    (void)w;
    free(state);
}

const wwf_mod mod_training = {
    "training",
    "practice mode: held clock, refills or infinite life/turbo, collision boxes",
    tr_init,
    tr_frame_begin,
    tr_frame_end,
    tr_shutdown,
    1, 3, 2, "REFILL", 1,
};
