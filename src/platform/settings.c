#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const base_names[ACT_PER_PLAYER] = {"UP", "DOWN", "LEFT", "RIGHT", "PUNCH", "BLOCK",
                                                       "SUPER PUNCH", "KICK", "SUPER KICK", "START", "RUN"};
static const char *const shared_names[] = {"COIN 1", "COIN 2", "TEST", "SERVICE"};

static const char *file_name_base(int act)
{
    static char buf[ACT_PER_PLAYER][16];
    snprintf(buf[act], sizeof buf[act], "%s", base_names[act]);
    for (char *p = buf[act]; *p; p++)
        *p = *p == ' ' ? '_' : (*p >= 'A' && *p <= 'Z') ? (char)(*p - 'A' + 'a') : *p;
    return buf[act];
}

const char *settings_base_name(int act)
{
    return act >= 0 && act < ACT_PER_PLAYER ? base_names[act] : "?";
}

/* The names used in the file (lowercase, no spaces). */
static void file_name(int act, char *out, size_t n)
{
    if (act < ACT_PLAYERS * ACT_PER_PLAYER) {
        snprintf(out, n, "key.p%d.%s", act / ACT_PER_PLAYER + 1, base_names[act % ACT_PER_PLAYER]);
    } else {
        snprintf(out, n, "key.%s", shared_names[act - ACT_PLAYERS * ACT_PER_PLAYER]);
    }
    for (char *p = out; *p; p++) {
        if (*p == ' ')
            *p = '_';
        else if (*p >= 'A' && *p <= 'Z')
            *p = (char)(*p - 'A' + 'a');
    }
}

const char *settings_action_name(int act)
{
    static char buf[ACT_COUNT][24];
    if (act < 0 || act >= ACT_COUNT)
        return "?";
    if (act < ACT_PLAYERS * ACT_PER_PLAYER)
        snprintf(buf[act], sizeof buf[act], "P%d %s", act / ACT_PER_PLAYER + 1, base_names[act % ACT_PER_PLAYER]);
    else
        snprintf(buf[act], sizeof buf[act], "%s", shared_names[act - ACT_PLAYERS * ACT_PER_PLAYER]);
    return buf[act];
}

static const char *const option_names[OPT_COUNT] = {
    "NO BLOCKING", "INSTANT COMBOS", "RING OUT MATCH", "SANS RING", "MOVE NAMES", "DRONE METERS",
    "HYPER SPEED", "P1 BUDDY", "P2 BUDDY", "FREE ROAM", "DISABLE LOW FIDELITY", "NO SELECT TIMER", "NO WHITE FLASHES", "NO RED FLASHES", "NO MATCH TIMER"};
static const char *const option_keys[OPT_COUNT] = {
    "no_block", "instant_combo", "ring_out_match", "sans_ring", "move_names", "drone_meters",
    "hyper_speed", "buddy_p1", "buddy_p2", "no_ringout_timer", "all_shadows", "no_select_timer", "no_white_flash", "no_red_flash", "no_match_timer"};

static const char *const option_help[OPT_COUNT] = {
    "NOBODY CAN BLOCK. THE GAME'S OWN SECRET POWERUP.",
    "EVERY COMBO CAN BE DONE, WITHOUT EARNING IT. THE GAME'S OWN SECRET POWERUP.",
    "A MATCH WON BY KNOCKING THE OTHER OUT OF THE RING. THE GAME'S OWN SECRET POWERUP.",
    "THE GAME'S UNFINISHED NO-RING POWERUP. THE MOD SANSRING (TWEAKS) DOES THE SIMPLE HALF OF IT.",
    "THE NAME OF EACH MOVE IS SHOWN WHEN IT IS DONE. THE GAME'S OWN SECRET POWERUP.",
    "THE COMPUTER'S WRESTLERS GET AN ENERGY METER TOO. THE GAME'S OWN SECRET POWERUP.",
    "THE WHOLE GAME RUNS FASTER. THE GAME'S OWN SECRET POWERUP.",
    "A COMPUTER BUDDY FOR PLAYER 1, ON PLAYER 1'S SIDE. BOTH ON IS THE GAME'S OWN BUDDY MODE. ONE ALONE NEEDS THE FOURPLAYER MOD.",
    "A COMPUTER BUDDY FOR PLAYER 2, ON PLAYER 2'S SIDE. BOTH ON IS THE GAME'S OWN BUDDY MODE. ONE ALONE NEEDS THE FOURPLAYER MOD.",
    "WALK OUT OF THE RING WHENEVER YOU LIKE: NO HEALTH IS LOST OUTSIDE, AND NOBODY HAS TO BE OUTSIDE ALREADY. HOLDING THE STICK AGAINST THE ROPES TAKES TWICE AS LONG TO CLIMB OUT; A BUTTON STILL DOES IT AT ONCE.",
    "KEEPS THE RING CROWDED WITH SHADOWS AND DETAIL EVEN WITH MANY WRESTLERS. THE GAME NORMALLY TURNS SOME OFF.",
    "THE CHARACTER SELECT CLOCK NEVER RUNS OUT.",
    "NO WHITE SCREEN FLASHES.",
    "NO RED SCREEN FLASHES.",
    "NO TIME LIMIT IN A ROUND.",
};

