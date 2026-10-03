#include "menu.h"

#include "font.h"

#include <stdio.h>
#include <string.h>

#include "mods/mods.h"
#include "sound/sound.h"
#include "wolf/wolf.h"

/* The main page: full screen, then the pages in alphabetical order, then save, restart and close. The pages: 0 main,
 * 1 controls, 2 game options, 3 mods, 4 display, 5 volume, 6 debug, 7 tweaks. */
enum { MAIN_FULL, MAIN_CONTROLS, MAIN_DEBUG, MAIN_DISPLAY, MAIN_GAME, MAIN_MODS, MAIN_TWEAKS, MAIN_VOLUME, MAIN_SAVE, MAIN_RESTART, MAIN_CLOSE, MAIN_COUNT };
enum { VOL_MASTER, VOL_MUSIC, VOL_FX, VOL_CROWD, VOL_BACK, VOL_COUNT };
enum { DBG_INFO, DBG_LIMITS, DBG_RESET, DBG_BACK, DBG_COUNT };

/* The page's line on the main page, for coming back to it. */
static int main_line_of(int page)
{
    return page == 1 ? MAIN_CONTROLS : page == 2 ? MAIN_GAME : page == 3 ? MAIN_MODS : page == 4 ? MAIN_DISPLAY :
           page == 5 ? MAIN_VOLUME : page == 7 ? MAIN_TWEAKS : MAIN_DEBUG;
}

void menu_limits_set(menu *m, char lines[][160], int n)
{
    m->nlimits = n > 8 ? 8 : n;
    for (int i = 0; i < m->nlimits; i++)
        snprintf(m->limit[i], sizeof m->limit[0], "%s", lines[i]);
    if (m->lim_sel >= m->nlimits)
        m->lim_sel = 0;
}

void menu_init(menu *m, settings *s, double *zoom, double zmin, int *fullscreen, const char *cfg_path,
               struct gsp_hw *wolf)
{
    memset(m, 0, sizeof *m);
    m->set = s;
    m->zoom = zoom;
    m->zmin = zmin;
    m->fullscreen = fullscreen;
    m->cfg_path = cfg_path;
    m->wolf = wolf;
}

/* The mods are on two pages: TWEAKS (page 7) has the ones that can be switched in a running game (wwf_mod.live), MODS
 * (page 3) the others, which show from the next match or start. */
static int page_mods(int page)
{
    int n = 0;
    for (const wwf_mod *const *p = mods_builtin(); *p; p++)
        if (((*p)->live != 0) == (page == 7))
            n++;
    return n;
}

static const wwf_mod *page_mod(int page, int line)
{
    for (const wwf_mod *const *p = mods_builtin(); *p; p++)
        if (((*p)->live != 0) == (page == 7) && line-- == 0)
            return *p;
    return NULL;
}

static int builtin_count(void)         /* the mods on the MODS page */
{
    return page_mods(3);
}

/* The value of a mod's number as words, so that nobody has to look it up in a README (the mod's own label is the
 * short name of the number). */
static void mod_value_text(const wwf_mod *mod, int v, char *out, size_t n)
{
    static const char *const lines[] = {"FIRST ONE THEY SAY", "HELLO", "OH MY", "GOODNIGHT", "TO THE FACE", "WHAT A BLOW"};
    static const char *const ladder[] = {"ROYAL RUMBLE", "INTERCONTINENTAL", "WWF TITLE", "ASK FOR THE BELT"};
    static const char *const refill[] = {"", "START 1 REFILLS", "HUMANS NEVER LOSE", "NOBODY LOSES"};
    const char *name = mod->name;

    if (!strcmp(name, "damage") || !strcmp(name, "moongravity"))
        snprintf(out, n, "%d%%", v * 10);
    else if (!strcmp(name, "matchtime"))
        snprintf(out, n, "%d SECONDS", v);
    else if (!strcmp(name, "cpuskill"))
        snprintf(out, n, v == 0 ? "NORMAL" : v < 0 ? "EASIER %d" : "HARDER %d", v < 0 ? -v : v);
    else if (!strcmp(name, "disco"))
        snprintf(out, n, "SPEED %d", v);
    else if (!strcmp(name, "training") && v >= 1 && v <= 3)
        snprintf(out, n, "%s", refill[v]);
    else if (!strcmp(name, "brokenrecord") && v >= 0 && v <= 5)
        snprintf(out, n, "%s", lines[v]);
    else if (!strcmp(name, "wrongnames"))
        snprintf(out, n, "%d ON THE ROSTER", v);
    else if (!strcmp(name, "coopladder") && v >= 0 && v <= 3)
        snprintf(out, n, "%s", ladder[v]);
    else if (!strcmp(name, "bottombuckles"))
        snprintf(out, n, v == 2 ? "ONLY AT THOSE OUTSIDE" : "LEAP AT ANYONE");
    else if (!strcmp(name, "chair"))
        snprintf(out, n, v ? "KEEP THE CHAIR" : "ONE SWING");
    else if (!strcmp(name, "morebuddies"))
        snprintf(out, n, "%d BUDDIES EACH", v);
    else if (!strcmp(name, "moredrones"))
        snprintf(out, n, "%d OPPONENTS", v);
    else if (!strcmp(name, "rounds"))
        snprintf(out, n, v == 1 ? "ONE ROUND" : v == 2 ? "BEST OF 3" : "BEST OF 5");
    else
        snprintf(out, n, "%d", v);
}

/* A mod's current number: the live one when it is on, else the saved one. */
static int mod_value(const menu *m, const wwf_mod *mod)
{
    return mods_is_enabled(m->wolf, mod) ? mods_arg(m->wolf, mod) : settings_mod_arg(m->set, mod->name, mod->arg_default);
}

