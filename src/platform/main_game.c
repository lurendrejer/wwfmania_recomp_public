/*
 * wwf - the recompiled game with an SDL2 window.
 *
 *   wwf [--gen DIR] [--img DIR] [--art DIR] [--scale N] [--cmos FILE]
 *       [--sound DIR] [--sound-art DIR] [--mute] [--classic] [--wide N]
 *       [--res WxH] [--zoom Z] [--min-zoom Z] [--config FILE]
 *       [--mod NAME[=N]]... [--free-play] [--list-mods] [--inspect] [--gpu] [--async-compute]
 *       [--sync-art] [--art-threads N] [--art-prefetch-mb N] [--art-budget-mb N]
 *
 * Defaults: --gen build/gen, --img orig/IMG, --cmos wwf.cmos, --scale 3,
 * --sound sounds (made by tools/dcs/dcsrip from the sound ROMs; without it
 * the game runs silent).
 *
 * Keys (player 1 / player 2):
 *   move        arrows        / I J K L
 *   punch       A             / G
 *   block       S             / H
 *   super punch D             / ;
 *   kick        F             / '
 *   super kick  R             / P
 *   run         Space         / \   (punch + kick together)
 *   start       1             / 2
 *   coin        5             / 6
 *   (all of these can be changed in the F1 menu and saved to wwf.cfg)
 *   test F2, service F3, F11 full screen, F1 menu, Esc quits (CMOS is saved)
 *   F6 the image inspector (with --inspect): freezes the game on the last few seconds, with every image boxed
 *   and named; arrows step and select, C copies the name (platform/inspect.h)
 *   zoom        mouse wheel, + / -   (0 resets)
 *
 * The view follows the window's shape (the --res one, or else the desktop's) and
 * shows as much as fits it: up to 56 extra pixels on each side of the 400 pixel
 * screen, then extra rows above and below (a 16:9 window: 512 x 288). The game
 * is told to draw that far (its cull limits are widened) and the camera is kept
 * off the ends of the level, so there is less scrolling and no black.
 * --classic keeps the original 400x254 view and the game's own limits.
 * Three separate settings: --res is the window's size, --scale the render scale
 * (framebuffer pixels per game pixel: how sharp the art can be; by default what
 * makes one window pixel about one framebuffer pixel) and --zoom (1 to 4, also
 * the wheel and +/-) how much of the view is shown, magnified from the middle.
 * --res WxH opens the window at that size (e.g. 1920x1080), and plans the view
 * for that shape; wider than 512 x 254 widens the bitmap. The render scale
 * follows the height unless --scale is given.
 * --wide N (test) widens the bitmap by N pixels per side, up to 56 + N shown;
 * where the level ends it shows black.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
#include <string.h>

#include "platform/font.h"
#include "platform/gamepad.h"
#include "platform/gfx_async.h"
#include "platform/gpu_video.h"
#include "platform/inspect.h"
#include "platform/menu.h"
#include "platform/sdl_audio.h"
#include "platform/settings.h"
#include "platform/sdl_video.h"
#include "util/dataextract.h"
#include "util/fsutil.h"
#include "wolf/replay.h"
#include "wolf/wolf.h"

static void dir_noop(const char *name, void *user)
{
    (void)name;
    (void)user;
}

#ifdef __ANDROID__
/* MemAvailable of /proc/meminfo in MB, or -1: what the system could give a process without killing one. */
static long mem_available_mb(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    char line[128];
    long kb = -1;
    if (!f)
        return -1;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "MemAvailable: %ld kB", &kb) == 1)
            break;
    fclose(f);
    return kb < 0 ? -1 : kb / 1024;
}
#endif

#ifdef __ANDROID__
/* A test switch: an empty file `name` in the app's internal storage (next to wwf.cfg) or in its external files
 * folder, which adb push can reach (Android/data/<id>/files). */
static int android_flag(const char *name)
{
    const char *dirs[2] = {SDL_AndroidGetInternalStoragePath(), SDL_AndroidGetExternalStoragePath()};
    for (int i = 0; i < 2; i++) {
        char path[1200];
        if (!dirs[i])
            continue;
        snprintf(path, sizeof path, "%s/%s", dirs[i], name);
        if (fs_file_exists(path))
            return 1;
    }
    return 0;
}
#endif

#ifdef __ANDROID__
#define ALOG(...) SDL_Log("WWF: " __VA_ARGS__)
#else
#define ALOG(...) ((void)0)
#endif

#ifdef __ANDROID__
/* The bundle is the app's assets (docs/ANDROID.md): SDL opens them by relative path. */
static uint8_t *asset_read(void *user, const char *rel, size_t *n)
{
    (void)user;
    SDL_RWops *rw = SDL_RWFromFile(rel, "rb");
    if (!rw)
        return NULL;
    Sint64 len = SDL_RWsize(rw);
    uint8_t *buf = len >= 0 ? malloc(len ? (size_t)len : 1) : NULL;
    if (buf && SDL_RWread(rw, buf, 1, (size_t)len) != (size_t)len) {
        free(buf);
        buf = NULL;
    }
    SDL_RWclose(rw);
    if (buf)
        *n = (size_t)len;
    return buf;
}


/* Where the game finds its files on Android: the bundled data copied to the app's storage (once per version), or,
 * when nothing was bundled, the app's external files directory (Android/data/<id>/files, filled by the player).
 * The settings and the CMOS live in the internal storage. */
/* The copy of the bundled data can take a minute (or much longer with a lot of art): a window with a bar that fills, so
 * that it does not look like a black screen that has stopped. */
typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    Uint32 last;
} copy_bar;

static void copy_progress(void *user, int done, int total)
{
    copy_bar *b = user;
    Uint32 now = SDL_GetTicks();

    if (done % 25 == 0 || done == total)
        SDL_Log("bundled data: %d of %d files", done, total);
    if (!b->ren || (now - b->last < 100 && done != total))
        return;
    b->last = now;
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(b->ren, &w, &h);
    SDL_SetRenderDrawColor(b->ren, 10, 15, 60, 255);
    SDL_RenderClear(b->ren);
    SDL_Rect frame = {w / 10, h / 2 - h / 24, w * 8 / 10, h / 12};
    SDL_SetRenderDrawColor(b->ren, 240, 200, 60, 255);
    SDL_RenderDrawRect(b->ren, &frame);
    SDL_Rect fill = {frame.x + 4, frame.y + 4, total > 0 ? (frame.w - 8) * done / total : 0, frame.h - 8};
    SDL_SetRenderDrawColor(b->ren, 200, 30, 40, 255);
    SDL_RenderFillRect(b->ren, &fill);
    SDL_RenderPresent(b->ren);
    SDL_PumpEvents();
}

static void android_paths(char *gen, size_t gen_n, char *img, size_t img_n, char *snd, size_t snd_n, char *cmos,
                          size_t cmos_n, char *cfg, size_t cfg_n, char *art, size_t art_n)
{
    const char *base = SDL_AndroidGetInternalStoragePath();
    char root[512], err[300] = "";
    copy_bar bar = {NULL, NULL, 0};
    data_source src = {&bar, asset_read, copy_progress};
    int copied = 0;

    if (!base)
        return;
    if (SDL_Init(SDL_INIT_VIDEO) == 0) {
        bar.win = SDL_CreateWindow("WWF WrestleMania", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0,
                                   SDL_WINDOW_FULLSCREEN_DESKTOP);
        bar.ren = bar.win ? SDL_CreateRenderer(bar.win, -1, 0) : NULL;
    }
    SDL_Log("WWF: internal storage %s", base);
    snprintf(root, sizeof root, "%s/data", base);
    int r = data_extract(&src, root, &copied, err, sizeof err);
    if (r < 0) {
        SDL_Log("WWF: bundled data: %s", err);
    } else if (r == 0) {
        const char *ext = SDL_AndroidGetExternalStoragePath();
        snprintf(root, sizeof root, "%s", ext ? ext : base);
    } else if (r == 2) {
        printf("bundled data: %d files copied to %s\n", copied, root);
    }
    if (bar.ren)
        SDL_DestroyRenderer(bar.ren);
    sdl_video_use_window(bar.win);         /* the game's window is this one: Android has room for one window */
    SDL_Log("WWF: data root %s (extract result %d, %d files copied)", root, r, copied);
    snprintf(gen, gen_n, "%s/gen", root);
    snprintf(img, img_n, "%s/orig/IMG", root);
    snprintf(snd, snd_n, "%s/sounds", root);
    /* the high-resolution art (what --art gives on the desktop): the folder art/hd, else the file art/hd.zip, else
     * the folder art, when it is there (a folder may hold loose PNGs and zips, docs/ASSET_OVERRIDES.md) */
    art[0] = 0;
    snprintf(art, art_n, "%s/art/hd", root);
    if (fs_list_dir(art, dir_noop, NULL) < 0) {
        snprintf(art, art_n, "%s/art/hd.zip", root);
        if (!fs_file_exists(art)) {
            snprintf(art, art_n, "%s/art", root);
            if (fs_list_dir(art, dir_noop, NULL) < 0)
                art[0] = 0;
        }
    }
    /* test switch: an empty file "noart" next to wwf.cfg turns the HD art off */
    if (android_flag("noart")) {
        SDL_Log("WWF: noart file found, HD art is off");
        art[0] = 0;
    }
    snprintf(cmos, cmos_n, "%s/wwf.cmos", base);
    snprintf(cfg, cfg_n, "%s/wwf.cfg", base);
}
#endif