const char *settings_option_help(int opt)
{
    return opt >= 0 && opt < OPT_COUNT ? option_help[opt] : "";
}

int settings_mod_wanted(const settings *s, const char *name)
{
    for (int i = 0; i < s->nmods; i++)
        if (!strcmp(s->mods[i], name))
            return 1;
    return 0;
}

int settings_mod_arg(const settings *s, const char *name, int def)
{
    for (int i = 0; i < s->nargs; i++)
        if (!strcmp(s->argname[i], name))
            return s->argval[i];
    return def;
}

void settings_mod_set_arg(settings *s, const char *name, int value)
{
    for (int i = 0; i < s->nargs; i++) {
        if (!strcmp(s->argname[i], name)) {
            s->argval[i] = value;
            return;
        }
    }
    if (s->nargs < WWF_MAX_MODS && strlen(name) < sizeof s->argname[0]) {
        strcpy(s->argname[s->nargs], name);
        s->argval[s->nargs++] = value;
    }
}

void settings_mod_set(settings *s, const char *name, int on)
{
    for (int i = 0; i < s->nmods; i++) {
        if (strcmp(s->mods[i], name))
            continue;
        if (!on) {
            for (int j = i; j + 1 < s->nmods; j++)
                memcpy(s->mods[j], s->mods[j + 1], sizeof s->mods[j]);
            s->nmods--;
        }
        return;
    }
    if (on && s->nmods < WWF_MAX_MODS && strlen(name) < sizeof s->mods[0])
        strcpy(s->mods[s->nmods++], name);
}

const char *settings_option_name(int opt)
{
    return opt >= 0 && opt < OPT_COUNT ? option_names[opt] : "?";
}

int settings_option_get(const settings *s, int opt)
{
    if (opt == OPT_NO_RINGOUT_TIMER)
        return s->no_ringout_timer;
    if (opt == OPT_ALL_SHADOWS)
        return s->all_shadows;
    if (opt == OPT_NO_SELECT_TIMER)
        return s->no_select_timer;
    if (opt == OPT_NO_WHITE_FLASH)
        return s->no_flash_white;
    if (opt == OPT_NO_RED_FLASH)
        return s->no_flash_red;
    if (opt == OPT_NO_MATCH_TIMER)
        return s->no_match_timer;
    return (int)((s->powerups >> opt) & 1u);
}

void settings_option_toggle(settings *s, int opt)
{
    if (opt == OPT_NO_RINGOUT_TIMER)
        s->no_ringout_timer = !s->no_ringout_timer;
    else if (opt == OPT_ALL_SHADOWS)
        s->all_shadows = !s->all_shadows;
    else if (opt == OPT_NO_SELECT_TIMER)
        s->no_select_timer = !s->no_select_timer;
    else if (opt == OPT_NO_WHITE_FLASH)
        s->no_flash_white = !s->no_flash_white;
    else if (opt == OPT_NO_RED_FLASH)
        s->no_flash_red = !s->no_flash_red;
    else if (opt == OPT_NO_MATCH_TIMER)
        s->no_match_timer = !s->no_match_timer;
    else
        s->powerups ^= 1u << opt;
}