/* page 3, MODS: the mods that show from the next match (they switch at once), then what only takes effect at start */
enum { P3_SELFTEST, P3_FREE, P3_APPLY, P3_BACK, P3_COUNT };
#define P3(n) (builtin_count() + (n))

static const char *const p3_help[P3_COUNT - 2] = {
    "SKIP THE POWER-ON SELF TEST. TAKES EFFECT AT NEXT START.",
    "NO COINS NEEDED. TAKES EFFECT AT NEXT START.",
};

/* Saves everything as it is now (the mods that are on, their numbers) and asks the front end to
 * start the game again, so what only takes effect at start (free play, the self test, a mod's
 * changes to the game's start) does. */
static void apply_and_restart(menu *m)
{
    m->set->zoom = *m->zoom;
    for (const wwf_mod *const *p = mods_builtin(); *p; p++) {
        settings_mod_set(m->set, (*p)->name, mods_is_enabled(m->wolf, *p));
        if ((*p)->arg_max > (*p)->arg_min && mods_is_enabled(m->wolf, *p))
            settings_mod_set_arg(m->set, (*p)->name, mods_arg(m->wolf, *p));
    }
    if (!settings_save(m->set, m->cfg_path)) {
        snprintf(m->status, sizeof m->status, "CANNOT WRITE %.40s", m->cfg_path);
        return;
    }
    m->restart = 1;
    m->open = 0;
}

/* page 4: the display options and the speed, as ints in the settings */
enum { DSP_SMOOTH, DSP_INTEGER, DSP_CRT, DSP_SCAN, DSP_SPEED, DSP_DYN, DSP_DYN_MIN, DSP_DYN_MAX, DSP_HUD, DSP_RES, DSP_SCALE, DSP_ART, DSP_GPU, DSP_PRECACHE, DSP_LIMITS, DSP_COUNT };
#define DSP_APPLY DSP_COUNT            /* then BACK */
#define DSP_BACK (DSP_COUNT + 1)

static int *dsp_field(settings *s, int i)
{
    switch (i) {
    case DSP_SMOOTH: return &s->smooth;
    case DSP_INTEGER: return &s->integer_scale;
    case DSP_CRT: return &s->crt_aspect;
    case DSP_SCAN: return &s->scanlines;
    case DSP_DYN: return &s->dyn_zoom;
    case DSP_DYN_MIN: return &s->dyn_min;
    case DSP_DYN_MAX: return &s->dyn_max;
    case DSP_HUD: return &s->hud_spread;
    case DSP_GPU: return &s->gpu;
    case DSP_PRECACHE: return &s->precache;
    default: return &s->speed;
    }
}

static const char *const dsp_names[DSP_COUNT] = {"SMOOTHING", "INTEGER SCALING", "4:3 PIXEL ASPECT", "SCANLINES", "GAME SPEED", "DYNAMIC ZOOM", "  FARTHEST OUT", "  CLOSEST IN", "HUD AT EDGES", "WINDOW SIZE", "RENDER SCALE", "HD ART", "GPU DRAWING", "PRECACHE", "LIMITS"};
static const char *const dsp_help[DSP_COUNT] = {
    "SMOOTH (LINEAR) OR SHARP (NEAREST) PIXELS WHEN THE PICTURE IS ENLARGED.",
    "ENLARGE ONLY BY WHOLE NUMBERS, SO EVERY PIXEL IS THE SAME SIZE. THE REST OF THE WINDOW STAYS BLACK.",
    "THE ARCADE MONITOR SHOWED THE PICTURE AT 4:3, SO ITS PIXELS WERE A LITTLE NARROWER THAN TALL. ON = THE SAME SHAPE. THE VIEW IS SIZED AT START: USE APPLY AND RESTART AFTER SWITCHING IT.",
    "A DARK LINE UNDER EVERY ROW OF THE GAME, LIKE A CRT.",
    "25 TO 300 PERCENT OF THE ARCADE SPEED (LEFT/RIGHT, STEPS OF 5). HOLD TAB TO FAST FORWARD AT 4X, WITHOUT SOUND.",
    "IN A MATCH THE ZOOM FOLLOWS HOW FAR APART THE WRESTLERS ARE: OUT WHEN THEY SPREAD, BACK IN WHEN THEY COME TOGETHER. WITHIN THE TWO LIMITS BELOW. THE VIEW IS SIZED AT START, SO USE APPLY AND RESTART AFTER GOING FARTHER OUT THAN BEFORE.",
    "THE ZOOM IT MAY GO OUT TO, 10 TO 200 PERCENT (100 = THE NORMAL VIEW, 10 SHOWS TEN TIMES AS MUCH). LEFT/RIGHT, STEPS OF 10. FARTHER OUT USES MORE MEMORY.",
    "THE ZOOM IT MAY GO IN TO, 10 TO 200 PERCENT. LEFT/RIGHT, STEPS OF 10.",
    "IN A WIDE VIEW THE HUD (NAMES, ENERGY BARS, TIMER) STAYS AT THE EDGES OF THE SCREEN. OFF = AT THE ORIGINAL PLACES, IN THE MIDDLE.",
    "THE WINDOW'S SIZE IN PIXELS (LEFT/RIGHT). AUTO = THE DESKTOP'S SHAPE. THE VIEW AND THE SHARPNESS FOLLOW THE SIZE. TAKES EFFECT AFTER APPLY AND RESTART.",
    "PICTURE POINTS PER GAME PIXEL (LEFT/RIGHT): AUTO, 1 TO 4. HIGHER = SHARPER HD ART BUT MUCH MORE WORK FOR THE CPU AND MORE MEMORY. TAKES EFFECT AFTER APPLY AND RESTART.",
    "THE HIGH-RESOLUTION ART (ENTER = ON/OFF). OFF = THE ORIGINAL PICTURES, MUCH FASTER ON A SLOW DEVICE. TAKES EFFECT AFTER APPLY AND RESTART.",
    "DRAW THE SPRITES AND LOOK UP THE COLORS ON THE GRAPHICS CHIP (OPENGL ES 2) INSTEAD OF THE CPU (ENTER = ON/OFF). SAME PICTURE; FALLS BACK TO THE CPU IF THE DEVICE CANNOT DO IT. TAKES EFFECT AFTER APPLY AND RESTART.",
    "READ ALL HD ART AND SOUNDS AT START, AND KEEP THEM, INSTEAD OF WHEN THEY ARE FIRST NEEDED: NO LOADING DURING THE GAME. NEEDS PLENTY OF RAM. TAKES EFFECT AFTER APPLY AND RESTART.",
    "WHAT THE GAME HAS HAD TO LIMIT ON THIS DEVICE (MEMORY, SIZE). LEFT/RIGHT SHOWS THE NEXT ONE.",
};

