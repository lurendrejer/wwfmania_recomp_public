/*
 * Game controllers (SDL's game controller mapping): the first controller plays as player 1, the
 * second as player 2, and the third and fourth as players 3 and 4 (only used by mods). Their input is added to what the keyboard gives.
 *
 * The left stick always moves. The buttons are set in the F1 menu (settings.pad); the defaults:
 *   D-pad move   A punch   X kick   B block   Y super punch   right bumper super kick
 *   left bumper run (punch + kick)   Start start   Back coin
 */
#ifndef WWF_GAMEPAD_H
#define WWF_GAMEPAD_H

#include <SDL.h>

#include "wolf/wolf.h"

void gamepad_open(void);                 /* opens the controllers that are connected */
void gamepad_event(const SDL_Event *e);  /* hot plugging */
void gamepad_close(void);
/* ORs the controllers' state into w->player, w->extra_player and w->coin_bits (after the keyboard's). */
void gamepad_read(wolf *w, const int *buttons /* settings.pad, ACT_PER_PLAYER entries */);

#endif
