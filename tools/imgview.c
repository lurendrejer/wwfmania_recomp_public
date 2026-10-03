/*
 * imgview - browse the game's images through the port's DMA renderer.
 *
 *   imgview <imgdir> [override_dir] [--scale N] [--label NAME]
 *
 * Keys: Left/Right previous/next image, PgUp/PgDn jump 50, Home first,
 *       H flip horizontally, V flip vertically, F flash (constant color),
 *       +/- DMA scale, 0 reset scale, O toggle overrides,
 *       R rescan the override directory, Esc quit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/sdl_video.h"
#include "preview.h"
#include "util/fsutil.h"

/* Duplicate-label notices are listed by `imgtool catalog`; skip them here. */
static void quiet_warn(const char *msg, void *user)
{
    (void)user;
    if (strncmp(msg, "duplicate image ", 16) != 0)
        fprintf(stderr, "warning: %s\n", msg);
}

static void update_title(SDL_Window *win, const catalog *cat, const cat_image *ci,
                         const gfx_image *gi, const preview_opts *o, int use_overrides)
{
    char title[256];
    const img_image *im = gi->img;
    char hi[32] = "original";
    if (gi->hi_state == 1)
        snprintf(hi, sizeof hi, "override %dx", gi->hi_factor);
    else if (ci->has_override && !use_overrides)
        snprintf(hi, sizeof hi, "override off");
    snprintf(title, sizeof title, "%s  [%d/%d]  %s  %dx%d  anchor %d,%d  scale %.2f  %s%s%s",
             ci->name, (int)(ci - cat->images) + 1, cat->nimages, cat->libs[ci->lib].file,
             im->width, im->height, im->anix, im->aniy, 256.0 / o->scale, hi,
             o->fliph ? "  H" : "", o->flipv ? "  V" : "");
    SDL_SetWindowTitle(win, title);
}

int main(int argc, char **argv)
{
    const char *imgdir = NULL, *override_dir = NULL, *label = NULL;
    int scale = 3;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (strcmp(argv[i], "--label") == 0 && i + 1 < argc)
            label = argv[++i];
        else if (!imgdir)
            imgdir = argv[i];
        else if (!override_dir)
            override_dir = argv[i];
    }
    if (!imgdir) {
        fprintf(stderr, "usage: imgview <imgdir> [override_dir] [--scale N] [--label NAME]\n");
        return 2;
    }

    catalog cat;
    if (!catalog_open(&cat, imgdir, override_dir, NULL, quiet_warn, NULL) || !cat.nimages) {
        fprintf(stderr, "no images found in %s\n", imgdir);
        return 1;
    }
    gfx_cache gc;
    video v;
    sdl_video sv;
    if (!gfx_cache_init(&gc, &cat, quiet_warn, NULL) || !video_init(&v, scale) ||
        !sdl_video_open(&sv, "imgview", &v)) {
        fprintf(stderr, "initialization failed\n");
        return 1;
    }

    int cur = 0;
    if (label) {
        const cat_image *ci = catalog_find(&cat, label);
        if (ci)
            cur = (int)(ci - cat.images);
        else
            fprintf(stderr, "%s: not in catalog\n", label);
    }
    preview_opts o = {0, 0, 0, DMA_SCALE_1X};
    int dirty = 1, running = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = 0;
            } else if (e.type == SDL_WINDOWEVENT) {
                dirty = 1;
            } else if (e.type == SDL_KEYDOWN) {
                dirty = 1;
                switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: running = 0; break;
                case SDLK_RIGHT: cur = (cur + 1) % cat.nimages; break;
                case SDLK_LEFT: cur = (cur + cat.nimages - 1) % cat.nimages; break;
                case SDLK_PAGEDOWN: cur = (cur + 50) % cat.nimages; break;
                case SDLK_PAGEUP: cur = (cur + cat.nimages - 50 % cat.nimages) % cat.nimages; break;
                case SDLK_HOME: cur = 0; break;
                case SDLK_h: o.fliph = !o.fliph; break;
                case SDLK_v: o.flipv = !o.flipv; break;
                case SDLK_f: o.flash = !o.flash; break;
                case SDLK_0: o.scale = DMA_SCALE_1X; break;
                case SDLK_PLUS:
                case SDLK_KP_PLUS:
                case SDLK_EQUALS:
                    if (o.scale > 0x40) o.scale -= 0x20;
                    break;
                case SDLK_MINUS:
                case SDLK_KP_MINUS:
                    if (o.scale < 0x800) o.scale += 0x20;
                    break;
                case SDLK_o:
                    gc.use_overrides = !gc.use_overrides;
                    gfx_cache_reload(&gc);
                    break;
                case SDLK_r:
                    catalog_rescan_overrides(&cat);
                    gfx_cache_reload(&gc);
                    break;
                default: dirty = 0; break;
                }
            }
        }
        if (dirty) {
            const cat_image *ci = &cat.images[cur];
            const gfx_image *gi = gfx_get(&gc, ci);
            preview_draw(&v, catalog_lib(&cat, ci), gi, &o);
            update_title(sv.win, &cat, ci, gi, &o, gc.use_overrides);
            dirty = 0;
        }
        sdl_video_present(&sv, &v);
        SDL_Delay(16);
    }

    sdl_video_close(&sv);
    video_free(&v);
    gfx_cache_free(&gc);
    catalog_close(&cat);
    return 0;
}
