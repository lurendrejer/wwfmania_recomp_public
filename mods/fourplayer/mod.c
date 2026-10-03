/*
 * fourplayer - four people in the game (docs/MODS.md, mods/fourplayer/README.md).
 *
 * The game has two players of its own (the left and right side) and, in buddy mode, a computer
 * partner for each; mods/fourplayer/gen.txt lets the partners be played through `fp_in`. This mod
 * gives each of the four people one of those places, fixed whoever presses start first:
 *
 *   - players 1 and 2 are the game's two players (left and right side) and play with all of the
 *     game's own rules (credits, select screen, continue, game over);
 *   - player 3 is the partner on player 1's side and player 4 the partner on player 2's, and only
 *     then is buddy mode asked for. A partner joins after a player of the game has started: a
 *     start before that is not kept.
 *
 * Two people (1 and 2) play one against the other, and a third and fourth make it two on
 * two. A person who has not pressed start plays nothing, except that players 1 and 2 keep their
 * own places while nobody else holds them (the attract mode and the menus as in the original).
 */
#include <stdio.h>
#include <stdlib.h>

#include "wolf/wolf.h"

/* The game's places: its players 1 and 2, and the partners (processes 2 and 3, fp_in). */
enum { FP_L1, FP_L2, FP_B1, FP_B2, FP_SLOTS };
/* Frames a place of the game's own players stays held without the game having the player in
 * (PSTATUS): the start goes through the game's start code first, and a player who has lost has
 * the continue count-down to come back. Frames a partner's start is held for the select screen. */
enum { FP_GRACE = 15 * 60 };
/* GAMSTATE (orig/GAME.EQU) of a match being fought. */
enum { FP_INGAME = 4 };

typedef struct {
    uint32_t on, in, wait, pstatus, gamstate, join, req, buddy, has, who, rumble;
    int owner[FP_SLOTS];  /* the person (0-3) who holds each place, -1 none */
    int idle[2];          /* frames the game has been without the holder of L1 / L2 */
    uint16_t prev[4];     /* each person's last word (a press of start is a rising edge) */
} fp_state;

static int fp_init(wolf *w, void **state, char *err, size_t err_len)
{
    fp_state *a = calloc(1, sizeof *a);
    if (!a) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    a->on = wolf_symbol_addr(w, "fp_on");
    a->in = wolf_symbol_addr(w, "fp_in");
    a->wait = wolf_symbol_addr(w, "fp_wait");
    a->pstatus = wolf_symbol_addr(w, "PSTATUS");
    a->gamstate = wolf_symbol_addr(w, "GAMSTATE");
    a->join = wolf_symbol_addr(w, "fp_join");
    a->req = wolf_symbol_addr(w, "fp_req");
    a->buddy = wolf_symbol_addr(w, "buddy_mode_on");
    a->has = wolf_symbol_addr(w, "fp_has");
    a->who = wolf_symbol_addr(w, "fp_who");
    a->rumble = wolf_symbol_addr(w, "royal_rumble");
    if (!a->has || !a->who || !a->rumble || !a->on || !a->in || !a->wait || !a->pstatus || !a->gamstate || !a->join || !a->req || !a->buddy) {
        free(a);
        snprintf(err, err_len, "not in this build (regenerate with the mod in place)");
        return 0;
    }
    for (int s = 0; s < FP_SLOTS; s++)
        a->owner[s] = -1;
    *state = a;
    return 1;
}

/* A person's controls as one word in the fp_in layout (wolf.h WOLF_X_*). Players 1 and 2 come from
 * the game's switches (stick and punch, block, super punch in SWITCH, the kicks in SWITCH2, start in
 * the coin register); 3 and 4 from wolf.extra_player. */
static uint16_t fp_person(const wolf *w, int p)
{
    if (p >= 2)
        return (uint16_t)(w->extra_player[p - 2] & 0x3FFu);
    const unsigned s = w->player[p], k = (unsigned)w->player[2] >> (4 * p);
    unsigned v = s & 0xFu;
    if (s & WOLF_B1) v |= WOLF_X_PUNCH;
    if (s & WOLF_B2) v |= WOLF_X_BLOCK;
    if (s & WOLF_B3) v |= WOLF_X_SPUNCH;
    if (k & 1u) v |= WOLF_X_KICK;
    if (k & 2u) v |= WOLF_X_SKICK;
    if (w->coin_bits & (p ? WOLF_START2 : WOLF_START1)) v |= WOLF_X_START;
    return (uint16_t)v;
}

/* The word v as the controls of the game's player q (0 or 1). */
static void fp_to_player(wolf *w, int q, uint16_t v)
{
    unsigned s = v & 0xFu;
    if (v & WOLF_X_PUNCH) s |= WOLF_B1;
    if (v & WOLF_X_BLOCK) s |= WOLF_B2;
    if (v & WOLF_X_SPUNCH) s |= WOLF_B3;
    w->player[q] = (uint8_t)s;
    unsigned k = 0;
    if (v & WOLF_X_KICK) k |= 1u;
    if (v & WOLF_X_SKICK) k |= 2u;
    w->player[2] = (uint8_t)((w->player[2] & ~(3u << (4 * q))) | (k << (4 * q)));
    if (v & WOLF_X_START)
        w->coin_bits |= q ? WOLF_START2 : WOLF_START1;
}

static int fp_holds(const fp_state *a, int p)
{
    for (int s = 0; s < FP_SLOTS; s++)
        if (a->owner[s] == p)
            return 1;
    return 0;
}

