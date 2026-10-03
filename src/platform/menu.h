/* In-game menu (F1): zoom, full screen, key bindings, save. Drawn over the
 * game with a built-in 5x7 font; the game is paused while it is open. */
#ifndef WWF_MENU_H
#define WWF_MENU_H

#include <SDL.h>

#include "settings.h"

typedef struct {
    int open;
    int page;          /* 0 main, 1 controls */
    int sel;           /* selected line of the page */
    int top;           /* first visible line of a long page */
    int waiting;       /* waiting for a key to bind */
    int restart;       /* "apply and restart" chosen: the front end starts the game again */
    double *zoom;      /* the front end's zoom, changed live */
    double zmin;
    int *fullscreen;   /* mirrored from the window, toggled by the menu */
    char status[64];   /* last message (saved, error) */
    const char *cfg_path;
    settings *set;
    struct gsp_hw *wolf; /* the machine, for switching mods on and off */
    /* Limits the front end has put on something (memory, the device): shown on the display page, one at a time with
     * left/right. Filled by menu_limits_clear / menu_limit_add. */
    char limit[8][160];
    int nlimits, lim_sel;
    int precache_ok;     /* the device has the RAM for the PRECACHE option */
    char precache_note[220]; /* what the option says about this device */
} menu;

/* Replaces the list of limits (at most 8, 160 characters each); the selection stays where it was if it still exists. */
void menu_limits_set(menu *m, char lines[][160], int n);

void menu_init(menu *m, settings *s, double *zoom, double zmin, int *fullscreen, const char *cfg_path,
               struct gsp_hw *wolf);

/* Returns 1 if the event was used (the menu is open, or F1 opened it). */
int menu_event(menu *m, const SDL_Event *e);

/* Draws over the whole output (w x h pixels). */
void menu_draw(const menu *m, SDL_Renderer *ren, int w, int h);

#endif
