#include "sdl_video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu_video.h"

/* (Re)creates the texture and pixel buffer for sv->w x sv->h. With the GPU path the texture is a render
 * target the GPU fills (bytes R, G, B, A); otherwise the CPU streams ARGB pixels into it. */
static int make_target(sdl_video *sv, int gpu)
{
    if (sv->tex)
        SDL_DestroyTexture(sv->tex);
    free(sv->argb);
    sv->tex = gpu ? SDL_CreateTexture(sv->ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, sv->w, sv->h)
                  : SDL_CreateTexture(sv->ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, sv->w, sv->h);
    sv->argb = malloc((size_t)sv->w * sv->h * sizeof *sv->argb);
    if (sv->tex && gpu) {         /* start as a streaming texture does: all zero */
        SDL_SetRenderTarget(sv->ren, sv->tex);
        SDL_SetRenderDrawColor(sv->ren, 0, 0, 0, 0);
        SDL_RenderClear(sv->ren);
        SDL_SetRenderTarget(sv->ren, NULL);
    }
    return sv->tex && sv->argb;
}

static SDL_Window *adopted_window;
static int want_gpu;

void sdl_video_request_gpu(int on)
{
    want_gpu = on;
}

void sdl_video_use_window(SDL_Window *win)
{
    adopted_window = win;
}

int sdl_video_open(sdl_video *sv, const char *title, const video *v)
{
    memset(sv, 0, sizeof *sv);
    sv->zoom = 1.0;
    sv->zoom_dyn = 0;
    sv->zmin = 1.0;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 0;
    }
    sv->w = video_view_width(v);
    sv->h = video_view_height(v);

    /* Start at the displayed size, shrunk to fit the monitor if needed. */
    int ww = sv->w, wh = sv->h;
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0) {
        int step_w = video_view_width(v) / v->scale;
        while ((ww > usable.w || wh > usable.h) && ww > step_w) {
            ww -= step_w;
            wh -= VIDEO_H;
        }
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    if (adopted_window) {
        sv->win = adopted_window;
        adopted_window = NULL;
        SDL_SetWindowTitle(sv->win, title);
    } else {
        sv->win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh,
                                   SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    }
    if (!sv->win)
        goto fail;
    if (want_gpu) {              /* the GPU path runs on the renderer's OpenGL ES 2 context */
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
        sv->ren = SDL_CreateRenderer(sv->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, NULL);
        if (!sv->ren)
            gpu_video_log("GPU path: no OpenGL ES 2 renderer (%s)", SDL_GetError());
    }
    if (!sv->ren)
        sv->ren = SDL_CreateRenderer(sv->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sv->ren)
        sv->ren = SDL_CreateRenderer(sv->win, -1, SDL_RENDERER_SOFTWARE);
    if (!sv->ren)
        goto fail;
    if (!make_target(sv, 0))
        goto fail;
    return 1;

fail:
    fprintf(stderr, "SDL video: %s\n", SDL_GetError());
    sdl_video_close(sv);
    return 0;
}

int sdl_video_attach_gpu(sdl_video *sv, video *v, char *why, size_t why_len)
{
    if (!sv->ren || !sv->tex) {
        snprintf(why, why_len, "no renderer");
        return 0;
    }
    sv->gpu = gpu_video_create(sv->ren, v, why, why_len);
    if (!sv->gpu)
        return 0;
    sv->gpu_vid = v;
    if (!make_target(sv, 1)) {
        snprintf(why, why_len, "cannot make the frame texture a render target (%s)", SDL_GetError());
        gpu_video_destroy(sv->gpu, v);
        sv->gpu = NULL;
        make_target(sv, 0);
        return 0;
    }
    gpu_video_log("GPU path on: %s", gpu_video_info(sv->gpu));
    return 1;
}

void sdl_video_gpu_reset(sdl_video *sv)
{
    if (sv->gpu) {
        gpu_video_context_lost(sv->gpu);
        if (sv->gpu_vid)
            video_set_sink(sv->gpu_vid, sv->gpu_vid->sink);     /* what the GPU held is gone: the CPU copy is used */
    }
}

/* The GPU path failed: the CPU path takes over. */
static void gpu_off(sdl_video *sv, video *v)
{
    gpu_video_log("GPU path: off, drawing on the CPU");
    gpu_video_destroy(sv->gpu, v);
    sv->gpu = NULL;
    sv->gpu_vid = NULL;
    make_target(sv, 0);
}

void sdl_video_zoom(sdl_video *sv, double factor)
{
    sv->zoom *= factor;
    if (sv->zoom < sv->zmin)
        sv->zoom = sv->zmin;
    if (sv->zoom > 4.0)
        sv->zoom = 4.0;
}

void sdl_video_zoom_reset(sdl_video *sv)
{
    sv->zoom = 1.0;
}

int sdl_video_visible_pad(const sdl_video *sv)
{
    return sv->vis_pad;
}

int sdl_video_visible_pad_y(const sdl_video *sv)
{
    return sv->vis_pad_y;
}