static void quiet_warn(const char *msg, void *user)
{
    (void)user;
#ifdef __ANDROID__
    if (strncmp(msg, "duplicate image ", 16) != 0)
        SDL_Log("WWF: %s", msg);
    return;
#endif
    if (strncmp(msg, "duplicate image ", 16) != 0)
        fprintf(stderr, "warning: %s\n", msg);
}

static void read_keys(wolf *w, const settings *st)
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    /* Wiring (orig/WRESTLE2.ASM get_but_val_cur, MAIN.ASM): the stick and the
     * first three buttons (punch, block, super punch) are bits 0-3 and 4-6 of
     * each player's byte of SWITCH; kick and super kick are SWITCH2 bits 0/1
     * (player 1) and 4/5 (player 2). */
    uint8_t p[2] = {0, 0}, sw2 = 0;
    uint16_t c = 0;
    for (int pl = 0; pl < 2; pl++) {
        const SDL_Scancode *key = st->key + pl * ACT_PER_PLAYER;
        int run = k[key[ACT_RUN]] != 0;                  /* punch + kick together */
        if (k[key[ACT_UP]]) p[pl] |= WOLF_UP;
        if (k[key[ACT_DOWN]]) p[pl] |= WOLF_DOWN;
        if (k[key[ACT_LEFT]]) p[pl] |= WOLF_LEFT;
        if (k[key[ACT_RIGHT]]) p[pl] |= WOLF_RIGHT;
        if (run || k[key[ACT_PUNCH]]) p[pl] |= WOLF_B1;
        if (k[key[ACT_BLOCK]]) p[pl] |= WOLF_B2;
        if (k[key[ACT_SPUNCH]]) p[pl] |= WOLF_B3;
        if (run || k[key[ACT_KICK]]) sw2 |= (uint8_t)(1 << (4 * pl));
        if (k[key[ACT_SKICK]]) sw2 |= (uint8_t)(2 << (4 * pl));
        if (k[key[ACT_START]]) c |= pl ? WOLF_START2 : WOLF_START1;
    }
    w->player[0] = p[0];
    w->player[1] = p[1];
    w->player[2] = sw2;
    /* Players 3 and 4 exist only for mods (mods/fourplayer): their own word, wolf.h extra_player. */
    for (int pl = 0; pl < 2; pl++) {
        const SDL_Scancode *key = st->key + (2 + pl) * ACT_PER_PLAYER;
        int run = k[key[ACT_RUN]] != 0;
        unsigned v = 0;
        if (k[key[ACT_UP]]) v |= WOLF_UP;
        if (k[key[ACT_DOWN]]) v |= WOLF_DOWN;
        if (k[key[ACT_LEFT]]) v |= WOLF_LEFT;
        if (k[key[ACT_RIGHT]]) v |= WOLF_RIGHT;
        if (run || k[key[ACT_PUNCH]]) v |= WOLF_X_PUNCH;
        if (k[key[ACT_BLOCK]]) v |= WOLF_X_BLOCK;
        if (k[key[ACT_SPUNCH]]) v |= WOLF_X_SPUNCH;
        if (run || k[key[ACT_KICK]]) v |= WOLF_X_KICK;
        if (k[key[ACT_SKICK]]) v |= WOLF_X_SKICK;
        if (k[key[ACT_START]]) v |= WOLF_X_START;
        w->extra_player[pl] = (uint16_t)v;
    }
    if (k[st->key[ACT_COIN1]]) c |= WOLF_COIN1;
    if (k[st->key[ACT_COIN2]]) c |= WOLF_COIN2;
    if (k[st->key[ACT_TEST]]) c |= WOLF_TEST;
    if (k[st->key[ACT_SERVICE]]) c |= WOLF_SERVICE;
    w->coin_bits = c;
}

/* Dynamic zoom: in a match the zoom follows how far apart the wrestlers are: it zooms out until they, with some
 * room around them, fit, at most to the settings' dyn_min percent, and back in when they come together, at
 * most to dyn_max percent. It zooms out quickly and in slowly, and is off outside a match. */
static void dynamic_zoom(sdl_video *sv, const wolf *w, const settings *st, int on)
{
    if (!on || !w->in_match) {
        sv->zoom_dyn = 0;
        return;
    }
    double lo_z = st->dyn_min / 100.0, hi_z = st->dyn_max / 100.0;
    double target = hi_z;
    int lo, hi;
    if (wolf_wrestler_extent(w, &lo, &hi) > 1) {
        int ww = 1, wh = 1;
        SDL_GetWindowSize(sv->win, &ww, &wh);
        double a = wh > 0 ? (double)ww / wh : 16.0 / 9;
        double base_h = sv->base_h > 0 ? sv->base_h : 254;
        double need = (double)(hi - lo) + 2 * 110;        /* the wrestlers are wider than their x, and the HUD */
        target = base_h * a / need;
    }
    target = target < lo_z ? lo_z : target > hi_z ? hi_z : target;
    double z = sv->zoom_dyn > 0 ? sv->zoom_dyn : (sv->zoom > 0 ? sv->zoom : 1.0);
    z += (target - z) * (target < z ? 0.10 : 0.03);
    sv->zoom_dyn = z;
}

/* What the game had to limit on this device (memory, the bitmap, the GPU): listed in the F1 menu (DISPLAY >
 * LIMITS) and announced by a line at the bottom of the screen for a few seconds. The static ones are known at start. */
static char lim_static[6][160];
static int nlim_static;
static char banner_text[160];
static int banner_frames;
static int mem_level;          /* Android: 0 = enough memory, 1 = low, 2 = very low (set where the art is evicted) */
static long mem_budget_mb;

static void lim_add(const char *fmt, ...)
{
    va_list ap;
    if (nlim_static >= 6)
        return;
    va_start(ap, fmt);
    vsnprintf(lim_static[nlim_static++], sizeof lim_static[0], fmt, ap);
    va_end(ap);
}

/* The debug info of the F1 menu (DEBUG INFO): a few lines of text, made every half second in the main loop. */
#define DBG_LINES 6
static char dbg_text[DBG_LINES][120];

typedef struct {
    menu *mn;
    inspector *in;
    const settings *set;
} overlays;

/* The precache's progress, in the top left corner while the art is read into memory and, with GPU drawing, uploaded
 * to the graphics memory (docs/OPTIONS.md, PRECACHE). The game runs meanwhile; this says why it can be slow. */
static struct {
    int on;                    /* still working */
    int ready_frames;          /* frames left of the "ready" message */
    int gpu;                   /* the GPU makes textures too */
    int total, loaded, on_gpu;
} precache_ui;

static int draw_debug(SDL_Renderer *ren, int w, int h)
{
    int s = h / 300, lines = 0, chars = 0;
    if (s < 1)
        s = 1;
    if (s > 3)
        s = 3;
    for (int i = 0; i < DBG_LINES; i++)
        if (dbg_text[i][0]) {
            lines = i + 1;
            if ((int)strlen(dbg_text[i]) > chars)
                chars = (int)strlen(dbg_text[i]);
        }
    if (!lines)
        return 0;
    SDL_Rect box = {4 * s, 4 * s, (chars * 6 + 4) * s, (lines * 10 + 2) * s};
    (void)w;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 190);
    SDL_RenderFillRect(ren, &box);
    SDL_SetRenderDrawColor(ren, 120, 255, 120, 255);
    for (int i = 0; i < lines; i++)
        font_text(ren, box.x + 2 * s, box.y + (1 + i * 10) * s, s, dbg_text[i]);
    return box.y + box.h;
}