void settings_reset_pad(settings *s)
{
    static const int def[ACT_PER_PLAYER] = {
        SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
        SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
        SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_LEFTSHOULDER};
    memcpy(s->pad, def, sizeof s->pad);
}

void settings_reset_keys(settings *s)
{
    static const SDL_Scancode p1[ACT_PER_PLAYER] = {
        SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
        SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F, SDL_SCANCODE_R,
        SDL_SCANCODE_1, SDL_SCANCODE_SPACE};
    static const SDL_Scancode p2[ACT_PER_PLAYER] = {
        SDL_SCANCODE_I, SDL_SCANCODE_K, SDL_SCANCODE_J, SDL_SCANCODE_L,
        SDL_SCANCODE_G, SDL_SCANCODE_H, SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_APOSTROPHE, SDL_SCANCODE_P,
        SDL_SCANCODE_2, SDL_SCANCODE_BACKSLASH};
    /* players 3 (keypad) and 4 (the block above the arrows and the bracket keys), for the fourplayer mod */
    static const SDL_Scancode p3[ACT_PER_PLAYER] = {
        SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_6,
        SDL_SCANCODE_KP_7, SDL_SCANCODE_KP_9, SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_0,
        SDL_SCANCODE_3, SDL_SCANCODE_KP_PLUS};
    static const SDL_Scancode p4[ACT_PER_PLAYER] = {
        SDL_SCANCODE_HOME, SDL_SCANCODE_END, SDL_SCANCODE_DELETE, SDL_SCANCODE_PAGEDOWN,
        SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_RIGHTBRACKET, SDL_SCANCODE_MINUS, SDL_SCANCODE_EQUALS,
        SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_4, SDL_SCANCODE_PAGEUP};
    for (int i = 0; i < ACT_PER_PLAYER; i++) {
        s->key[i] = p1[i];
        s->key[ACT_PER_PLAYER + i] = p2[i];
        s->key[2 * ACT_PER_PLAYER + i] = p3[i];
        s->key[3 * ACT_PER_PLAYER + i] = p4[i];
    }
    settings_reset_pad(s);
    s->key[ACT_COIN1] = SDL_SCANCODE_5;
    s->key[ACT_COIN2] = SDL_SCANCODE_6;
    s->key[ACT_TEST] = SDL_SCANCODE_F2;
    s->key[ACT_SERVICE] = SDL_SCANCODE_F3;
}

static const struct { int w, h; } res_presets[] = {
    {1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160},
    {1024, 768}, {1280, 800}, {1680, 1050}, {1920, 1200}, {3440, 1440}};
#define NUM_RES_PRESETS ((int)(sizeof res_presets / sizeof res_presets[0]))

void settings_res_step(settings *s, int dir)
{
    int at = -1;                       /* index of the current one; -1 = automatic (or one not in the list) */
    for (int i = 0; i < NUM_RES_PRESETS; i++)
        if (res_presets[i].w == s->res_w && res_presets[i].h == s->res_h)
            at = i;
    int next = at + (dir < 0 ? -1 : 1);
    if (at < 0 && s->res_h > 0)        /* a size from the command line or the file: start the list over */
        next = dir < 0 ? NUM_RES_PRESETS - 1 : 0;
    if (next >= NUM_RES_PRESETS || next < -1)
        next = next < 0 ? NUM_RES_PRESETS - 1 : -1;
    s->res_w = next < 0 ? 0 : res_presets[next].w;
    s->res_h = next < 0 ? 0 : res_presets[next].h;
}

void settings_res_text(const settings *s, char *out, size_t n)
{
    if (s->res_h > 0)
        snprintf(out, n, "%dx%d", s->res_w, s->res_h);
    else
        snprintf(out, n, "AUTO");
}