enum { CTL_PAD = ACT_COUNT, CTL_BACK = ACT_COUNT + ACT_PER_PLAYER };

/* The order the controls are listed in (keys of each player, and the pad buttons): the stick, then punch, super punch,
 * kick, super kick, run, block, start. The settings keep their own order; only the lines of the menu follow this one. */
static const int ctl_order[ACT_PER_PLAYER] = {ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_PUNCH, ACT_SPUNCH,
                                              ACT_KICK, ACT_SKICK, ACT_RUN, ACT_BLOCK, ACT_START};

/* The action of a line of the controls page (a key line, below CTL_PAD): the players' keys in that order, then the
 * shared ones (coins, test, service) as they are. */
static int ctl_act(int line)
{
    if (line < ACT_PLAYERS * ACT_PER_PLAYER)
        return line / ACT_PER_PLAYER * ACT_PER_PLAYER + ctl_order[line % ACT_PER_PLAYER];
    return line;
}

/* The game options in alphabetical order, like the mods: line -> option. */
static int opt_at(int line)
{
    static int ord[OPT_COUNT], ready;
    if (!ready) {
        for (int i = 0; i < OPT_COUNT; i++) {
            int j = i;
            while (j > 0 && strcmp(settings_option_name(ord[j - 1]), settings_option_name(i)) > 0) {
                ord[j] = ord[j - 1];
                j--;
            }
            ord[j] = i;
        }
        ready = 1;
    }
    return ord[line];
}

static int page_lines(const menu *m)
{
    if (m->page == 0)
        return MAIN_COUNT;
    if (m->page == 4)
        return DSP_COUNT + 2;                /* display and speed, APPLY AND RESTART, then BACK */
    if (m->page == 2)
        return OPT_COUNT + 1;                /* game options, then BACK */
    if (m->page == 5)
        return VOL_COUNT;
    if (m->page == 6)
        return DBG_COUNT;
    if (m->page == 7)
        return page_mods(7) + 1;             /* the tweaks, then BACK */
    if (m->page == 3)
        return P3(P3_COUNT);                 /* mods, start options, RESTART, then BACK */
    return CTL_BACK + 1;                     /* controls: every key, every pad button, then BACK */
}

static void apply_volume(const menu *m)
{
    if (m->wolf->snd)
        snd_set_gains(m->wolf->snd, (float)m->set->vol[0] / 100, (float)m->set->vol[1] / 100,
                      (float)m->set->vol[2] / 100, (float)m->set->vol[3] / 100);
}

static void rebind(menu *m, int act, SDL_Scancode key)
{
    settings *s = m->set;
    for (int a = 0; a < ACT_COUNT; a++)          /* the key moves: swap with its old owner */
        if (a != act && s->key[a] == key)
            s->key[a] = s->key[act];
    s->key[act] = key;
}

static void rebind_pad(menu *m, int act, int button)
{
    settings *s = m->set;
    for (int a = 0; a < ACT_PER_PLAYER; a++)
        if (a != act && s->pad[a] == button)
            s->pad[a] = s->pad[act];
    s->pad[act] = button;
}

/* A controller button as the key it stands for in the menu (0 = none). */
static SDL_Keycode pad_key(int button)
{
    switch (button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return SDLK_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return SDLK_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return SDLK_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return SDLK_RIGHT;
    case SDL_CONTROLLER_BUTTON_A: return SDLK_RETURN;
    case SDL_CONTROLLER_BUTTON_B: return SDLK_ESCAPE;
    case SDL_CONTROLLER_BUTTON_GUIDE: return SDLK_F1;
    default: return 0;
    }
}

/* Start and Back pressed together: another way to open the menu where the Guide button belongs to the system
 * (Android TV). Turned into a Guide press. */
static int start_back_chord(const SDL_Event *e)
{
    if (e->type != SDL_CONTROLLERBUTTONDOWN)
        return 0;
    int b = e->cbutton.button;
    int other = b == SDL_CONTROLLER_BUTTON_START ? SDL_CONTROLLER_BUTTON_BACK :
                b == SDL_CONTROLLER_BUTTON_BACK ? SDL_CONTROLLER_BUTTON_START : -1;
    SDL_GameController *c = other < 0 ? NULL : SDL_GameControllerFromInstanceID(e->cbutton.which);
    return c && SDL_GameControllerGetButton(c, (SDL_GameControllerButton)other);
}