static void draw_precache(SDL_Renderer *ren, int w, int h, int y0)
{
    char l1[80], l2[80];
    int s = h / 300;
    (void)w;
    if (!precache_ui.on && precache_ui.ready_frames <= 0)
        return;
    s = s < 1 ? 1 : s > 3 ? 3 : s;
    const int total = precache_ui.total > 0 ? precache_ui.total : 1;
    double frac;
    if (precache_ui.on) {
        snprintf(l1, sizeof l1, "PRECACHING HD ART: READ %d OF %d", precache_ui.loaded, precache_ui.total);
        if (precache_ui.gpu)
            snprintf(l2, sizeof l2, "ON THE GPU %d OF %d", precache_ui.on_gpu, precache_ui.total);
        else
            l2[0] = 0;
        frac = precache_ui.gpu ? (double)(precache_ui.loaded + precache_ui.on_gpu) / (2.0 * total)
                               : (double)precache_ui.loaded / total;
    } else {
        snprintf(l1, sizeof l1, "HD ART READY");
        snprintf(l2, sizeof l2, "%d IMAGES%s", precache_ui.loaded, precache_ui.gpu ? " ON THE GPU" : " IN MEMORY");
        frac = 1.0;
    }
    frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
    int chars = (int)strlen(l1) > (int)strlen(l2) ? (int)strlen(l1) : (int)strlen(l2);
    int lines = l2[0] ? 2 : 1;
    SDL_Rect box = {4 * s, y0 ? y0 + 2 * s : 4 * s, (chars * 6 + 4) * s, (lines * 10 + 8) * s};
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 190);
    SDL_RenderFillRect(ren, &box);
    SDL_SetRenderDrawColor(ren, 255, 210, 60, 255);
    font_text(ren, box.x + 2 * s, box.y + 1 * s, s, l1);
    if (l2[0])
        font_text(ren, box.x + 2 * s, box.y + 11 * s, s, l2);
    SDL_Rect bar = {box.x + 2 * s, box.y + box.h - 5 * s, box.w - 4 * s, 3 * s};   /* the progress bar */
    SDL_SetRenderDrawColor(ren, 70, 70, 70, 255);
    SDL_RenderFillRect(ren, &bar);
    bar.w = (int)(bar.w * frac);
    SDL_SetRenderDrawColor(ren, 255, 210, 60, 255);
    SDL_RenderFillRect(ren, &bar);
}

static void draw_overlays(SDL_Renderer *ren, int w, int h, void *user)
{
    overlays *o = user;
    inspect_draw(o->in, ren, w, h);
    int y0 = 0;
    if (o->set && o->set->debug_overlay)
        y0 = draw_debug(ren, w, h);
    draw_precache(ren, w, h, y0);
    if (banner_frames > 0 && banner_text[0]) {
        int s = h / 300, n = (int)strlen(banner_text);
        s = s < 1 ? 1 : s > 3 ? 3 : s;
        SDL_Rect box = {(w - (n * 6 + 6) * s) / 2, h - 16 * s, (n * 6 + 6) * s, 12 * s};
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 200);
        SDL_RenderFillRect(ren, &box);
        SDL_SetRenderDrawColor(ren, 255, 210, 60, 255);
        font_text(ren, box.x + 3 * s, box.y + 2 * s, s, banner_text);
    }
    menu_draw(o->mn, ren, w, h);
}

int main(int argc, char **argv)
{
    const char *gen = "build/gen", *img = "orig/IMG", *art = NULL, *cmos = "wwf.cmos";
    const char *sound_dir = "sounds", *sound_art = NULL;
    int free_play_cli = 0;
    int inspect = 0, gpu_cli = 0, async_cli = 0;
    int scale = 3, mute = 0, classic = 0, nmods = 0, wide = 0, res_w = 0, res_h = 0, scale_given = 0;
    double zoom0 = 1.0, min_zoom = 0.5;
    int min_zoom_given = 0;
    int zoom_given = 0;
    const char *cfg_path = "wwf.cfg";
    /* test aids: scripted inputs (as wwfrun --input) and a screenshot of one frame */
    struct { long frame, len; int what; unsigned bits; } inputs[64];
    int ninputs = 0;
    long shot_frame = -1;
    int shot_menu = -1, shot_sel = 0;      /* --shot-menu PAGE SEL: open the menu on that page for the shot */
    int shot_inspect = -1;                 /* --shot-inspect BACK: the image inspector, BACK frames back, for the shot */
    const char *shot_file = NULL;
    const char *mods[WWF_MAX_MODS];
    /* HD art is read on worker threads (platform/gfx_async.h); --sync-art reads it on the game thread, as before */
    int sync_art = 0, art_threads = 2;
    long prefetch_mb = 256, settle_from = -1;   /* settle_from: test aid, wait for the loads from this frame on */
    /* the decoded HD art (and the GPU's copy of it) is kept within this many MB: the least recently drawn images are freed
     * and read again when they are needed (gfx_cache_evict). The Shield has 3 GB, and a Royal Rumble used it all. */
    long art_budget_mb = 2048;
    long art_evicted = 0;
    (void)art_evicted;           /* only the Android log reads it */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--gen") && i + 1 < argc) gen = argv[++i];
        else if (!strcmp(argv[i], "--img") && i + 1 < argc) img = argv[++i];
        else if (!strcmp(argv[i], "--art") && i + 1 < argc) art = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) { scale = atoi(argv[++i]); scale_given = 1; }
        else if (!strcmp(argv[i], "--zoom") && i + 1 < argc) {
            zoom0 = atof(argv[++i]);
            zoom_given = 1;
            if (zoom0 < 0.25 || zoom0 > 4.0) {
                fprintf(stderr, "--zoom expects 0.25 to 4\n");
                return 2;
            }
        }
        else if (!strcmp(argv[i], "--input") && i + 1 < argc && ninputs < 64) {
            long fr, len;
            char what[16];
            unsigned bits = 0;
            if (sscanf(argv[++i], "%ld,%ld,%15[^,],%u", &fr, &len, what, &bits) == 4) {
                inputs[ninputs].frame = fr;
                inputs[ninputs].len = len;
                inputs[ninputs].what = !strcmp(what, "coins") ? 4 : what[0] == 'p' ? what[1] - '1' : what[0] == 'x' ? 3 + (what[1] - '0') : 0;
                inputs[ninputs].bits = bits;
                ninputs++;
            }
        }
        else if (!strcmp(argv[i], "--shot-inspect") && i + 1 < argc) shot_inspect = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot-menu") && i + 2 < argc) { shot_menu = atoi(argv[i + 1]); shot_sel = atoi(argv[i + 2]); i += 2; }
        else if (!strcmp(argv[i], "--shot") && i + 2 < argc) { shot_frame = atol(argv[i + 1]); shot_file = argv[i + 2]; i += 2; }
        else if (!strcmp(argv[i], "--config") && i + 1 < argc) cfg_path = argv[++i];
        else if (!strcmp(argv[i], "--min-zoom") && i + 1 < argc) {
            min_zoom = atof(argv[++i]);
            min_zoom_given = 1;
            if (min_zoom < 0.1 || min_zoom > 1.0) {
                fprintf(stderr, "--min-zoom expects 0.1 to 1\n");
                return 2;
            }
        }
        else if (!strcmp(argv[i], "--res") && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &res_w, &res_h) != 2 || res_w < 200 || res_h < 100 ||
                res_w > 16384 || res_h > 16384) {
                fprintf(stderr, "--res expects WIDTHxHEIGHT, e.g. 1920x1080\n");
                return 2;
            }
        }
        else if (!strcmp(argv[i], "--cmos") && i + 1 < argc) cmos = argv[++i];
        else if (!strcmp(argv[i], "--sound") && i + 1 < argc) sound_dir = argv[++i];
        else if (!strcmp(argv[i], "--sound-art") && i + 1 < argc) sound_art = argv[++i];
        else if (!strcmp(argv[i], "--mute")) mute = 1;
        else if (!strcmp(argv[i], "--free-play")) free_play_cli = 1;
        else if (!strcmp(argv[i], "--classic")) classic = 1;
        else if (!strcmp(argv[i], "--inspect")) inspect = 1;
        else if (!strcmp(argv[i], "--gpu")) gpu_cli = 1;
        else if (!strcmp(argv[i], "--async-compute")) async_cli = 1;
        else if (!strcmp(argv[i], "--sync-art")) sync_art = 1;
        else if (!strcmp(argv[i], "--art-threads") && i + 1 < argc) art_threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--art-prefetch-mb") && i + 1 < argc) prefetch_mb = atol(argv[++i]);
        else if (!strcmp(argv[i], "--art-budget-mb") && i + 1 < argc) art_budget_mb = atol(argv[++i]);
        else if (!strcmp(argv[i], "--art-settle") && i + 1 < argc) settle_from = atol(argv[++i]);
        else if (!strcmp(argv[i], "--wide") && i + 1 < argc) wide = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mod") && i + 1 < argc && nmods < WWF_MAX_MODS) mods[nmods++] = argv[++i];
        else if (!strcmp(argv[i], "--list-mods")) {
            for (const wwf_mod *const *m = mods_builtin(); *m; m++)
                printf("%-12s %s\n", (*m)->name, (*m)->description);
            return 0;
        }
        else {
            fprintf(stderr, "usage: wwf [--gen DIR] [--img DIR] [--art DIR] [--scale N] [--cmos FILE]\n"
                            "           [--sound DIR] [--sound-art DIR] [--mute] [--classic] [--wide N]\n"
                            "           [--res WxH] [--zoom Z] [--min-zoom Z]\n"
                            "           [--config FILE]\n"
                            "           [--mod NAME[=N]]... [--free-play] [--list-mods] [--gpu] [--async-compute]\n"
                            "           [--sync-art] [--art-threads N] [--art-prefetch-mb N] [--art-budget-mb N]\n");
            return 2;
        }
    }

