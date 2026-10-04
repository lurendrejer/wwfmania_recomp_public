/*
 * The optional GPU path (docs/VIDEO.md, "GPU path"): the blitter's work and the palette lookup done with
 * OpenGL ES 2 instead of on the CPU. Only this file and sdl_video.c know about it; the game and
 * src/video see a video_sink (video.h).
 *
 * It runs on the GL ES 2 context of the SDL renderer (SDL_HINT_RENDER_DRIVER=opengles2), so the window,
 * the scaling, the scanlines and the overlays stay what sdl_video.c always did: the GPU only fills the
 * renderer's frame texture, in place of the CPU converting the framebuffer and uploading it.
 */
#ifndef WWF_GPU_VIDEO_H
#define WWF_GPU_VIDEO_H

#include <SDL.h>

#include "video/gfx.h"
#include "video/video.h"

typedef struct gpu_video gpu_video;

/* Sets up the GPU path on `ren`'s GL ES 2 context and installs it as v's sink (v keeps what it holds on the
 * CPU and hands it over at the next present). Returns NULL, with the reason in err, when this machine
 * cannot do it (no ES 2 context, no highp floats in fragment shaders, a texture limit too small for the
 * framebuffer) or when the built-in comparison against the CPU path fails. */
gpu_video *gpu_video_create(SDL_Renderer *ren, video *v, char *err, size_t err_len);

/* Reads the picture back to the CPU, removes the sink from v and frees everything. */
void gpu_video_destroy(gpu_video *g, video *v);

/* Renders the part src (framebuffer pixels from the top left of the displayed view) into the same part of
 * `tex`, an SDL_PIXELFORMAT_ABGR8888 SDL_TEXTUREACCESS_TARGET texture of the renderer. 0 if the GPU path
 * failed (it has then removed itself; the CPU path takes over). */
int gpu_video_render(gpu_video *g, video *v, SDL_Texture *tex, SDL_Rect src);

/* The same part as 0xAARRGGBB pixels on the CPU (the image inspector). */
int gpu_video_to_argb(gpu_video *g, video *v, uint32_t *out, SDL_Rect src);

/* The GL context was lost and made again (Android, SDL_RENDER_DEVICE_RESET): everything is made anew; what
 * the GPU held of the picture is gone. */
void gpu_video_context_lost(gpu_video *g);

/* Draws random blits, fills and palettes on a CPU video and on one with the sink and compares the
 * framebuffers, the detail planes and the converted pictures bit for bit. 1 if all equal; otherwise 0 with
 * the first difference in msg. `scale` is the render scale of the scratch videos. */
int gpu_video_selftest(gpu_video *g, int rounds, int scale, unsigned seed, char *msg, size_t msg_len);

/* printf-style message to stderr (logcat on Android). */
void gpu_video_log(const char *fmt, ...);

/* Timing of what happened since the last call (CPU time in the sink, the GL calls, texture uploads, the number of
 * blits and read backs), as one line; the counters start again. With the profile on, glFinish is called after the
 * list and the present pass and the GPU's own time is added (this slows the game a little: a test aid). */
void gpu_video_stats(gpu_video *g, char *out, size_t n);
void gpu_video_set_profile(gpu_video *g, int on);
/* Async compute (off by default; the GPU path behaves as before without it). The textures of an override are not made
 * the first time the game draws the image, in the middle of the frame, but from a queue: `budget_ms` of work per
 * frame (at least one image), before the picture is drawn. Until an image's textures are there the game draws it from
 * the original pixels, as it does while the art is being read from disk. The work stays on the thread that owns the GL
 * context; what changes is that it is spread over frames and never lands in one. 0 or less budget = 2 ms. */
void gpu_video_set_async(gpu_video *g, int on, double budget_ms);
/* Precache: makes the textures of the loaded overrides among imgs[0..n) (gfx_cache.images, the images that have an
 * override) that are not on the GPU yet, for about `budget_ms` of work (at least one image when there is one). Meant to be
 * called every frame while the art is being read. Reports the number of images with an override (`total`), of those
 * loaded into memory (`loaded`) and of those with their textures on the GPU (`on_gpu`, which counts what cannot be a
 * texture too, see sink_blit_raw). Used with or without async compute. */
void gpu_video_precache(gpu_video *g, gfx_image *imgs, int n, double budget_ms, int *total, int *loaded, int *on_gpu);
/* Frees the GPU's textures for the override of these images (gfx_cache_evict dropped them on the CPU). */
void gpu_video_release_images(gpu_video *g, gfx_image **imgs, int n);

/* What the sink saw since the last call: single-pixel fills (the pixel writes of the original's pixel-moving
 * effects) and full read backs. Used to pause the GPU path while such an effect runs. */
void gpu_video_activity(gpu_video *g, long *fills, long *readbacks);
/* Pauses: reads the picture back once and hands the drawing to the CPU (video.c) until gpu_video_resume, which
 * makes the CPU copy the one that counts again so that the next present uploads it. Both do nothing when it is
 * already so. The picture is the same before and after. */
void gpu_video_suspend(gpu_video *g, video *v);
void gpu_video_resume(gpu_video *g, video *v);
int gpu_video_suspended(const gpu_video *g);
/* Read backs and single pixel reads since the start (for the on-screen debug info; gpu_video_stats resets its own). */
void gpu_video_totals(const gpu_video *g, long *readbacks, long *pixel_reads);

/* Who caused the read backs since the last call: up to `max` (video.sync_tag, count) pairs; the list starts again. */
int gpu_video_readback_callers(gpu_video *g, uint32_t *tag, long *count, int max);

/* The GL strings, for the log. */
const char *gpu_video_info(const gpu_video *g);

#endif
