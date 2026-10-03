/* SDL2 presentation of a video framebuffer (window, texture upload). */
#ifndef WWF_SDL_VIDEO_H
#define WWF_SDL_VIDEO_H

#include <SDL.h>

#include "video/video.h"

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    uint32_t *argb;
    int w, h;              /* size of tex/argb: the whole rendered view at scale */
    double zoom;           /* 1 = the planned view (base_w x base_h); > 1 magnifies the middle, < 1 shows more */
    double zoom_dyn;       /* > 0: the zoom to show while the dynamic zoom is on (main_game.c), instead of `zoom` */
    int zoom_off;          /* 1: show the planned view (zoom 1) whatever `zoom` is: the game's menus and screens */
    double zmin;           /* smallest zoom (the rendered view is big enough for it), <= 1 */
    int base_w, base_h;    /* the view at zoom 1, in game pixels (0 = the whole rendered view) */
    int vis_pad;           /* extra original pixels per side visible at the last present */
    /* Called after the game is drawn, before it is shown, with the window's
     * output size in pixels (no logical size is set). */
    void (*overlay)(SDL_Renderer *ren, int w, int h, void *user);
    void *overlay_user;
    const char *shot_path; /* if set: save the next frame as a BMP (test aid), then cleared */
    /* display options, may change at any time */
    int smooth;            /* linear filtering (default) or sharp pixels */
    int integer_scale;     /* whole number magnification only */
    int crt_aspect;        /* the arcade monitor's 4:3 picture: pixels a little narrower than tall */
    int scanlines;         /* dark line between the game's rows */
    int vis_pad_y;         /* extra rows above and below visible at the last present */
    /* the last present: the part of the rendered view shown (argb/texture pixels) and the pixel aspect */
    SDL_Rect shown;
    double shown_par;
    /* the GPU path (gpu_video.h): NULL = off. need_argb: keep `argb` filled with the shown picture (the
     * image inspector) even though the GPU does the conversion. */
    struct gpu_video *gpu;
    video *gpu_vid;
    int need_argb;
    int paused_frames;                /* frames the GPU path has been paused */
    int rb_streak, calm_streak;       /* frames in a row with pixel-moving activity / without it (GPU pause policy) */
    uint32_t *swap;                   /* scratch for showing a CPU picture while the GPU path is paused */
    size_t swap_cap;
} sdl_video;

/* Opens a resizable window showing v's framebuffer. Returns 0 on failure. */
/* Uses this window (already created, for example for a progress bar) instead of making one, the next time
 * sdl_video_open runs. Android has one window only: destroying it and making another crashes. */
void sdl_video_use_window(SDL_Window *win);

int sdl_video_open(sdl_video *sv, const char *title, const video *v);

/* Asks the next sdl_video_open for an OpenGL ES 2 renderer, which the GPU path needs (falls back to SDL's
 * choice, with a message, when there is none). */
void sdl_video_request_gpu(int on);

/* Moves the drawing and the palette conversion of v to the GPU. Call after sdl_video_open, before the first
 * frame. Returns 0 and says why in `why` when the machine cannot do it; everything then stays on the CPU. */
int sdl_video_attach_gpu(sdl_video *sv, video *v, char *why, size_t why_len);

/* The GL context was lost and made again (SDL_RENDER_DEVICE_RESET). */
void sdl_video_gpu_reset(sdl_video *sv);

/* Zoom in (factor > 1) or out. Zoom 1 shows the planned view (base_w x base_h,
 * with the window's shape); larger zoom shows the middle part, magnified;
 * smaller shows more, down to what was rendered.
 * Clamped to zmin..4. */
void sdl_video_zoom(sdl_video *sv, double factor);
void sdl_video_zoom_reset(sdl_video *sv);

/* Extra original pixels per side beyond the 400 pixel screen that were
 * visible at the last present (0 if the screen is cropped). */
int sdl_video_visible_pad(const sdl_video *sv);
/* The same for rows above and below the 254 row screen. */
int sdl_video_visible_pad_y(const sdl_video *sv);

/* Converts the framebuffer through color RAM and shows it. */
void sdl_video_present(sdl_video *sv, video *v);

/* Shows a picture kept from an earlier present instead (the image inspector): `argb` holds src.w x src.h pixels
 * of the rendered view, `src` where they were in it. */
void sdl_video_present_argb(sdl_video *sv, const uint32_t *argb, SDL_Rect src, int scale);

/* Where a point of the rendered view (texture pixels) lands in the window's output (pixels), as shown at the last
 * present; for overlays. */
void sdl_video_to_output(const sdl_video *sv, int ow, int oh, double x, double y, int *ox, int *oy);

void sdl_video_close(sdl_video *sv);

#endif