int menu_event(menu *m, const SDL_Event *e)
{
    if (e->type == SDL_CONTROLLERBUTTONDOWN || e->type == SDL_CONTROLLERBUTTONUP) {
        SDL_Event ke;
        int b = e->cbutton.button;

        if (!m->open && start_back_chord(e))
            b = SDL_CONTROLLER_BUTTON_GUIDE;

        if (!m->open && !(e->type == SDL_CONTROLLERBUTTONDOWN && b == SDL_CONTROLLER_BUTTON_GUIDE))
            return 0;
        if (e->type == SDL_CONTROLLERBUTTONUP)
            return 1;
        if (m->waiting == 2) {              /* the next button is the new binding (Guide cancels) */
            if (b != SDL_CONTROLLER_BUTTON_GUIDE)
                rebind_pad(m, ctl_order[m->sel - CTL_PAD], b);
            m->waiting = 0;
            return 1;
        }
        if (m->waiting || !pad_key(b))
            return 1;
        memset(&ke, 0, sizeof ke);
        ke.type = SDL_KEYDOWN;
        ke.key.keysym.sym = pad_key(b);
        return menu_event(m, &ke);
    }
    if (!m->open) {
        if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_F1) {
            m->open = 1;
            m->page = 0;
            m->sel = 0;
            m->status[0] = 0;
            return 1;
        }
        return 0;
    }
    if (e->type != SDL_KEYDOWN)
        return e->type == SDL_KEYUP;      /* swallow releases too */
    SDL_Keycode k = e->key.keysym.sym;
    m->status[0] = 0;
    if (m->waiting) {                       /* the next key is the new binding */
        if (m->waiting == 1 && k != SDLK_ESCAPE)
            rebind(m, ctl_act(m->sel), e->key.keysym.scancode);
        m->waiting = 0;
        return 1;
    }
    int n = page_lines(m);
    if (k == SDLK_F1 || (k == SDLK_ESCAPE && m->page == 0)) {
        m->open = 0;
        return 1;
    }
    if (k == SDLK_ESCAPE) {
        m->sel = main_line_of(m->page);
        m->page = 0;
        return 1;
    }
    if (k == SDLK_UP)
        m->sel = (m->sel + n - 1) % n;
    else if (k == SDLK_DOWN)
        m->sel = (m->sel + 1) % n;
    else if (m->page == 4 && (k == SDLK_LEFT || k == SDLK_RIGHT) && m->sel < DSP_COUNT) {
        int *f = dsp_field(m->set, m->sel);
        if (m->sel == DSP_DYN_MIN || m->sel == DSP_DYN_MAX) {
            *f += k == SDLK_RIGHT ? 10 : -10;
            *f = *f < 10 ? 10 : *f > 200 ? 200 : *f;
            if (m->set->dyn_min > m->set->dyn_max) {           /* the limits keep their order */
                if (m->sel == DSP_DYN_MIN)
                    m->set->dyn_max = m->set->dyn_min;
                else
                    m->set->dyn_min = m->set->dyn_max;
            }
        } else if (m->sel == DSP_SPEED) {
            *f += k == SDLK_RIGHT ? 5 : -5;
            *f = *f < 25 ? 25 : *f > 300 ? 300 : *f;
        } else if (m->sel == DSP_RES) {
            settings_res_step(m->set, k == SDLK_RIGHT ? 1 : -1);
        } else if (m->sel == DSP_SCALE) {
            int v = m->set->render_scale + (k == SDLK_RIGHT ? 1 : -1);
            m->set->render_scale = v < 0 ? 0 : v > 4 ? 4 : v;
        } else if (m->sel == DSP_ART) {
            m->set->no_art = !m->set->no_art;
        } else if (m->sel == DSP_LIMITS) {
            if (m->nlimits > 0)
                m->lim_sel = (m->lim_sel + (k == SDLK_RIGHT ? 1 : m->nlimits - 1)) % m->nlimits;
        } else {
            *f = !*f;
        }
    }
    else if (m->page == 5 && (k == SDLK_LEFT || k == SDLK_RIGHT) && m->sel <= VOL_CROWD) {
        int *v = &m->set->vol[m->sel];
        *v += k == SDLK_RIGHT ? 10 : -10;
        *v = *v < 0 ? 0 : *v > (m->sel == VOL_MASTER ? 300 : 100) ? (m->sel == VOL_MASTER ? 300 : 100) : *v;   /* the master goes to 300 */
        apply_volume(m);
    }
    else if ((m->page == 3 || m->page == 7) && (k == SDLK_LEFT || k == SDLK_RIGHT) && m->sel < page_mods(m->page) &&
             page_mod(m->page, m->sel)->arg_max > page_mod(m->page, m->sel)->arg_min) {
        const wwf_mod *mod = page_mod(m->page, m->sel);
        int v = mod_value(m, mod) + (k == SDLK_RIGHT ? 1 : -1);
        v = v < mod->arg_min ? mod->arg_min : v > mod->arg_max ? mod->arg_max : v;
        settings_mod_set_arg(m->set, mod->name, v);
        mods_set_arg(m->wolf, mod, v);
    }
    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) {
        if (m->page == 0) {
            switch (m->sel) {
            case MAIN_FULL:
                *m->fullscreen = !*m->fullscreen;
                m->set->fullscreen = *m->fullscreen;
                break;
            case MAIN_CONTROLS: m->page = 1; m->sel = 0; m->top = 0; break;
            case MAIN_DEBUG: m->page = 6; m->sel = 0; break;
            case MAIN_DISPLAY: m->page = 4; m->sel = 0; break;
            case MAIN_GAME: m->page = 2; m->sel = 0; break;
            case MAIN_MODS: m->page = 3; m->sel = 0; break;
            case MAIN_TWEAKS: m->page = 7; m->sel = 0; break;
            case MAIN_VOLUME: m->page = 5; m->sel = 0; break;
            case MAIN_SAVE:
                m->set->zoom = *m->zoom;
                for (const wwf_mod *const *p = mods_builtin(); *p; p++)      /* the numbers given on the command line */
                    if ((*p)->arg_max > (*p)->arg_min && mods_is_enabled(m->wolf, *p))
                        settings_mod_set_arg(m->set, (*p)->name, mods_arg(m->wolf, *p));
                snprintf(m->status, sizeof m->status, settings_save(m->set, m->cfg_path) ? "SAVED %s" : "CANNOT WRITE %s",
                         m->cfg_path);
                break;
            case MAIN_RESTART: apply_and_restart(m); break;
            default: m->open = 0; break;
            }
        } else if (m->page == 5) {
            if (m->sel == VOL_BACK) {
                m->page = 0;
                m->sel = MAIN_VOLUME;
            } else {
                m->set->vol[m->sel] = 100;
                apply_volume(m);
            }
        } else if (m->page == 6) {
            if (m->sel == DBG_INFO) {
                m->set->debug_overlay = !m->set->debug_overlay;
            } else if (m->sel == DBG_LIMITS) {
                m->set->limit_warnings = !m->set->limit_warnings;
            } else if (m->sel == DBG_RESET) {
                settings_reset_keys(m->set);
                snprintf(m->status, sizeof m->status, "KEYS AND BUTTONS RESET");
            } else {
                m->page = 0;
                m->sel = MAIN_DEBUG;
            }
        } else if (m->page == 4) {
            if (m->sel == DSP_APPLY) {
                apply_and_restart(m);
            } else if (m->sel == DSP_BACK) {
                m->page = 0;
                m->sel = MAIN_DISPLAY;
            } else if (m->sel == DSP_DYN_MIN) {
                m->set->dyn_min = 30;
            } else if (m->sel == DSP_DYN_MAX) {
                m->set->dyn_max = 100;
            } else if (m->sel == DSP_SPEED) {
                *dsp_field(m->set, DSP_SPEED) = 100;
            } else if (m->sel == DSP_RES) {
                settings_res_step(m->set, 1);
            } else if (m->sel == DSP_SCALE) {
                m->set->render_scale = 0;
            } else if (m->sel == DSP_ART) {
                m->set->no_art = !m->set->no_art;
            } else if (m->sel == DSP_LIMITS) {
                if (m->nlimits > 0)
                    m->lim_sel = (m->lim_sel + 1) % m->nlimits;
            } else {
                int *f = dsp_field(m->set, m->sel);
                *f = !*f;
            }
        } else if (m->page == 3) {
            int n = builtin_count();
            if (m->sel == P3(P3_APPLY)) {
                apply_and_restart(m);
            } else if (m->sel == P3(P3_BACK)) {
                m->page = 0;
                m->sel = MAIN_MODS;
            } else if (m->sel == P3(P3_SELFTEST)) {
                m->set->skip_selftest = !m->set->skip_selftest;
            } else if (m->sel == P3(P3_FREE)) {
                m->set->free_play = !m->set->free_play;
            } else if (m->sel >= n) {
                /* nothing */
            } else {
                const wwf_mod *mod = page_mod(3, m->sel);
                if (mods_is_enabled(m->wolf, mod)) {
                    mods_remove(m->wolf, mod);
                    settings_mod_set(m->set, mod->name, 0);
                } else {
                    char err[64];
                    if (mods_add(m->wolf, mod, err, sizeof err)) {
                        settings_mod_set(m->set, mod->name, 1);
                        mods_set_arg(m->wolf, mod, settings_mod_arg(m->set, mod->name, mod->arg_default));
                    }
                    else
                        snprintf(m->status, sizeof m->status, "%.60s", err);
                }
            }
        } else if (m->page == 7) {
            if (m->sel >= page_mods(7)) {
                m->page = 0;
                m->sel = MAIN_TWEAKS;
            } else {
                const wwf_mod *mod = page_mod(7, m->sel);
                if (mods_is_enabled(m->wolf, mod)) {
                    mods_remove(m->wolf, mod);
                    settings_mod_set(m->set, mod->name, 0);
                } else {
                    char err[64];
                    if (mods_add(m->wolf, mod, err, sizeof err)) {
                        settings_mod_set(m->set, mod->name, 1);
                        mods_set_arg(m->wolf, mod, settings_mod_arg(m->set, mod->name, mod->arg_default));
                    } else
                        snprintf(m->status, sizeof m->status, "%.60s", err);
                }
            }
        } else if (m->page == 2) {
            if (m->sel == OPT_COUNT) {
                m->page = 0;
                m->sel = MAIN_GAME;
            } else {
                settings_option_toggle(m->set, opt_at(m->sel));
            }
        } else if (m->sel == CTL_BACK) {
            m->page = 0;
            m->sel = MAIN_CONTROLS;
        } else {
            m->waiting = m->sel >= CTL_PAD ? 2 : 1;
        }
    }
    return 1;
}