#ifdef __ANDROID__
    {
        static char a_gen[600], a_img[600], a_snd[600], a_cmos[600], a_cfg[600], a_art[600];
        android_paths(a_gen, sizeof a_gen, a_img, sizeof a_img, a_snd, sizeof a_snd, a_cmos, sizeof a_cmos, a_cfg,
                      sizeof a_cfg, a_art, sizeof a_art);
        SDL_setenv("WWF_TRACE", "1", 1);
        if (a_gen[0]) {
            gen = a_gen;
            img = a_img;
            sound_dir = a_snd;
            art = a_art[0] ? a_art : NULL;
            cmos = a_cmos;
            cfg_path = a_cfg;
        }
    }
#endif
    static settings set;
    settings_defaults(&set);
    settings_load(&set, cfg_path);
    if (set.no_art)
        art = NULL;
    if (free_play_cli)
        set.free_play = 1;
    if (gpu_cli)
        set.gpu = 1;
    if (async_cli)
        set.async_compute = 1;
#ifdef __ANDROID__
    if (android_flag("gpu")) {   /* test switch: an empty file "gpu" turns the GPU drawing on (docs/ANDROID.md) */
        SDL_Log("WWF: gpu file found, GPU drawing is on");
        set.gpu = 1;
    }
    if (android_flag("asynccompute")) {   /* test switch: an empty file "asynccompute" turns async compute on (docs/ANDROID.md) */
        SDL_Log("WWF: asynccompute file found, async compute is on");
        set.async_compute = 1;
    }
#endif
    if (!zoom_given && set.zoom > 0)
        zoom0 = set.zoom;
    /* The window size: --res, else the one from the F1 menu (0 = the desktop's shape). Either way the
     * settings then hold what is running, so the menu shows it and saving keeps it. */
    if (res_h > 0) {
        set.res_w = res_w;
        set.res_h = res_h;
    } else if (set.res_h > 0) {
        res_w = set.res_w;
        res_h = set.res_h;
    }

    ALOG("settings read from %s", cfg_path);
    static wolf w;

    /* Wide view. The game culls what it draws against SCRNTL/SCRNLR, 32 pixels
     * beyond each side of the screen; the bitmap has 56 (and floor and ring
     * reach that far), so the margin is widened. For a window shape that needs
     * more, the bitmap gets more columns, and more rows above and below the
     * two display pages (wolf_set_extra_size). The shape is the --res one, or
     * else the desktop's, which is what full screen (F11) will have. */
    int pad_x = 56, pad_y = 0;
    if (!classic) {
        double aspect = 16.0 / 9;
        if (res_h > 0) {
            aspect = (double)res_w / res_h;
        } else if (SDL_Init(SDL_INIT_VIDEO) == 0) {
            SDL_DisplayMode dm;
            if (SDL_GetDesktopDisplayMode(0, &dm) == 0 && dm.w > 0 && dm.h > 0)
                aspect = (double)dm.w / dm.h;
        }
        /* 4:3 pixels are narrower than tall: the same window shows more columns */
        if (set.crt_aspect)
            aspect *= ((double)VIDEO_W / VIDEO_H) / (4.0 / 3.0);
        /* up to the game's 512 columns, the rest of the shape goes to rows;
         * beyond that (wider than 512 x 254) to columns */
        if (aspect >= 512.0 / VIDEO_H) {
            pad_x = ((int)(VIDEO_H * aspect + 0.5) - VIDEO_W + 1) / 2;
        } else {
            pad_y = ((int)(512.0 / aspect + 0.5) - VIDEO_H + 1) / 2;
        }
        if (pad_x < 56 + wide)
            pad_x = 56 + wide;
        if (pad_y > 120)
            pad_y = 120;
    }
    const int base_pad_x = pad_x, base_pad_y = pad_y;      /* the view at zoom 1 */
#ifdef __ANDROID__
    /* a TV has no mouse wheel to zoom out with: the bitmap need not be four times the picture (min zoom 0.5); the
     * dynamic zoom, when it is on, still widens it just below */
    if (!min_zoom_given)
        min_zoom = 1.0;
#else
    (void)min_zoom_given;
#endif
    double zmin = classic ? 1.0 : min_zoom;
    /* the dynamic zoom needs a view as wide as its farthest-out limit: the bitmap is sized for it now */
    if (!classic && set.dyn_zoom && set.dyn_min / 100.0 < zmin)
        zmin = set.dyn_min / 100.0;
    if (zoom0 < zmin)
        zoom0 = zmin;
    if (zmin < 1.0) {
        /* render enough to fill the window at the smallest zoom too */
        pad_x = (int)(((VIDEO_W + 2 * base_pad_x) / zmin - VIDEO_W + 1) / 2);
        pad_y = (int)(((VIDEO_H + 2 * base_pad_y) / zmin - VIDEO_H + 1) / 2);
        if (pad_y > 1400)
            pad_y = 1400;
    }

    /* Three separate settings: the window's resolution (--res), the render
     * scale (--scale: framebuffer pixels per game pixel, i.e. how sharp the
     * art can be) and the zoom (--zoom, and the wheel: how much of the view is
     * shown). Unless given, the scale is what makes one window pixel about one
     * framebuffer pixel at the initial zoom. */
    if (!scale_given && set.render_scale > 0) {
        scale = set.render_scale;
        scale_given = 1;
    }
    if (res_h > 0 && !scale_given) {
        scale = (int)(res_h * zoom0 / (VIDEO_H + 2 * base_pad_y) + 0.999);
        if (scale < 1)
            scale = 1;
        if (scale > 8)
            scale = 8;
    }
    const int scale_wanted = scale;
    /* the bitmap is (512 + 2 * extra) * scale by 2 * (256 + 2 * pad_y) * scale entries. The render scale is only lowered
     * when it would not fit: up to a quarter of the RAM at about 14 bytes a pixel (the CPU's picture, its detail words,
     * the GPU's two planes), never less than the 160 Mpixel this always allowed. (The Android build has its own, much
     * smaller limit just below.) */
    double max_bitmap_px = 160e6;
    {
        int ram = SDL_GetSystemRAM();
        if (ram > 0 && (double)ram * 1048576.0 * 0.25 / 14.0 > max_bitmap_px)
            max_bitmap_px = (double)ram * 1048576.0 * 0.25 / 14.0;
    }
    while (scale > 1 && (double)(VIDEO_W + 112 + 2 * (pad_x - 56)) * scale * 2 * (256 + 2 * pad_y) * scale > max_bitmap_px)
        scale--;
    /* The picture is one texture, and a texture has a largest size (16384 on macOS, Windows and most Linux drivers; the
     * program used to stop with "Texture dimensions are limited to 16384x16384" at a render scale of 4 with the dynamic
     * zoom going 10% out). The zoom-out limit gives way first, then the render scale. */
    {
        const int max_dim = 16384;
        double want = zmin;
        while (zmin < 1.0 && ((VIDEO_W + 2 * pad_x) * scale > max_dim || (VIDEO_H + 2 * pad_y) * scale > max_dim)) {
            zmin += 0.02;
            if (zmin > 1.0)
                zmin = 1.0;
            pad_x = (int)(((VIDEO_W + 2 * base_pad_x) / zmin - VIDEO_W + 1) / 2);
            pad_y = (int)(((VIDEO_H + 2 * base_pad_y) / zmin - VIDEO_H + 1) / 2);
            if (pad_y > 1400)
                pad_y = 1400;
        }
        if (zmin > want) {
            lim_add("THE PICTURE TEXTURE CANNOT BE LARGER THAN %d PIXELS: ZOOM-OUT LIMIT %d%% INSTEAD OF %d%% AT SCALE %d.",
                    max_dim, (int)(zmin * 100 + 0.5), (int)(want * 100 + 0.5), scale);
            if (zoom0 < zmin)
                zoom0 = zmin;
        }
        while (scale > 1 && ((VIDEO_W + 2 * pad_x) * scale > max_dim || (VIDEO_H + 2 * pad_y) * scale > max_dim))
            scale--;
    }
