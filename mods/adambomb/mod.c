/*
 * adambomb - Adam Bomb, cut from the shipped game for Lex Luger (docs/MODS.md,
 * mods/adambomb/README.md).
 *
 * mods/adambomb/gen.txt has the generator make him (gen/mkadam.py: Razor Ramon's
 * code with Adam's own frames where he has them, Razor's recoloured where he has
 * not) and fill wrestler number 7 in every table by wrestler. Nothing in the
 * original can choose number 7: the select screen has eight squares. This mod
 * fills square A of the screen's extra squares with him (src/wolf/asm/XSQUARE.ASM)
 * and tells the machine to leave a wrestler number 7 alone (it turns one into
 * Doink's 6: a crash fix for buddy mode, wolf.c).
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"

/* Square A of the select screen's extra squares (src/wolf/asm/XSQUARE.ASM). */
#define SQUARE 0

typedef struct {
    uint32_t on, wnum, small, crut, mug, name;   /* bit addresses of the xsq_ variables */
    uint32_t crut_img, mug_list, name_img;       /* what goes into them */
    uint32_t logo, logo_adm, logo_orig;          /* LOGO_IMAGE_TABLE[7] (PROGRESS.ASM): Lex's there, Adam's with the mod */
} adam_state;

static void fill(wolf *w, const adam_state *s)
{
    gsp_write(&w->cpu, s->on, 16, gsp_read(&w->cpu, s->on, 16) | 1u << SQUARE);
    gsp_write(&w->cpu, s->wnum + 16 * SQUARE, 16, 7);
    gsp_write(&w->cpu, s->small + 16 * SQUARE, 16, 0);
    gsp_write(&w->cpu, s->crut + 32 * SQUARE, 32, s->crut_img);
    gsp_write(&w->cpu, s->mug + 32 * SQUARE, 32, s->mug_list);
    gsp_write(&w->cpu, s->name + 32 * SQUARE, 32, s->name_img);
    gsp_write(&w->cpu, s->logo + 32 * 7, 32, s->logo_adm);
}

static int adam_init(wolf *w, void **state, char *err, size_t err_len)
{
    adam_state *s = malloc(sizeof *s);
    if (!s) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    s->on = wolf_symbol_addr(w, "xsq_on");
    s->wnum = wolf_symbol_addr(w, "xsq_wnum");
    s->small = wolf_symbol_addr(w, "xsq_small");
    s->crut = wolf_symbol_addr(w, "xsq_crut");
    s->mug = wolf_symbol_addr(w, "xsq_mug");
    s->name = wolf_symbol_addr(w, "xsq_name");
    s->crut_img = wolf_symbol_addr(w, "CRUT_AB");
    s->mug_list = wolf_symbol_addr(w, "adam_mug");
    s->name_img = wolf_symbol_addr(w, "NAM_ADM");
    s->logo = wolf_symbol_addr(w, "LOGO_IMAGE_TABLE");
    s->logo_adm = wolf_symbol_addr(w, "ADM");
    s->logo_orig = s->logo ? gsp_read(&w->cpu, s->logo + 32 * 7, 32) : 0;
    if (!s->on || !s->wnum || !s->small || !s->crut || !s->mug || !s->name || !s->crut_img || !s->mug_list ||
        !s->name_img || !s->logo || !s->logo_adm) {
        free(s);
        snprintf(err, err_len, "not in this build (regenerate with the mod in place)");
        return 0;
    }
    fill(w, s);
    w->wrestler7 = 1;   /* the machine turns a wrestler number 7 into 6 (Doink) otherwise */
    *state = s;
    return 1;
}

/* The game clears its variables while it starts, so the square is filled every frame. */
static void adam_frame_begin(wolf *w, void *state)
{
    fill(w, state);
}

static void adam_shutdown(wolf *w, void *state)
{
    const adam_state *s = state;
    gsp_write(&w->cpu, s->on, 16, gsp_read(&w->cpu, s->on, 16) & ~(1u << SQUARE));
    gsp_write(&w->cpu, s->logo + 32 * 7, 32, s->logo_orig);
    w->wrestler7 = 0;
    free(state);
}
const wwf_mod mod_adambomb = {
    "adambomb",
    "Adam Bomb, the wrestler Lex Luger replaced",
    adam_init,
    adam_frame_begin,
    NULL,
    adam_shutdown,
    0, 0, 0, NULL, 0,
};