#define DESC_ROWS 4

/* Wraps str into DESC_ROWS rows of at most `width` characters (width <= 127); what does not fit ends in "...". */
static void wrap_text(const char *str, int width, char rows[DESC_ROWS][128])
{
    int r = 0, n = 0;

    if (width > 127)
        width = 127;
    if (width < 8)
        width = 8;
    for (int i = 0; i < DESC_ROWS; i++)
        rows[i][0] = 0;
    while (*str && r < DESC_ROWS) {
        int len, cut;

        while (*str == ' ')
            str++;
        len = (int)strlen(str);
        if (len <= width) {
            cut = len;
        } else {
            cut = width;
            while (cut > 0 && str[cut] != ' ')      /* last space that leaves the row within width */
                cut--;
            if (cut == 0)
                cut = width;                        /* a word longer than a row */
        }
        n = cut;
        while (n > 0 && str[n - 1] == ' ')
            n--;
        memcpy(rows[r], str, (size_t)n);
        rows[r][n] = 0;
        str += cut;
        r++;
    }
    while (*str == ' ')
        str++;
    if (*str) {                                     /* too long: mark the cut on the last row */
        int l = (int)strlen(rows[DESC_ROWS - 1]);
        if (l > width - 3)
            l = width - 3;
        memcpy(rows[DESC_ROWS - 1] + l, "...", 4);
    }
}