#ifdef __ANDROID__
    /* the TV's CPU renders the bitmap in software: keep it small */
    if (!scale_given && scale > 1)
        scale = 1;
    /* The bitmap is what the GPU path keeps as textures (about 8 bytes a pixel, more with the CPU's copy): a farthest-out
     * dynamic zoom of 10% asked for 10680 x 10168 pixels, 1.2 GB of GPU memory on a 3 GB box, and the system killed the
     * game. Keep it to about 16 million pixels: the zoom-out limit gives way first (it is a convenience), the scale
     * only when even the normal view is too big for it. */
    {
        const double max_px = 16e6;
        double want = zmin;
        while (zmin < 1.0 && (double)(VIDEO_W + 112 + 2 * (pad_x - 56)) * scale * 2 * (256 + 2 * pad_y) * scale > max_px) {
            zmin += 0.02;
            if (zmin > 1.0)
                zmin = 1.0;
            pad_x = (int)(((VIDEO_W + 2 * base_pad_x) / zmin - VIDEO_W + 1) / 2);
            pad_y = (int)(((VIDEO_H + 2 * base_pad_y) / zmin - VIDEO_H + 1) / 2);
            if (pad_y > 1400)
                pad_y = 1400;
        }
        if (zmin > want)
            lim_add("BITMAP CAPPED AT 16 MPIXEL FOR THIS DEVICE: ZOOM-OUT LIMIT %d%% INSTEAD OF %d%% AT SCALE %d.",
                    (int)(zmin * 100 + 0.5), (int)(want * 100 + 0.5), scale);
        if (zmin > want) {
            ALOG("bitmap limited to 16 Mpixel for this device: zoom-out limit %d%% instead of %d%% (at scale %d)",
                 (int)(zmin * 100 + 0.5), (int)(want * 100 + 0.5), scale);
            if (zoom0 < zmin)
                zoom0 = zmin;
        }
        while (scale > 1 && (double)(VIDEO_W + 112 + 2 * (pad_x - 56)) * scale * 2 * (256 + 2 * pad_y) * scale > max_px)
            scale--;
    }
#endif
    if (scale_given && scale < scale_wanted)
        lim_add("RENDER SCALE LOWERED FROM %dX TO %dX: THE BITMAP WOULD BE TOO BIG FOR THIS DEVICE.", scale_wanted, scale);
    ALOG("starting the machine: gen %s, img %s, art %s, scale %d, view pad %d x %d", gen, img, art ? art : "-", scale,
         pad_x, pad_y);
    if (!wolf_init(&w, gen, img, art, scale, cmos, quiet_warn)) {
        char sym[1100];
        snprintf(sym, sizeof sym, "%s/symbols.txt", gen);
        fprintf(stderr, "cannot start: check --gen (%s) and --img (%s)\n", gen, img);
        {   /* say what is missing on the screen: a phone or a TV box has no console */
            char msg[1600];
            snprintf(msg, sizeof msg,
                     "The game cannot start.\n\ngen folder:  %s\n  symbols.txt: %s\nimage folder: %s\n  %s\n\n"
                     "Put the data there (docs/ANDROID.md): orig/IMG, gen (from build/gen, without gen/c), and "
                     "optionally sounds and art/hd.",
                     gen, fs_file_exists(sym) ? "found" : "MISSING", img,
                     fs_list_dir(img, dir_noop, NULL) < 0 ? "MISSING" : "found");
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "WWF WrestleMania", msg, NULL);
        }
        return 1;
    }
    for (int i = 0; i < set.nmods; i++) {          /* the mods switched on in the menu last time */
        char err[256];
        if (!mods_enable(&w, set.mods[i], err, sizeof err))
            fprintf(stderr, "mod %s (from %s): %s\n", set.mods[i], cfg_path, err);
        else if (mods_find(set.mods[i]))
            mods_set_arg(&w, mods_find(set.mods[i]),
                         settings_mod_arg(&set, set.mods[i], mods_find(set.mods[i])->arg_default));
    }
    for (int i = 0; i < nmods; i++) {
        char err[256];
        char base[64];
        snprintf(base, sizeof base, "%s", mods[i]);
        if (strchr(base, '='))
            *strchr(base, '=') = 0;
        if (mods_find(base) && mods_is_enabled(&w, mods_find(base))) {    /* already on from the settings */
            if (strchr(mods[i], '='))
                mods_set_arg(&w, mods_find(base), atoi(strchr(mods[i], '=') + 1));
            continue;
        }
        if (!mods_enable(&w, mods[i], err, sizeof err)) {
            fprintf(stderr, "mod %s: %s\n", mods[i], err);
            wolf_free(&w);
            return 1;
        }
    }
    ALOG("machine is up");
    wolf_set_options(&w, set.skip_selftest, set.powerups, set.no_ringout_timer, set.all_shadows);
    wolf_set_select_timer(&w, set.no_select_timer);
        wolf_set_match_timer(&w, set.no_match_timer);
    wolf_set_flashes(&w, set.no_flash_white, set.no_flash_red);
    wolf_set_free_play(&w, set.free_play);
    if (!classic) {
        if (!wolf_set_extra_size(&w, pad_x - 56, pad_y)) {
            wolf_free(&w);
            return 1;
        }
        w.v.view_pad = pad_x;
        w.v.view_pad_y = pad_y;
        wolf_set_draw_margin(&w, pad_x);
        wolf_set_draw_margin_y(&w, pad_y);
    }
    ALOG("opening the video");
    sdl_video sv;
    sdl_video_request_gpu(set.gpu);
    if (!sdl_video_open(&sv, "WWF WrestleMania", &w.v)) {
        wolf_free(&w);
        return 1;
    }
    ALOG("video is open");
    if (set.gpu) {
        char why[400];
        if (sdl_video_attach_gpu(&sv, &w.v, why, sizeof why)) {
            ALOG("GPU drawing is on");
            int prof = getenv("WWF_GPU_PROFILE") != NULL;
#ifdef __ANDROID__
            prof = prof || android_flag("gpuprofile");   /* the GPU's own time is logged too (slows the game a little) */
#endif
            gpu_video_set_profile(sv.gpu, prof);
            if (set.async_compute) {
                gpu_video_set_async(sv.gpu, 1, 2.0);
                printf("async compute: override textures are made from a queue, 2 ms per frame\n");
            }
        } else {
            fprintf(stderr, "GPU path not used: %s\n", why);
            ALOG("GPU path not used: %s", why);
            lim_add("GPU DRAWING NOT USED: %.120s", why);
        }
    }
    sv.need_argb = inspect;
    sv.zoom = zoom0;
    sv.zmin = zmin;
    sv.base_w = VIDEO_W + 2 * base_pad_x;
    sv.base_h = VIDEO_H + 2 * base_pad_y;
    printf("view %dx%d game pixels (rendered %dx%d), render scale %d, zoom %.2f (%.2f to 4)%s\n", sv.base_w,
           sv.base_h, VIDEO_W + 2 * w.v.view_pad, VIDEO_H + 2 * w.v.view_pad_y, scale, zoom0, zmin,
           classic ? " (classic)" : "");
    fflush(stdout);
    double shown_zoom = 0;
    int fullscreen = set.fullscreen;      /* what the window should be; F11 and the menu change it */
    menu mn;
    menu_init(&mn, &set, &sv.zoom, zmin, &fullscreen, cfg_path, &w);
    /* PRECACHE reads all HD art and sounds at start: only where there is room for them (a PC with plenty of RAM) */
    const int ram_mb = SDL_GetSystemRAM();
#ifdef __ANDROID__
    const int precache_ok = 0;
#else
    const int precache_ok = ram_mb >= 6144;