/* The game clears its variables while it starts, so everything is written every frame. */
static void fp_frame_begin(wolf *w, void *state)
{
    fp_state *a = state;
    const uint32_t ps = gsp_read(&w->cpu, a->pstatus, 16);
    /* The cooperative game (the royal rumble: players 1 and 2 on one side against a line of the
     * computer's wrestlers) uses the partners' places for those wrestlers: no partners in it. */
    const int rumble = gsp_read(&w->cpu, a->rumble, 16) != 0;
    uint16_t in[4];
    for (int p = 0; p < 4; p++)
        in[p] = fp_person(w, p);

    /* Places given up: one of the game's players is out of the game for long enough (game over,
     * or a continue not taken). Partners leave with the game: when neither player is in. */
    for (int q = 0; q < 2; q++) {
        if (a->owner[q] < 0 || (ps & (1u << q)))
            a->idle[q] = 0;
        else if (++a->idle[q] > FP_GRACE)
            a->owner[q] = -1;
    }
    if (!(ps & 3u) && a->owner[FP_L1] < 0 && a->owner[FP_L2] < 0)
        a->owner[FP_B1] = a->owner[FP_B2] = -1;

    /* New starts take places. */
    for (int p = 0; p < 4; p++) {
        const int pressed = (in[p] & WOLF_X_START) && !(a->prev[p] & WOLF_X_START);
        a->prev[p] = in[p];
        if (!pressed || fp_holds(a, p))
            continue;
        /* the places are fixed: 1 and 2 the game's own players, 3 and 4 the partners (3 with player 1's side,
         * 4 with player 2's), whoever presses start first */
        int s = p;
        if (a->owner[s] >= 0 || (s < 2 && (ps & (1u << s))))
            continue;
        a->owner[s] = p;
        a->idle[s & 1] = 0;
        /* a partner during a match without partners: the match stops for the select screen (fp_mtr_update) */
        if (s >= FP_B1 && !rumble && gsp_read(&w->cpu, a->gamstate, 16) == FP_INGAME && !gsp_read(&w->cpu, a->buddy, 16))
            gsp_write(&w->cpu, a->req, 16, 1);
    }

    /* The controls of each place. The game's players 1 and 2 keep their own places while nobody
     * holds them, so the attract mode, the menus and a game without players 3 and 4 are as in the
     * original. */
    uint16_t l[2];
    for (int q = 0; q < 2; q++) {
        int o = a->owner[q];
        if (o < 0 && !fp_holds(a, q))
            o = q;
        l[q] = o >= 0 ? in[o] : 0;
    }
    w->coin_bits &= (uint16_t)~(WOLF_START1 | WOLF_START2);
    for (int q = 0; q < 2; q++)
        fp_to_player(w, q, l[q]);

    gsp_write(&w->cpu, a->on, 16, 1);
    int partners = 0;
    /* a partner is made for each place a person holds and for each side with a computer buddy asked for (the
     * settings P1 BUDDY and P2 BUDDY) */
    gsp_write(&w->cpu, a->has, 16,
              rumble ? 0u : (a->owner[FP_B1] >= 0 ? 1u : 0u) | (a->owner[FP_B2] >= 0 ? 2u : 0u) | w->buddy_sides);
    if (w->buddy_sides && !rumble)
        partners = 1;
    for (int b = 0; b < 2; b++) {
        const int o = a->owner[FP_B1 + b];
        uint32_t v = o >= 0 ? in[o] : 0;
        if (o >= 0 && !rumble)
            partners = 1;
        /* a partner's start reaches the select screen (or the match) only later: it is shown until the
         * game has the partner in (fp_join, which the select screen clears when it starts) */
        if (o >= 0 && !gsp_read(&w->cpu, a->join + 16u * (unsigned)b, 16))
            v |= WOLF_X_START;
        gsp_write(&w->cpu, a->in + 16u * (unsigned)b, 16, v);
    }
    /* who is where, for the labels on the select screen (the game's player 1 and 2 shown as
     * players 1 and 2 while nobody else holds them) */
    for (int q = 0; q < FP_SLOTS; q++) {
        int o = a->owner[q];
        if (o < 0 && q < 2 && (ps & (1u << q)) && !fp_holds(a, q))
            o = q;
        gsp_write(&w->cpu, a->who + 16u * (unsigned)q, 16, (uint32_t)(o & 0xFFFF));
    }
    /* Buddy mode exactly while a partner is played by someone (the request bit of the game's own
     * secret code, which the mod owns): without a partner a request left from an earlier game is
     * taken back, so two people are a plain one-on-one. */
    if (w->pu1_addr && w->pu2_addr) {
        uint32_t r1 = gsp_read(&w->cpu, w->pu1_addr, 32), r2 = gsp_read(&w->cpu, w->pu2_addr, 32);
        r1 = partners ? r1 | 128u : r1 & ~128u;
        r2 = partners ? r2 | 128u : r2 & ~128u;
        gsp_write(&w->cpu, w->pu1_addr, 32, r1);
        gsp_write(&w->cpu, w->pu2_addr, 32, r2);
    }
}

static void fp_shutdown(wolf *w, void *state)
{
    const fp_state *a = state;
    gsp_write(&w->cpu, a->on, 16, 0);
    gsp_write(&w->cpu, a->wait, 16, 0);
    free(state);
}

const wwf_mod mod_fourplayer = {
    "fourplayer",
    "four people: players 3 and 4 join like 1 and 2, the third and fourth as buddy mode partners",
    fp_init,
    fp_frame_begin,
    NULL,
    fp_shutdown,
    0, 0, 0, NULL, 0,
};