void menu_draw(const menu *m, SDL_Renderer *ren, int w, int h)
{
    if (!m->open)
        return;
    int s = h / 260;                      /* font pixel size: about 30 lines fit */
    if (s < 2)
        s = 2;
    if (s > 6)
        s = 6;
    if (s > (w - 8) / 300)                /* the 50 character panel must fit the width */
        s = (w - 8) / 300;
    if (s < 1)
        s = 1;
    /* One panel size for every page: the longest list, then DESC_ROWS rows of description and a
     * status row, so the window never changes size. */
    int list_rows = MAIN_COUNT;
    if (OPT_COUNT + 1 > list_rows)
        list_rows = OPT_COUNT + 1;
    if (P3(P3_COUNT) > list_rows)
        list_rows = P3(P3_COUNT);
    if (page_mods(7) + 1 > list_rows)
        list_rows = page_mods(7) + 1;
    if (12 > list_rows)
        list_rows = 12;
    /* The panel must fit the screen: a smaller font first, then a shorter list that scrolls with the selection. */
    const int chrome = 2 + 1 + DESC_ROWS + 1 + 2;     /* title, gaps, description, status */
    while (s > 1 && (list_rows + chrome) * 10 * s > h - 8 && (h - 8) / (10 * s) - chrome < 12)
        s--;
    int line = 10 * s, pw = 50 * 6 * s;
    int fit = (h - 8) / line - chrome;
    if (fit < 3)
        fit = 3;
    if (list_rows > fit)
        list_rows = fit;
    int ph = (list_rows + chrome) * line;
    if (pw > w - 8)
        pw = w - 8;
    int px = (w - pw) / 2, py = (h - ph) / 2;
    if (py < 4)
        py = 4;

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 245);
    SDL_Rect panel = {px, py, pw, ph};
    SDL_RenderFillRect(ren, &panel);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_RenderDrawRect(ren, &panel);

    char buf[96];
    int x = px + 2 * s * 3, y = py + line;
    SDL_SetRenderDrawColor(ren, 255, 210, 60, 255);
    font_text(ren, x, y, s, m->page == 0 ? "WWF WRESTLEMANIA - SETTINGS" : m->page == 2 ? "GAME OPTIONS - ENTER = ON/OFF" : m->page == 3 ? "MODS - FROM THE NEXT MATCH" : m->page == 7 ? "TWEAKS - ENTER = ON/OFF, LEFT/RIGHT = VALUE" : m->page == 4 ? "DISPLAY - ENTER = CHANGE" : m->page == 5 ? "VOLUME - LEFT/RIGHT, ENTER = 100%" : m->page == 6 ? "DEBUG - ENTER = CHANGE" : "CONTROLS - ENTER = CHANGE KEY");
    y += 2 * line;

    int n = page_lines(m), first = 0, vis = n < list_rows ? n : list_rows;
    if (n > vis) {                        /* scroll so the selected line stays visible */
        first = m->sel - vis / 2;
        if (first < 0)
            first = 0;
        if (first > n - vis)
            first = n - vis;
    }
    for (int i = first; i < n && i < first + vis; i++, y += line) {
        buf[0] = 0;
        if (m->page == 0) {
            switch (i) {
            case MAIN_FULL: snprintf(buf, sizeof buf, "FULL SCREEN  %s", *m->fullscreen ? "ON" : "OFF"); break;
            case MAIN_CONTROLS: snprintf(buf, sizeof buf, "CONTROLS"); break;
            case MAIN_DEBUG: snprintf(buf, sizeof buf, "DEBUG"); break;
            case MAIN_DISPLAY: snprintf(buf, sizeof buf, "DISPLAY"); break;
            case MAIN_GAME: snprintf(buf, sizeof buf, "GAME OPTIONS"); break;
            case MAIN_MODS: snprintf(buf, sizeof buf, "MODS"); break;
            case MAIN_TWEAKS: snprintf(buf, sizeof buf, "TWEAKS"); break;
            case MAIN_VOLUME: snprintf(buf, sizeof buf, "VOLUME"); break;
            case MAIN_SAVE: snprintf(buf, sizeof buf, "SAVE SETTINGS"); break;
            case MAIN_RESTART: snprintf(buf, sizeof buf, "APPLY AND RESTART"); break;
            default: snprintf(buf, sizeof buf, "CLOSE MENU"); break;
            }
        } else if (m->page == 4) {
            if (i == DSP_BACK)
                snprintf(buf, sizeof buf, "BACK");
            else if (i == DSP_APPLY)
                snprintf(buf, sizeof buf, "APPLY AND RESTART");
            else if (i == DSP_DYN_MIN || i == DSP_DYN_MAX)
                snprintf(buf, sizeof buf, "%-18s < %d%% >", dsp_names[i], *dsp_field((settings *)m->set, i));
            else if (i == DSP_SPEED)
                snprintf(buf, sizeof buf, "%-18s < %d%% >", dsp_names[i], m->set->speed);
            else if (i == DSP_RES) {
                char rt[24];
                settings_res_text(m->set, rt, sizeof rt);
                snprintf(buf, sizeof buf, "%-18s < %s >", dsp_names[i], rt);
            } else if (i == DSP_SCALE) {
                if (m->set->render_scale > 0)
                    snprintf(buf, sizeof buf, "%-18s < %dX >", dsp_names[i], m->set->render_scale);
                else
                    snprintf(buf, sizeof buf, "%-18s < AUTO >", dsp_names[i]);
            } else if (i == DSP_ART)
                snprintf(buf, sizeof buf, "%-18s %s", dsp_names[i], m->set->no_art ? "OFF" : "ON");
            else if (i == DSP_PRECACHE)
                snprintf(buf, sizeof buf, "%-18s %s%s", dsp_names[i], m->set->precache ? "ON" : "OFF",
                         m->precache_ok ? "" : "  (NOT HERE)");
            else if (i == DSP_LIMITS) {
                if (m->nlimits > 0)
                    snprintf(buf, sizeof buf, "%-18s %d, < %d OF %d >", dsp_names[i], m->nlimits, m->lim_sel + 1, m->nlimits);
                else
                    snprintf(buf, sizeof buf, "%-18s NONE", dsp_names[i]);
            } else
                snprintf(buf, sizeof buf, "%-18s %s", dsp_names[i], *dsp_field((settings *)m->set, i) ? "ON" : "OFF");
        } else if (m->page == 3) {
            int r = i - builtin_count();
            if (r == P3_APPLY)
                snprintf(buf, sizeof buf, "APPLY AND RESTART");
            else if (r == P3_BACK)
                snprintf(buf, sizeof buf, "BACK");
            else if (r == P3_SELFTEST)
                snprintf(buf, sizeof buf, "%-16s %s", "SKIP SELF TEST", m->set->skip_selftest ? "ON" : "OFF");
            else if (r == P3_FREE)
                snprintf(buf, sizeof buf, "%-16s %s", "FREE PLAY", m->set->free_play ? "ON" : "OFF");
            else {
                const wwf_mod *mod = page_mod(3, i);
                char num[48] = "";
                if (mod->arg_max > mod->arg_min) {
                    char vt[32];
                    mod_value_text(mod, mod_value(m, mod), vt, sizeof vt);
                    snprintf(num, sizeof num, " < %s >", vt);
                }
                snprintf(buf, sizeof buf, "%-13s %s%s", mod->name, mods_is_enabled(m->wolf, mod) ? "ON " : "OFF", num);
            }
        } else if (m->page == 7) {
            if (i >= page_mods(7)) {
                snprintf(buf, sizeof buf, "BACK");
            } else {
                const wwf_mod *mod = page_mod(7, i);
                char num[48] = "";
                if (mod->arg_max > mod->arg_min) {
                    char vt[32];
                    mod_value_text(mod, mod_value(m, mod), vt, sizeof vt);
                    snprintf(num, sizeof num, " < %s >", vt);
                }
                snprintf(buf, sizeof buf, "%-13s %s%s", mod->name, mods_is_enabled(m->wolf, mod) ? "ON " : "OFF", num);
            }
        } else if (m->page == 5) {
            static const char *const vn[VOL_COUNT] = {"VOLUME", "MUSIC", "EFFECTS", "CROWD", "BACK"};
            if (i == VOL_BACK)
                snprintf(buf, sizeof buf, "BACK");
            else
                snprintf(buf, sizeof buf, "%-13s< %d%% >", vn[i], m->set->vol[i]);
        } else if (m->page == 6) {
            if (i == DBG_BACK)
                snprintf(buf, sizeof buf, "BACK");
            else if (i == DBG_RESET)
                snprintf(buf, sizeof buf, "RESET KEYS AND BUTTONS");
            else if (i == DBG_LIMITS)
                snprintf(buf, sizeof buf, "%-18s %s", "LIMIT WARNINGS", m->set->limit_warnings ? "ON" : "OFF");
            else
                snprintf(buf, sizeof buf, "%-18s %s", "DEBUG INFO", m->set->debug_overlay ? "ON" : "OFF");
        } else if (m->page == 2) {
            if (i == OPT_COUNT)
                snprintf(buf, sizeof buf, "BACK");
            else
                snprintf(buf, sizeof buf, "%-26s %s", settings_option_name(opt_at(i)), settings_option_get(m->set, opt_at(i)) ? "ON" : "OFF");
        } else if (i == CTL_BACK) {
            snprintf(buf, sizeof buf, "BACK");
        } else if (i >= CTL_PAD) {
            int b = m->set->pad[ctl_order[i - CTL_PAD]];
            const char *bn = b >= 0 ? SDL_GameControllerGetStringForButton((SDL_GameControllerButton)b) : NULL;
            snprintf(buf, sizeof buf, "PAD %-12s %s", settings_base_name(ctl_order[i - CTL_PAD]),
                     (m->waiting == 2 && i == m->sel) ? "PRESS A BUTTON..." : bn ? bn : "NONE");
        } else {
            snprintf(buf, sizeof buf, "%-16s %s", settings_action_name(ctl_act(i)),
                     (m->waiting && i == m->sel) ? "PRESS A KEY..." : SDL_GetScancodeName(m->set->key[ctl_act(i)]));
        }
        if (i == m->sel) {
            SDL_SetRenderDrawColor(ren, 60, 60, 160, 255);
            SDL_Rect bar = {px + s * 2, y - s, pw - s * 4, line};
            SDL_RenderFillRect(ren, &bar);
        }
        SDL_SetRenderDrawColor(ren, i == m->sel ? 255 : 200, i == m->sel ? 255 : 200, i == m->sel ? 255 : 200, 255);
        font_text(ren, x, y, s, buf);
    }
    if (n > vis) {                        /* more lines above or below */
        int mx = px + pw - 3 * 6 * s, top = py + 3 * line;
        SDL_SetRenderDrawColor(ren, 255, 210, 60, 255);
        if (first > 0)
            font_text(ren, mx, top, s, "^");
        if (first + vis < n)
            font_text(ren, mx, top + (vis - 1) * line, s, "V");
    }
    {
        /* the description of the selected line: wrapped at word breaks into DESC_ROWS rows, cut with
         * "..." when it is longer (DESC_ROWS * width characters at most) */
        int maxc = (pw - 12 * s) / (6 * s);
        const char *hint = "UP/DOWN ENTER  LEFT/RIGHT  ESC BACK  F1 CLOSE";
        char rows[DESC_ROWS][128];

        if (m->page == 7 && m->sel < page_mods(7))
            hint = page_mod(7, m->sel)->description;
        else if (m->page == 7)
            hint = "THESE CAN BE SWITCHED AT ANY TIME, ALSO IN A MATCH. SAVE SETTINGS KEEPS WHAT IS ON FOR THE NEXT START.";
        else if (m->page == 2 && m->sel < OPT_COUNT)
            hint = settings_option_help(opt_at(m->sel));
        else if (m->page == 3 && m->sel < builtin_count())
            hint = page_mod(3, m->sel)->description;
        else if (m->page == 3 && m->sel < P3(P3_APPLY))
            hint = p3_help[m->sel - builtin_count()];
        else if (m->page == 3)
            hint = "SAVES EVERYTHING AND STARTS THE GAME AGAIN. THE MODS AND THE OPTIONS ABOVE ONLY TAKE EFFECT AT START.";
        else if (m->page == 4 && m->sel < DSP_COUNT) {
            hint = dsp_help[m->sel];
            if (m->sel == DSP_LIMITS)
                hint = m->nlimits > 0 ? m->limit[m->lim_sel] : "NOTHING IS LIMITED AT THE MOMENT.";
            else if (m->sel == DSP_PRECACHE && m->precache_note[0])
                hint = m->precache_note;
        }
        else if (m->page == 5 && m->sel == VOL_MASTER)
            hint = "THE OVERALL VOLUME, 0 TO 300 PERCENT. ABOVE 100 AMPLIFIES THE SOUND; THE PEAKS ARE LIMITED SOFTLY. ENTER = 100.";
        else if (m->page == 5 && m->sel < VOL_BACK)
            hint = "LEFT/RIGHT, 0 TO 100 PERCENT, ON TOP OF THE OVERALL VOLUME. ENTER = 100.";
        else if (m->page == 6 && m->sel == DBG_INFO)
            hint = "SHOWS FRAMES PER SECOND AND WHERE THE TIME GOES, THE BITMAP, THE GPU STATE, HD ART MEMORY AND, ON ANDROID, FREE MEMORY. TAKES EFFECT AT ONCE.";
        else if (m->page == 6 && m->sel == DBG_LIMITS)
            hint = "ANNOUNCES ON THE SCREEN WHEN THE GAME HAS HAD TO LIMIT SOMETHING FOR MEMORY OR THE DEVICE. THE LIST IS ALWAYS UNDER DISPLAY, LIMITS. TAKES EFFECT AT ONCE.";
        else if (m->page == 6 && m->sel == DBG_RESET)
            hint = "PUTS EVERY KEY AND EVERY CONTROLLER BUTTON BACK TO ITS DEFAULT. THE SETTINGS ARE SAVED WITH SAVE SETTINGS.";
        else if (m->page == 1 && m->sel >= CTL_PAD && m->sel < CTL_BACK)
            hint = "ENTER, THEN PRESS THE CONTROLLER BUTTON. THE LEFT STICK ALWAYS MOVES. THE SECOND, THIRD AND FOURTH CONTROLLER PLAY PLAYERS 2 TO 4 WITH THE SAME BUTTONS.";
        else if (m->page == 1 && m->sel < CTL_PAD)
            hint = m->sel >= 2 * ACT_PER_PLAYER && m->sel < ACT_COIN1
                       ? "ENTER, THEN PRESS THE NEW KEY. PLAYERS 3 AND 4 ARE FOR THE FOURPLAYER MOD (BUDDY MODE); THEIR START JOINS."
                       : "ENTER, THEN PRESS THE NEW KEY. ESC CANCELS.";
        wrap_text(hint, maxc, rows);
        SDL_SetRenderDrawColor(ren, 120, 220, 120, 255);
        y = py + (list_rows + 3 + 1) * line;
        for (int r = 0; r < DESC_ROWS; r++, y += line)
            font_text(ren, x, y, s, rows[r]);
        if (m->status[0]) {
            char last[128];
            snprintf(last, sizeof last, "%.*s", maxc < 127 ? maxc : 127, m->status);
            SDL_SetRenderDrawColor(ren, 255, 210, 60, 255);
            font_text(ren, x, y + line / 2, s, last);
        }
    }
}