#endif
    const int precache = set.precache && precache_ok;
    mn.precache_ok = precache_ok;
    if (precache_ok)
        snprintf(mn.precache_note, sizeof mn.precache_note,
                 "READ ALL HD ART AND SOUNDS AT START, AND KEEP THEM: NO LOADING DURING THE GAME. USES UP TO %d MB OF YOUR %d MB RAM. TAKES EFFECT AFTER APPLY AND RESTART.",
                 ram_mb * 6 / 10, ram_mb);
    else
        snprintf(mn.precache_note, sizeof mn.precache_note,
                 "NOT ON THIS DEVICE: IT HAS %d MB RAM AND THE OPTION NEEDS 6144 MB.", ram_mb);
    if (set.precache && !precache_ok)
        lim_add("PRECACHE NOT USED: THIS DEVICE HAS %d MB RAM, IT NEEDS 6144 MB.", ram_mb);
    int precache_total = 0, precache_active = 0;
    inspector ins;
    inspect_init(&ins, &sv, inspect);
    if (inspect && !wolf_trace_draws(&w, 1))
        ins.recording = 0;
    overlays ov = {&mn, &ins, &set};
    sv.overlay = draw_overlays;
    sv.overlay_user = &ov;
    if (res_h > 0) {
        SDL_SetWindowSize(sv.win, res_w, res_h);
        SDL_SetWindowPosition(sv.win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }

    /* Sound: the extracted files, mixed here and queued to SDL. When audio
     * runs, it paces the frames; otherwise a timer does. */
    enum { RATE = 48000 };
    sdl_audio sa = {0, 0};
    int16_t *mixbuf = NULL;
    double mix_acc = 0;
    if (!mute) {
        char path[1024], err[512];
        fs_join(path, sizeof path, sound_dir, "sounds.txt");
        if (!fs_file_exists(path)) {
            fprintf(stderr, "no sound: %s not found (extract the sounds with dcsrip, see README)\n", path);
        } else if ((w.snd = snd_open(sound_dir, sound_art, RATE, err, sizeof err)) == NULL) {
            fprintf(stderr, "no sound: %s\n", err);
        } else if (!sdl_audio_open(&sa, RATE) || !(mixbuf = malloc(sizeof(int16_t) * 2 * RATE / 10))) {
            /* keep the player (it answers the board's handshakes) but stay silent */
            sdl_audio_close(&sa);
        } else if (precache) {
            size_t bytes = 0;
            int total = 0, got = snd_precache(w.snd, (size_t)ram_mb * 1024 * 1024 / 5, &bytes, &total);
            printf("precache: %d of %d sounds in memory (%zu MB)\n", got, total, bytes / 1048576);
            if (got < total)
                lim_add("PRECACHE: %d OF %d SOUNDS IN MEMORY (MEMORY BUDGET REACHED).", got, total);
        }
    }

    replay rp;
    memset(&rp, 0, sizeof rp);
    gamepad_open();
    if (w.snd)
        snd_set_gains(w.snd, (float)set.vol[0] / 100, (float)set.vol[1] / 100, (float)set.vol[2] / 100, (float)set.vol[3] / 100);

    /* The Wolf Unit runs at about 54.7 frames per second. */
    const double frame_ms = 1000.0 / 54.7;
    /* HD art: the first draw of an image queues its PNG for the workers (the original pixels show until it is
     * there), and the images of the wrestlers in the match are queued ahead of time */
    gfx_async *art_async = NULL;
    /* WWF_FRAME_STATS=1: wall time of each frame (art poll, game, present) summed up when the run ends (a test aid) */
    const int frame_stats = getenv("WWF_FRAME_STATS") != NULL;
    double *frame_times = frame_stats ? calloc(200000, sizeof *frame_times) : NULL;
    long nframe_times = 0;
    Uint64 t_loop = 0;
    {
        int have_art = 0;
        for (int i = 0; i < w.cat.nimages && !have_art; i++)
            have_art = w.cat.images[i].has_override;
#ifdef __ANDROID__
        if (android_flag("syncart"))
            sync_art = 1;
        prefetch_mb = prefetch_mb > 96 ? 96 : prefetch_mb;
        if (art_budget_mb > 260) {
            art_budget_mb = 260;       /* a 3 GB TV box: 260 MB for the art, counting the GPU's copy */
            if (art && art[0] && have_art)
                lim_add("HD ART KEPT WITHIN %ld MB OF MEMORY ON THIS DEVICE: IMAGES NOT DRAWN FOR A WHILE ARE FREED AND READ AGAIN.", art_budget_mb);
        }
#endif
        if (precache) {                /* everything, within 60% of the RAM (the GPU's copy counts too) */
            art_budget_mb = ram_mb * 6 / 10;
            prefetch_mb = art_budget_mb / (sv.gpu ? 2 : 1);
        }
        if (art && art[0] && have_art && !sync_art && !getenv("WWF_SYNC_ART")) {
            art_async = gfx_async_start(&w.gc, art_threads, prefetch_mb * 1024L * 1024L);
            ALOG("HD art: %s", art_async ? "read on worker threads" : "worker threads not available, read on the game thread");
            if (!art_async)
                lim_add("HD ART IS READ ON THE GAME THREAD: NO WORKER THREADS, SO LOADING CAN CAUSE SHORT PAUSES.");
        }
        if (precache && art && art[0] && have_art) {
            precache_active = 1;
            if (art_async) {           /* queued in the background, the game does not wait */
                for (int i = 0; i < w.cat.nimages; i++)
                    if (w.cat.images[i].has_override)
                        gfx_prefetch_add(&w.gc, &w.cat.images[i]);
                precache_total = w.gc.plan_n;
            } else {                   /* no threads: read them now, up to the budget */
                size_t cap = (size_t)art_budget_mb * 1048576 / (sv.gpu ? 2 : 1);
                for (int i = 0; i < w.cat.nimages && gfx_cache_resident(&w.gc) < cap; i++)
                    if (w.cat.images[i].has_override)
                        gfx_get(&w.gc, &w.cat.images[i]);
                printf("precache: %.0f MB of HD art in memory\n", (double)gfx_cache_resident(&w.gc) / 1048576.0);
            }
        }
    }
    double next = (double)SDL_GetTicks();
    Uint64 dbg_machine = 0, dbg_present = 0, dbg_t0 = SDL_GetPerformanceCounter();   /* DEBUG INFO: ticks of the last 30 frames */
    int dbg_n = 0;
    int running = 1, restart = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
#ifdef __ANDROID__
            if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_KEYDOWN)
                ALOG("input: %s %d", e.type == SDL_KEYDOWN ? "key" : "pad button",
                     e.type == SDL_KEYDOWN ? (int)e.key.keysym.sym : (int)e.cbutton.button);