void settings_defaults(settings *s)
{
    memset(s, 0, sizeof *s);
    s->hud_spread = 1;
    s->vol[0] = s->vol[1] = s->vol[2] = s->vol[3] = 100;
    s->smooth = 0;               /* sharp pixels by default */
    s->speed = 100;
    s->skip_selftest = 1;
    /* the defaults of a new install: the arcade monitor's 4:3 pixels, the zoom that follows the wrestlers (out to 30
     * percent, in to 100), the HUD at the edges of a wide view and the HD art on (no_art 0) */
    s->crt_aspect = 1;
    s->all_shadows = 1;          /* DISABLE LOW FIDELITY: the original's savings with more than two wrestlers are off */
    s->dyn_zoom = 1;
    s->dyn_min = 30;
    s->dyn_max = 100;
    settings_reset_keys(s);
}

int settings_load(settings *s, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#')
            continue;
        *eq = 0;
        const char *key = line, *val = eq + 1;
        if (!strcmp(key, "zoom")) {
            double z = atof(val);
            if (z >= 0.25 && z <= 4.0)
                s->zoom = z;
        } else if (!strcmp(key, "mod")) {
            char name[32];
            if (sscanf(val, "%31s", name) == 1)
                settings_mod_set(s, name, 1);
        } else if (!strcmp(key, "modarg")) {
            char name[32];
            int v;
            if (sscanf(val, "%31[^=]=%d", name, &v) == 2)
                settings_mod_set_arg(s, name, v);
        } else if (!strcmp(key, "no_art")) {
            s->no_art = atoi(val) != 0;
        } else if (!strcmp(key, "gpu")) {
            s->gpu = atoi(val) != 0;
        } else if (!strcmp(key, "free_play")) {
            s->free_play = atoi(val) != 0;
        } else if (!strcmp(key, "skip_selftest")) {
            s->skip_selftest = atoi(val) != 0;
        } else if (!strncmp(key, "vol_", 4)) {
            static const char *const vn[4] = {"master", "music", "effects", "crowd"};
            for (int v = 0; v < 4; v++)
                if (!strcmp(key + 4, vn[v]))
                    s->vol[v] = atoi(val) < 0 ? 0 : atoi(val) > (v == 0 ? 300 : 100) ? (v == 0 ? 300 : 100) : atoi(val);
        } else if (!strcmp(key, "smooth")) {
            s->smooth = atoi(val) != 0;
        } else if (!strcmp(key, "integer_scale")) {
            s->integer_scale = atoi(val) != 0;
        } else if (!strcmp(key, "crt_aspect")) {
            s->crt_aspect = atoi(val) != 0;
        } else if (!strcmp(key, "scanlines")) {
            s->scanlines = atoi(val) != 0;
        } else if (!strcmp(key, "speed")) {
            s->speed = atoi(val) < 25 ? 25 : atoi(val) > 300 ? 300 : atoi(val);
        } else if (!strcmp(key, "dyn_zoom")) {
            s->dyn_zoom = atoi(val) != 0;
        } else if (!strcmp(key, "dyn_min") || !strcmp(key, "dyn_max")) {
            int v = atoi(val);
            v = v < 10 ? 10 : v > 200 ? 200 : v;
            if (key[4] == 'm' && key[5] == 'i')
                s->dyn_min = v;
            else
                s->dyn_max = v;
            if (s->dyn_min > s->dyn_max) {
                if (key[5] == 'i')
                    s->dyn_max = s->dyn_min;
                else
                    s->dyn_min = s->dyn_max;
            }
        } else if (!strcmp(key, "res")) {
            int rw = 0, rh = 0;
            if (sscanf(val, "%dx%d", &rw, &rh) == 2 && rw >= 200 && rh >= 100 && rw <= 16384 && rh <= 16384) {
                s->res_w = rw;
                s->res_h = rh;
            } else {
                s->res_w = s->res_h = 0;
            }
        } else if (!strcmp(key, "render_scale")) {
            int v = atoi(val);
            s->render_scale = v < 0 ? 0 : v > 4 ? 4 : v;
        } else if (!strcmp(key, "precache")) {
            s->precache = atoi(val) != 0;
        } else if (!strcmp(key, "limit_warnings")) {
            s->limit_warnings = atoi(val) != 0;
        } else if (!strcmp(key, "debug_overlay")) {
            s->debug_overlay = atoi(val) != 0;
        } else if (!strcmp(key, "hud_spread")) {
            s->hud_spread = atoi(val) != 0;
        } else if (!strcmp(key, "fullscreen")) {
            s->fullscreen = atoi(val) != 0;
        } else if (!strcmp(key, "buddy_mode")) {      /* the old option: a buddy for each player */
            for (int o = OPT_BUDDY_P1; o <= OPT_BUDDY_P2; o++)
                if ((atoi(val) != 0) != settings_option_get(s, o))
                    settings_option_toggle(s, o);
        } else {
            for (int o = 0; o < OPT_COUNT; o++)
                if (!strcmp(key, option_keys[o]) && (atoi(val) != 0) != settings_option_get(s, o))
                    settings_option_toggle(s, o);
            for (int a = 0; a < ACT_PER_PLAYER; a++) {
                char name[64];
                snprintf(name, sizeof name, "pad_%.16s", file_name_base(a));
                if (!strcmp(key, name)) {
                    int b = atoi(val);
                    if (b >= -1 && b < SDL_CONTROLLER_BUTTON_MAX)
                        s->pad[a] = b;
                }
            }
            for (int a = 0; a < ACT_COUNT; a++) {
                char name[64];
                file_name(a, name, sizeof name);
                if (!strcmp(key, name)) {
                    int code = atoi(val);
                    if (code > 0 && code < SDL_NUM_SCANCODES)
                        s->key[a] = (SDL_Scancode)code;
                }
            }
        }
    }
    fclose(f);
    return 1;
}