static void show(sdl_video *sv, const uint32_t *argb, SDL_Rect src, int scale);

void sdl_video_present(sdl_video *sv, video *v)
{
    if (!sv->tex || !sv->argb)
        return;

    /* Visible part, in game pixels: the planned view divided by the zoom, with
     * the window's shape, but not more than what was rendered. */
    int ww = 1, wh = 1;
    SDL_GetWindowSize(sv->win, &ww, &wh);
    if (ww < 1 || wh < 1)
        ww = wh = 1;
    double zoom = sv->zoom < sv->zmin ? sv->zmin : sv->zoom;
    if (sv->zoom_dyn > 0) {
        zoom = sv->zoom_dyn < sv->zmin ? sv->zmin : sv->zoom_dyn > 4.0 ? 4.0 : sv->zoom_dyn;
    }
    if (sv->zoom_off)
        zoom = 1.0;
    double max_w = (double)sv->w / v->scale, max_h = (double)sv->h / v->scale;
    /* 400 x 254 was shown at 4:3 on the arcade monitor: each pixel 0.847 as wide as tall */
    double par = sv->crt_aspect ? (4.0 / 3.0) / ((double)VIDEO_W / VIDEO_H) : 1.0;
    double a = (double)ww / wh / par;
    double base_h = sv->base_h > 0 ? sv->base_h : VIDEO_H;
    double vis_h = base_h / zoom;
    double vis_w = vis_h * a;
    if (vis_w > max_w)
        vis_w = max_w;
    if (vis_h > max_h)
        vis_h = max_h;
    sv->vis_pad = vis_w > VIDEO_W ? (int)((vis_w - VIDEO_W) / 2) : 0;
    sv->vis_pad_y = vis_h > VIDEO_H ? (int)((vis_h - VIDEO_H) / 2) : 0;

    SDL_Rect src;
    src.w = (int)(vis_w * v->scale + 0.5);
    src.h = (int)(vis_h * v->scale + 0.5);
    if (src.w > sv->w)
        src.w = sv->w;
    if (src.h > sv->h)
        src.h = sv->h;
    if (src.w < 1)
        src.w = 1;
    if (src.h < 1)
        src.h = 1;
    src.x = (sv->w - src.w) / 2;
    src.y = (sv->h - src.h) / 2;

    sv->shown_par = par;
    if (sv->gpu) {
        /* The original moves pixels one by one in some effects (the first screen fades in with small squares): the
         * GPU path pays a read back and an upload for each frame of that, the CPU path does not. Pause the GPU
         * for as long as it lasts: two frames in a row with thousands of single pixel fills or a full read back
         * start it, twenty quiet frames end it. */
        long fills = 0, rbs = 0;
        if (!gpu_video_suspended(sv->gpu)) {
            gpu_video_activity(sv->gpu, &fills, &rbs);
            sv->rb_streak = (fills > 2000 || rbs > 0) ? sv->rb_streak + 1 : 0;
            if (sv->rb_streak >= 2) {
                gpu_video_suspend(sv->gpu, v);
                sv->rb_streak = 0;
                sv->calm_streak = 0;
                sv->paused_frames = 0;
                v->cpu_px_ops = 0;
            }
        } else {
            /* back to the GPU after twenty frames below 1500 pixel operations (the pause starts at 2000), or after
             * three seconds whatever they are: a screen that is busy but not busy enough to be a pixel effect would
             * otherwise keep the slow CPU drawing for good. If it is still busy, two frames pause it again. */
            sv->calm_streak = v->cpu_px_ops < 1500 ? sv->calm_streak + 1 : 0;
            if (sv->paused_frames == 0 || sv->paused_frames % 60 == 0)
                gpu_video_log("GPU path: paused, %d pixel operations in this frame", v->cpu_px_ops);
            v->cpu_px_ops = 0;
            sv->paused_frames++;
            if (sv->calm_streak >= 20 || sv->paused_frames >= 180) {
                gpu_video_resume(sv->gpu, v);
                sv->calm_streak = 0;
                sv->paused_frames = 0;
            }
        }
    }
    if (sv->gpu && gpu_video_suspended(sv->gpu)) {
        /* the CPU draws and converts: the frame texture is ABGR, so red and blue change places on the way in */
        video_to_argb_area(v, sv->argb, src.x, src.y, src.w, src.h);
        size_t n = (size_t)src.w * src.h;
        if (sv->swap_cap < n) {
            uint32_t *p = realloc(sv->swap, n * sizeof *p);
            if (!p)
                return;
            sv->swap = p;
            sv->swap_cap = n;
        }
        for (size_t i = 0; i < n; i++) {
            uint32_t c = sv->argb[i];
            sv->swap[i] = (c & 0xFF00FF00u) | (c >> 16 & 0xFF) | (c & 0xFF) << 16;
        }
        show(sv, sv->swap, src, v->scale);
        return;
    }
    if (sv->gpu) {
        if (gpu_video_render(sv->gpu, v, sv->tex, src)) {
            if (sv->need_argb && !gpu_video_to_argb(sv->gpu, v, sv->argb, src))
                memset(sv->argb, 0, (size_t)src.w * src.h * sizeof *sv->argb);
            show(sv, NULL, src, v->scale);
            return;
        }
        gpu_off(sv, v);
    }
    /* convert and upload only the visible part */
    video_to_argb_area(v, sv->argb, src.x, src.y, src.w, src.h);
    show(sv, sv->argb, src, v->scale);
}