#endif
            if (e.type == SDL_QUIT) {
                running = 0;
            } else if (e.type == SDL_CONTROLLERDEVICEADDED || e.type == SDL_CONTROLLERDEVICEREMOVED) {
                gamepad_event(&e);
            } else if (e.type == SDL_RENDER_DEVICE_RESET) {
                sdl_video_gpu_reset(&sv);   /* the GL context was made again: the GPU path starts from the CPU copy */
            } else if (menu_event(&mn, &e)) {
                continue;                   /* F1 menu: it has the keys while open */
            } else if (inspect_event(&ins, &e)) {
                continue;                   /* F6: the image inspector (--inspect) */
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
                running = 0;
            } else if (e.type == SDL_MOUSEWHEEL && e.wheel.y != 0) {
                sdl_video_zoom(&sv, e.wheel.y > 0 ? 1.1 : 1 / 1.1);
            } else if (e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_EQUALS || e.key.keysym.sym == SDLK_KP_PLUS)) {
                sdl_video_zoom(&sv, 1.1);
            } else if (e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_MINUS || e.key.keysym.sym == SDLK_KP_MINUS)) {
                sdl_video_zoom(&sv, 1 / 1.1);
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_0) {
                sdl_video_zoom_reset(&sv);
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F5) {
                printf("save state: %s\n", wolf_state_save(&w, "wwf.state") ? "saved wwf.state" : "failed");
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F7) {
                if (rp.mode == REPLAY_RECORDING) {      /* F7 again: stop and keep it */
                    size_t n = rp.count;
                    printf("recording: %s (%lu frames)\n", replay_stop_record(&rp, "wwf.rec") ? "saved wwf.rec" : "failed",
                           (unsigned long)n);
                } else if (wolf_state_save(&w, "wwf.state")) {     /* the state it starts from */
                    replay_start_record(&rp);
                    printf("recording from here (F7 stops)\n");
                } else {
                    printf("recording: cannot write wwf.state\n");
                }
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F8) {
                rp.mode = REPLAY_IDLE;
                if (wolf_state_load(&w, "wwf.state") && replay_start_play(&rp, "wwf.rec"))
                    printf("replaying wwf.rec (any load or F8 again starts over)\n");
                else
                    printf("replay: needs wwf.state and wwf.rec\n");
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F9) {
                rp.mode = REPLAY_IDLE;
                printf("load state: %s\n", wolf_state_load(&w, "wwf.state") ? "loaded wwf.state" : "failed");
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F11) {
                fullscreen = !fullscreen;
            }
        }
        if (mn.restart) {                   /* "apply and restart": the settings are saved, start again below */
            restart = 1;
            break;
        }
        {
            int is_full = (SDL_GetWindowFlags(sv.win) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
            if (fullscreen != is_full)
                SDL_SetWindowFullscreen(sv.win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            set.fullscreen = fullscreen;
        }
        if (sv.zoom != shown_zoom) {
            char title[64];
            shown_zoom = sv.zoom;
            snprintf(title, sizeof title, "WWF WrestleMania  (zoom %.2fx)", shown_zoom);
            SDL_SetWindowTitle(sv.win, title);
        }
        sv.smooth = set.smooth;
        sv.integer_scale = set.integer_scale;
        sv.crt_aspect = set.crt_aspect;
        sv.scanlines = set.scanlines;
        /* keep the camera off the ends of the level by what the view shows
         * beyond the original screen, less the background's slack (~15 px) */
        int vis = sdl_video_visible_pad(&sv), vis_y = sdl_video_visible_pad_y(&sv);
        wolf_set_scroll_inset(&w, classic || vis <= 16 ? 0 : vis - 16);
        wolf_set_scroll_inset_y(&w, classic ? 0 : vis_y);
        wolf_set_options(&w, set.skip_selftest, set.powerups, set.no_ringout_timer, set.all_shadows);
        wolf_set_select_timer(&w, set.no_select_timer);
        wolf_set_match_timer(&w, set.no_match_timer);
        wolf_set_flashes(&w, set.no_flash_white, set.no_flash_red);
        wolf_set_hud_spread(&w, classic || !set.hud_spread ? 0 : vis, classic || !set.hud_spread ? 0 : vis_y);
        wolf_set_art_layers(&w, (unsigned)set.art_off);     /* the F1 menu's HD art layers */
        if (shot_menu >= 0 && shot_file && (long)w.frames >= shot_frame && !mn.open) {
            mn.open = 1;
            mn.page = shot_menu;
            mn.sel = shot_sel;
            sv.shot_path = shot_file;
                sv.zoom_off = !w.in_match;
        sdl_video_present(&sv, &w.v);
            break;
        }
        if (shot_inspect >= 0 && shot_file && (long)w.frames >= shot_frame && ins.count) {
            inspect_freeze(&ins, shot_inspect);
            sv.shot_path = shot_file;
            inspect_present(&ins);
            break;
        }
        if (ins.active && !mn.open) {       /* the image inspector: frozen, or a step of one or ten frames */
            memset(w.player, 0, sizeof w.player);
            memset(w.extra_player, 0, sizeof w.extra_player);
            w.coin_bits = 0;
            if (ins.step > 0) {
                int n = ins.step;
                ins.step = 0;
                ins.active = 0;             /* so the frames are kept */
                for (int k = 0; k < n && (art_async ? (gfx_async_poll(art_async), 1) : 1) && wolf_frame(&w); k++) {
                    sv.zoom_off = !w.in_match;
                    sdl_video_present(&sv, &w.v);
                    inspect_capture(&ins, &w);
                }
                ins.active = 1;
                ins.back = 0;
            }
            inspect_present(&ins);
            SDL_Delay(16);
            next = (double)SDL_GetTicks();
            continue;
        }
        if (mn.open) {                      /* paused: nothing pressed reaches the game */
            memset(w.player, 0, sizeof w.player);
            memset(w.extra_player, 0, sizeof w.extra_player);
            w.coin_bits = 0;
                sv.zoom_off = !w.in_match;
        sdl_video_present(&sv, &w.v);
            SDL_Delay(16);
            next = (double)SDL_GetTicks();
            continue;
        }
        read_keys(&w, &set);
        gamepad_read(&w, set.pad);
        for (int k = 0; k < ninputs; k++)
            if ((long)w.frames >= inputs[k].frame && (long)w.frames < inputs[k].frame + inputs[k].len) {
                if (inputs[k].what == 4)
                    w.coin_bits |= (uint16_t)inputs[k].bits;
                else if (inputs[k].what >= 0 && inputs[k].what < 4)
                    w.player[inputs[k].what] |= (uint8_t)inputs[k].bits;
                else if (inputs[k].what == 6 || inputs[k].what == 7)   /* x3, x4: the mods' third and fourth */
                    w.extra_player[inputs[k].what - 6] |= (uint16_t)inputs[k].bits;
            }
        replay_frame_inputs(&rp, &w);
#ifdef __ANDROID__
        Uint32 t_frame = SDL_GetTicks();
#endif
        t_loop = SDL_GetPerformanceCounter();
        gfx_cache_tick(&w.gc);
        if (banner_frames > 0)
            banner_frames--;
        if (art_async) {
            if (settle_from >= 0 && (long)w.frames >= settle_from)
                gfx_async_wait_idle(art_async);
            else
                gfx_async_poll(art_async);
            wolf_prefetch_tick(&w);
        }
        if (precache_active) {               /* read all the art, and with the GPU make its textures: show how far it is */
            int tot = 0, ld = 0, up = 0;
            if (sv.gpu) {
                gpu_video_precache(sv.gpu, w.gc.images, w.cat.nimages, 3.0, &tot, &ld, &up);
            } else {
                for (int i = 0; i < w.cat.nimages; i++)
                    if (w.gc.images[i].entry && w.gc.images[i].entry->has_override && !w.gc.images[i].hi_off) {
                        tot++;
                        ld += w.gc.images[i].hi_state == 1 && w.gc.images[i].hi;
                    }
                up = ld;
            }
            precache_ui.on = 1;
            precache_ui.gpu = sv.gpu != NULL;
            precache_ui.total = tot;
            precache_ui.loaded = ld;
            precache_ui.on_gpu = up;
            if (w.gc.npending == 0 && w.gc.plan_n == 0 && up == ld) {     /* everything that will be read is read and uploaded */
                precache_active = 0;
                precache_ui.on = 0;
                precache_ui.ready_frames = 240;
            }
        } else if (precache_ui.ready_frames > 0) {
            precache_ui.ready_frames--;
        }
        if (!wolf_frame(&w)) {
            uint32_t off = 0;
            const char *sym = wolf_symbol(gen, w.cpu.fault_pc, &off);
            fprintf(stderr, "stopped: %s at %08X (%s+0x%X)\n",
                    w.cpu.fault ? w.cpu.fault : "?", w.cpu.fault_pc, sym ? sym : "?", off);
            break;
        }
        const Uint64 t_after_machine = SDL_GetPerformanceCounter();
        dbg_machine += t_after_machine - t_loop;
        if (shot_file && shot_menu < 0 && (long)w.frames == shot_frame)
            sv.shot_path = shot_file;
        sv.zoom_off = !w.in_match;
        dynamic_zoom(&sv, &w, &set, set.dyn_zoom && !classic);
#ifdef __ANDROID__
        Uint32 t_present = SDL_GetTicks();
        static Uint32 ms_frame, ms_present;
        ms_frame += t_present - t_frame;
#endif
        sdl_video_present(&sv, &w.v);
        if (w.frames % 30 == 0 && art_budget_mb > 0) {    /* the frame is shown: nothing refers to the images of the last list */
            gfx_image *gone[256];
            long budget_mb = art_budget_mb;
            int min_age = 180;
#ifdef __ANDROID__
            /* the system's own memory is the real limit: when it runs low (a TV box with 3 GB had 200 MB free while
             * a Royal Rumble ran, and the process was killed at the next burst of new images), let go of more, sooner,
             * prefetched images included (a negative age) */
            long avail = mem_available_mb();
            if (avail >= 0 && avail < 160) {
                budget_mb = art_budget_mb / 4;
                min_age = -4000;
            } else if (avail >= 0 && avail < 320) {
                budget_mb = art_budget_mb / 2;
                min_age = 30;
            }
            static int last_level = 0;
            int level = avail >= 0 && avail < 160 ? 2 : avail >= 0 && avail < 320 ? 1 : 0;
            mem_level = level;
            mem_budget_mb = budget_mb;
            if (level != last_level) {
                ALOG("memory: %ld MB available, art budget now %ld MB (level %d)", avail, budget_mb, level);
                last_level = level;
            }
#endif
            size_t cpu_budget = (size_t)budget_mb * 1024 * 1024 / (sv.gpu ? 2 : 1);
            int ng = gfx_cache_evict(&w.gc, cpu_budget, min_age, gone, 256);
            if (ng && sv.gpu)
                gpu_video_release_images(sv.gpu, gone, ng);
            art_evicted += ng;
        }
        dbg_present += SDL_GetPerformanceCounter() - t_after_machine;
        if (++dbg_n >= 30) {
            const Uint64 now = SDL_GetPerformanceCounter(), freq = SDL_GetPerformanceFrequency();
            if (set.debug_overlay) {
                const double wall = (double)(now - dbg_t0) * 1000.0 / (double)freq / dbg_n;
                memset(dbg_text, 0, sizeof dbg_text);
                snprintf(dbg_text[0], sizeof dbg_text[0], "FPS %.1f  FRAME %.1f MS  GAME %.1f  DRAW %.1f", 1000.0 / wall, wall,
                         (double)dbg_machine * 1000.0 / (double)freq / dbg_n, (double)dbg_present * 1000.0 / (double)freq / dbg_n);
                snprintf(dbg_text[1], sizeof dbg_text[1], "BITMAP %dX%d  RENDER SCALE %d", w.v.w, w.v.h, w.v.scale);
                if (sv.gpu) {
                    long rb = 0, pr = 0;
                    gpu_video_totals(sv.gpu, &rb, &pr);
                    snprintf(dbg_text[2], sizeof dbg_text[2], "GPU %s  READBACKS %ld  PIXEL READS %ld",
                             gpu_video_suspended(sv.gpu) ? "PAUSED (CPU DRAWING)" : "DRAWING", rb, pr);
                } else {
                    snprintf(dbg_text[2], sizeof dbg_text[2], "GPU OFF (CPU DRAWING)");
                }
                snprintf(dbg_text[3], sizeof dbg_text[3], "HD ART %.0f MB IN MEMORY, %ld FREED, %d PENDING",
                         (double)gfx_cache_resident(&w.gc) / 1048576.0, art_evicted, w.gc.npending);
#ifdef __ANDROID__
                snprintf(dbg_text[4], sizeof dbg_text[4], "SYSTEM MEMORY %ld MB AVAILABLE", mem_available_mb());
#endif
                if (precache_total > 0) {
                    int left = w.gc.plan_n - w.gc.plan_pos;
                    snprintf(dbg_text[5], sizeof dbg_text[5], "PRECACHE %d OF %d IMAGES QUEUED OR LOADED", precache_total - left,
                             precache_total);
                }
            } else {
                memset(dbg_text, 0, sizeof dbg_text);
            }
            {   /* the limits: those known at start and those that came up while playing */
                char lines[8][160];
                int nl = 0;
                static int last_nl;
                for (int i = 0; i < nlim_static && nl < 8; i++)
                    snprintf(lines[nl++], sizeof lines[0], "%s", lim_static[i]);
                if (art_evicted > 0 && art_budget_mb > 0 && nl < 8)
                    snprintf(lines[nl++], sizeof lines[0],
                             "HD ART: %ld IMAGES FREED SO FAR TO STAY WITHIN %ld MB OF MEMORY. THEY ARE READ AGAIN WHEN NEEDED.",
                             art_evicted, art_budget_mb);
                if (mem_level > 0 && nl < 8)
                    snprintf(lines[nl++], sizeof lines[0], "THE SYSTEM IS LOW ON MEMORY: HD ART KEPT TO %ld MB FOR NOW.", mem_budget_mb);
                if (precache && w.gc.prefetch_cap > 0 && w.gc.prefetch_bytes >= w.gc.prefetch_cap && nl < 8)
                    snprintf(lines[nl++], sizeof lines[0],
                             "PRECACHE STOPPED: THE MEMORY BUDGET (%ld MB) WAS REACHED. THE REST IS READ WHEN IT IS NEEDED.", art_budget_mb);
                menu_limits_set(&mn, lines, nl);
                if (nl > last_nl && set.limit_warnings) {
                    snprintf(banner_text, sizeof banner_text, "%d LIMIT%s ACTIVE - F1, DISPLAY, LIMITS", nl, nl > 1 ? "S" : "");
                    banner_frames = 600;
                }
                last_nl = nl;
            }
            dbg_machine = dbg_present = 0;
            dbg_n = 0;
            dbg_t0 = now;
        }
#ifdef __ANDROID__
        {
            static int last_gs = -2;
            if (w.gamstate != last_gs) {
                ALOG("gamstate %d -> %d at frame %ld", last_gs, w.gamstate, (long)w.frames);
                if (w.gamstate == 0 && last_gs > 0) {   /* the machine started over: say what ran last */
                    char buf[1024];
                    int len = 0;
                    for (unsigned b = 0; b < 60 && len < (int)sizeof buf - 40; b++) {
                        uint32_t off, pc = wolf_trace_pc(&w, b);
                        const char *nm = pc ? wolf_symbol(w.gen_dir, pc, &off) : NULL;
                        len += snprintf(buf + len, sizeof buf - (size_t)len, "%s+%X ", nm ? nm : "?", nm ? (unsigned)off : 0u);
                    }
                    ALOG("before the restart (newest first): %s", buf);
                }
                last_gs = w.gamstate;
            }
        }
#endif
        inspect_capture(&ins, &w);
        if (frame_times && nframe_times < 200000)
            frame_times[nframe_times++] =
                (double)(SDL_GetPerformanceCounter() - t_loop) * 1000.0 / (double)SDL_GetPerformanceFrequency();
#ifdef __ANDROID__
        ms_present += SDL_GetTicks() - t_present;
        if (w.frames % 60 == 0) {
            static Uint32 last;
            Uint32 now = SDL_GetTicks();
            ALOG("frame %ld, %u ms for the last 60 (machine %u, present %u)", (long)w.frames, (unsigned)(now - last),
                 (unsigned)ms_frame, (unsigned)ms_present);
            {
                long nl = 0;
                double ml = 0;
                int pend = 0;
                gfx_cache_load_stats(&w.gc, &nl, &ml, &pend);
                if (nl || pend)
                    ALOG("hd art read from disk: %ld images, %.0f ms%s, %d pending", nl, ml,
                         art_async ? " (on the workers)" : "", pend);
                ALOG("hd art in memory: %.0f MB decoded (budget %ld MB with the GPU's copy), %ld freed so far",
                     (double)gfx_cache_resident(&w.gc) / 1048576.0, art_budget_mb, art_evicted);
            }
            if (sv.gpu) {
                char gs[480];
                gpu_video_stats(sv.gpu, gs, sizeof gs);
                ALOG("%s", gs);
                uint32_t tag[8];
                long cnt[8];
                int nt = gpu_video_readback_callers(sv.gpu, tag, cnt, 8);
                for (int k = 0; k < nt; k++) {
                    uint32_t off = 0;
                    const char *sn = tag[k] > 3 ? wolf_symbol(gen, tag[k], &off) : NULL;
                    ALOG("  read back %ld times by %s (%08X%s%X)", cnt[k],
                         tag[k] == 1 ? "a blit the GPU refused" : tag[k] == 2 ? "the CPU conversion" : tag[k] == 3 ? "a saved state"
                                                                                                           : sn ? sn : "?",
                         (unsigned)tag[k], sn ? " +0x" : "", (unsigned)off);
                }
            }
            last = now;
            ms_frame = ms_present = 0;
        }
#endif
        if (shot_file && (long)w.frames > shot_frame + 1)
            break;                          /* test run: unpaced, ends after the shot */
        if (shot_file)
            continue;
        /* speed: the menu's percentage; Tab held is fast forward (4x, without sound) */
        int ff = SDL_GetKeyboardState(NULL)[SDL_SCANCODE_TAB] != 0;
        double speed = ff ? 4.0 : set.speed / 100.0;
        if (w.snd) {
            mix_acc += RATE / (54.7 * speed);
            int n = (int)mix_acc;
            mix_acc -= n;
            if (sa.dev && !ff) {
                snd_mix(w.snd, mixbuf, n);
                sdl_audio_queue(&sa, mixbuf, n);
                /* keep about three frames queued: audio sets the pace */
                while (sdl_audio_backlog(&sa) > 3 * RATE / 54)
                    SDL_Delay(1);
                next = (double)SDL_GetTicks();
                continue;
            }
            static int16_t scratch[2 * RATE / 10];
            snd_mix(w.snd, scratch, n);
        }
        next += frame_ms / speed;
        double now = (double)SDL_GetTicks();
        if (next > now)
            SDL_Delay((Uint32)(next - now));
        else if (now - next > 100)
            next = now;
    }
    replay_free(&rp);
    inspect_free(&ins);
    gamepad_close();
    sdl_audio_close(&sa);
    free(mixbuf);
    snd_close(w.snd);
    w.snd = NULL;
    if (frame_times && nframe_times > 0) {
        double sum = 0, mx = 0;
        long over20 = 0, over50 = 0;
        for (long i = 0; i < nframe_times; i++) {
            sum += frame_times[i];
            mx = frame_times[i] > mx ? frame_times[i] : mx;
            over20 += frame_times[i] > 20;
            over50 += frame_times[i] > 50;
            if (frame_times[i] > 50)
                printf("  slow frame %ld: %.1f ms\n", i, frame_times[i]);
        }
        printf("frame stats (%s): %ld frames, mean %.2f ms, max %.1f ms, over 20 ms %ld, over 50 ms %ld\n",
               art_async ? "async art" : "sync art", nframe_times, sum / (double)nframe_times, mx, over20, over50);
    }
    free(frame_times);
    gfx_async_stop(art_async);              /* before the image cache goes */
    sdl_video_close(&sv);
    wolf_free(&w);                          /* saves the CMOS */
#ifdef __ANDROID__
    if (restart) {                          /* no exec on Android: the settings are saved, the app is started again by hand */
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "WWF WrestleMania",
                                 "Settings saved. Close the app and start it again to apply them.", NULL);
        return 0;
    }
#else
    if (restart) {
        /* The same command line, less what the settings now hold (the mods and free play were
         * saved as they are, so a mod switched off in the menu stays off). */
        char **args = calloc((size_t)argc + 1, sizeof *args);
        int n = 0;
        for (int i = 0; args && i < argc; i++) {
            if (i > 0 && !strcmp(argv[i], "--mod") && i + 1 < argc) {
                i++;
                continue;
            }
            if (i > 0 && !strcmp(argv[i], "--free-play"))
                continue;
            if (i > 0 && !strcmp(argv[i], "--res") && i + 1 < argc) {      /* the settings hold the size now */
                i++;
                continue;
            }
            args[n++] = argv[i];
        }
        if (args) {
            fflush(NULL);
#ifdef _WIN32
            _execvp(argv[0], (const char *const *)args);
#else
            execvp(argv[0], args);
#endif
        }
        fprintf(stderr, "cannot restart (%s): start the game again by hand\n", argv[0]);
        free(args);
        return 1;
    }
#endif
    return 0;
}
