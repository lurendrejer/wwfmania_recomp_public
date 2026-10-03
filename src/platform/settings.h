/* Player-changeable settings of the SDL front end: key bindings and a few
 * options, kept in a small text file (wwf.cfg). */
#ifndef WWF_SETTINGS_H
#define WWF_SETTINGS_H

#include <SDL.h>

#include "mods/mods.h"

/* What a key can do. Per player: the stick, the five buttons of the game
 * (GAME.EQU PLAYER_*_BIT), start, and run (punch + kick at the same time,
 * which is what makes the wrestler run: the action table in the wrestler
 * files maps punch+kick to start_run_anim). */
enum {
    ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT,
    ACT_PUNCH, ACT_BLOCK, ACT_SPUNCH, ACT_KICK, ACT_SKICK,
    ACT_START, ACT_RUN,
    ACT_PER_PLAYER
};
/* Four players' worth of them: players 3 and 4 exist only for mods (mods/fourplayer); their start
 * joins the game as the computer's partner's player. */
#define ACT_PLAYERS 4
/* Then the shared ones. */
enum {
    ACT_COIN1 = ACT_PLAYERS * ACT_PER_PLAYER, ACT_COIN2, ACT_TEST, ACT_SERVICE,
    ACT_COUNT
};

typedef struct {
    SDL_Scancode key[ACT_COUNT];
    int pad[ACT_PER_PLAYER]; /* game controller buttons (SDL_CONTROLLER_BUTTON_*) of all players, -1 = none */
    double zoom;         /* start zoom, 0 = not set */
    int fullscreen;
    int skip_selftest;   /* bypass the power-up test at start (default on) */
    int free_play;       /* the coinage dipswitches set to free play (next start) */
    unsigned powerups;   /* GAME.EQU powerup bits forced on (0 = none) */
    int no_ringout_timer;
    int dyn_zoom;        /* zoom in a match follows how far apart the wrestlers are */
    int dyn_min, dyn_max;  /* its limits in percent (10..200): farthest out, closest in */
    int res_w, res_h;    /* window size in pixels, 0 = automatic (the desktop's shape); takes effect at start */
    int no_select_timer;  /* the character select clock never runs out */
    int no_match_timer;   /* the round's clock does not count down */
    int no_flash_white, no_flash_red;   /* the full screen flashes are left out */
    int all_shadows;     /* shadows also with more than two wrestlers */
    char mods[WWF_MAX_MODS][32]; /* mods switched on in the menu, started with the game */
    int nmods;
    char argname[WWF_MAX_MODS][32]; /* the mods' numbers (wwf_mod.arg_*), on or off */
    int argval[WWF_MAX_MODS];
    int nargs;
    int smooth, integer_scale, crt_aspect, scanlines; /* display options (menu page DISPLAY) */
    int render_scale;    /* render scale (framebuffer pixels per game pixel), 0 = automatic; takes effect at start */
    int no_art;          /* the high-resolution art is left out (takes effect at start) */
    int gpu;             /* sprite drawing and the palette lookup on the GPU (OpenGL ES 2; takes effect at start) */
    int speed;           /* game speed in percent (100 = the arcade's 54.7 frames/s) */
    int vol[4];          /* percent: master, music, effects, crowd (default 100) */
    int hud_spread;      /* keep the HUD at the edges of a wide view (default on) */
    int precache;        /* read all HD art and sounds at start instead of when they are first needed (needs plenty of RAM) */
    int limit_warnings;  /* announce a limit (memory, size) on the screen when it comes into effect; the LIMITS list is always there */
    int debug_overlay;   /* show frame times, GPU state, memory and the like on the screen (menu page DISPLAY) */
} settings;

/* A mod's saved number, or `def` when there is none; setting it. */
int settings_mod_arg(const settings *s, const char *name, int def);
void settings_mod_set_arg(settings *s, const char *name, int value);

void settings_defaults(settings *s);

/* The window sizes offered in the F1 menu (after "automatic"). Step: dir = +1 / -1 goes to the next / previous
 * one, wrapping through "automatic" (res_h 0). Text: "AUTO" or "1920x1080". */
void settings_res_step(settings *s, int dir);
void settings_res_text(const settings *s, char *out, size_t n);
void settings_reset_keys(settings *s);
void settings_reset_pad(settings *s);

/* The game options of the menu, in the order of the powerup bits, then the
 * ring-out timer. */
enum { OPT_NO_BLOCK, OPT_INSTANT_COMBO, OPT_RING_OUT_MATCH, OPT_SANS_RING, OPT_MOVE_NAMES,
       OPT_DRONE_METERS, OPT_HYPER_SPEED, OPT_BUDDY_P1, OPT_BUDDY_P2, OPT_NO_RINGOUT_TIMER, OPT_ALL_SHADOWS, OPT_NO_SELECT_TIMER, OPT_NO_WHITE_FLASH, OPT_NO_RED_FLASH, OPT_NO_MATCH_TIMER, OPT_COUNT };
int settings_mod_wanted(const settings *s, const char *name);
void settings_mod_set(settings *s, const char *name, int on);
const char *settings_option_name(int opt);
/* One or two sentences on what the option does (the menu shows it under the list). */
const char *settings_option_help(int opt);
int settings_option_get(const settings *s, int opt);
void settings_option_toggle(settings *s, int opt);

/* "P1 UP", "COIN 1", ... */
const char *settings_action_name(int act);
const char *settings_base_name(int act); /* "UP", "PUNCH", ... for one player */

/* Missing file or lines are fine (defaults stay). Returns 1 if a file was read. */
int settings_load(settings *s, const char *path);
int settings_save(const settings *s, const char *path);

#endif