int settings_save(const settings *s, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return 0;
    fprintf(f, "# wwf settings; keys are SDL scancodes\n");
    if (s->zoom > 0)
        fprintf(f, "zoom=%.2f\n", s->zoom);
    fprintf(f, "fullscreen=%d\n", s->fullscreen);
    fprintf(f, "hud_spread=%d\ndebug_overlay=%d\nprecache=%d\nlimit_warnings=%d\n", s->hud_spread, s->debug_overlay, s->precache,
            s->limit_warnings);
    fprintf(f, "dyn_zoom=%d\ndyn_min=%d\ndyn_max=%d\n", s->dyn_zoom, s->dyn_min, s->dyn_max);
    fprintf(f, "render_scale=%d\n", s->render_scale);
    if (s->res_h > 0)
        fprintf(f, "res=%dx%d\n", s->res_w, s->res_h);
    else
        fprintf(f, "res=auto\n");
    fprintf(f, "smooth=%d\ninteger_scale=%d\ncrt_aspect=%d\nscanlines=%d\nspeed=%d\n", s->smooth, s->integer_scale,
            s->crt_aspect, s->scanlines, s->speed);
    fprintf(f, "vol_master=%d\nvol_music=%d\nvol_effects=%d\nvol_crowd=%d\n", s->vol[0], s->vol[1], s->vol[2], s->vol[3]);
    fprintf(f, "skip_selftest=%d\nno_art=%d\ngpu=%d\n", s->skip_selftest, s->no_art, s->gpu);
    fprintf(f, "free_play=%d\n", s->free_play);
    for (int i = 0; i < s->nmods; i++)
        fprintf(f, "mod=%s\n", s->mods[i]);
    for (int i = 0; i < s->nargs; i++)
        fprintf(f, "modarg=%s=%d\n", s->argname[i], s->argval[i]);
    for (int o = 0; o < OPT_COUNT; o++)
        fprintf(f, "%s=%d\n", option_keys[o], settings_option_get(s, o));
    for (int a = 0; a < ACT_COUNT; a++) {
        char name[64];
        file_name(a, name, sizeof name);
        fprintf(f, "%s=%d\t# %s\n", name, (int)s->key[a], SDL_GetScancodeName(s->key[a]));
    }
    for (int a = 0; a < ACT_PER_PLAYER; a++) {
        const char *bn = s->pad[a] >= 0 ? SDL_GameControllerGetStringForButton((SDL_GameControllerButton)s->pad[a]) : NULL;
        fprintf(f, "pad_%s=%d\t# %s\n", file_name_base(a), s->pad[a], bn ? bn : "none");
    }
    return fclose(f) == 0;
}