void sdl_video_present_argb(sdl_video *sv, const uint32_t *argb, SDL_Rect src, int scale)
{
    if (!sv->tex || src.x < 0 || src.y < 0 || src.x + src.w > sv->w || src.y + src.h > sv->h)
        return;
    if (sv->gpu) {                /* the texture holds R, G, B, A bytes: swap the picture's red and blue */
        size_t n = (size_t)src.w * src.h;
        uint32_t *tmp = malloc(n * sizeof *tmp);
        if (!tmp)
            return;
        for (size_t i = 0; i < n; i++)
            tmp[i] = (argb[i] & 0xFF00FF00u) | (argb[i] >> 16 & 0xFF) | (argb[i] & 0xFF) << 16;
        show(sv, tmp, src, scale);
        free(tmp);
        return;
    }
    show(sv, argb, src, scale);
}

void sdl_video_to_output(const sdl_video *sv, int ow, int oh, double x, double y, int *ox, int *oy)
{
    /* as SDL_RenderSetLogicalSize letterboxes the logical size into the output */
    double par = sv->shown_par > 0 ? sv->shown_par : 1.0;
    double lw = sv->shown.w * par, lh = sv->shown.h;
    double s = lw > 0 && lh > 0 ? (ow / lw < oh / lh ? ow / lw : oh / lh) : 1.0;
    if (sv->integer_scale && s >= 1)
        s = (int)s;
    double x0 = (ow - lw * s) / 2, y0 = (oh - lh * s) / 2;
    *ox = (int)(x0 + (x - sv->shown.x) * par * s);
    *oy = (int)(y0 + (y - sv->shown.y) * s);
}

/* Uploads src.w x src.h pixels of the view (at src in it) and shows them, with the scanlines and the overlay. */
static void show(sdl_video *sv, const uint32_t *argb, SDL_Rect src, int scale)
{
    double par = sv->shown_par > 0 ? sv->shown_par : 1.0;
    sv->shown = src;
    if (argb)
        SDL_UpdateTexture(sv->tex, &src, argb, src.w * (int)sizeof *argb);
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(sv->tex, sv->smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
#endif
    SDL_RenderSetIntegerScale(sv->ren, sv->integer_scale ? SDL_TRUE : SDL_FALSE);
    SDL_RenderSetLogicalSize(sv->ren, (int)(src.w * par + 0.5), src.h);
    SDL_SetRenderDrawColor(sv->ren, 0, 0, 0, 255);
    SDL_RenderClear(sv->ren);
    SDL_RenderCopy(sv->ren, sv->tex, &src, NULL);
    if (sv->scanlines) {                /* the lower part of every game row is dimmed */
        int lw = (int)(src.w * par + 0.5);
        SDL_SetRenderDrawBlendMode(sv->ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(sv->ren, 0, 0, 0, 120);
        for (int y = 0; y + scale <= src.h; y += scale) {
            int th = scale / 3 > 0 ? scale / 3 : 1;
            SDL_Rect line = {0, y + scale - th, lw, th};
            SDL_RenderFillRect(sv->ren, &line);
        }
        SDL_SetRenderDrawBlendMode(sv->ren, SDL_BLENDMODE_NONE);
    }
    if (sv->overlay) {
        int ow = 0, oh = 0;
        SDL_RenderSetLogicalSize(sv->ren, 0, 0);
        SDL_RenderSetIntegerScale(sv->ren, SDL_FALSE);
        SDL_GetRendererOutputSize(sv->ren, &ow, &oh);
        sv->overlay(sv->ren, ow, oh, sv->overlay_user);
    }
    if (sv->shot_path) {
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(sv->ren, &ow, &oh);
        SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
        if (surf) {
            if (SDL_RenderReadPixels(sv->ren, NULL, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch) == 0)
                SDL_SaveBMP(surf, sv->shot_path);
            SDL_FreeSurface(surf);
        }
        sv->shot_path = NULL;
    }
    SDL_RenderPresent(sv->ren);
}

void sdl_video_close(sdl_video *sv)
{
    if (sv->gpu)
        gpu_video_destroy(sv->gpu, sv->gpu_vid);
    if (sv->tex)
        SDL_DestroyTexture(sv->tex);
    if (sv->ren)
        SDL_DestroyRenderer(sv->ren);
    if (sv->win)
        SDL_DestroyWindow(sv->win);
    free(sv->argb);
    free(sv->swap);
    memset(sv, 0, sizeof *sv);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
