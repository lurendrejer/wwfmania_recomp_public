/*
 * Worker threads for the high-resolution art (docs/ASSET_OVERRIDES.md "Loading while playing"): the platform half of
 * gfx_cache_set_async (src/video/gfx.h). The core library queues a job when the game first draws an image that has
 * an override; the workers read, decode and prepare the PNG; the main loop hands the results back with
 * gfx_async_poll. Until then the image is drawn from its original pixels.
 *
 * Two queues: the images the game is drawing now come before the prefetch jobs (gfx_prefetch_*). All
 * functions but the workers' own run on the main thread.
 */
#ifndef WWF_GFX_ASYNC_H
#define WWF_GFX_ASYNC_H

#include "video/gfx.h"

typedef struct gfx_async gfx_async;

/* Starts `nthreads` workers (1 to 8) and turns async loading on for gc. `prefetch_cap` is the most hi-res bytes the
 * prefetch fills (0 = no limit). NULL if the threads cannot be made (the cache then loads synchronously). */
gfx_async *gfx_async_start(gfx_cache *gc, int nthreads, long prefetch_cap);

/* Once per frame: attaches the finished images (cheap: it only swaps pointers) and feeds the prefetch plan. */
void gfx_async_poll(gfx_async *a);

/* Waits until every queued and running job and the prefetch plan are done and attached. Never needed while playing:
 * for the screenshot and test runs, which compare against synchronous loading. */
void gfx_async_wait_idle(gfx_async *a);

/* Jobs not started yet and jobs running (a diagnostic). */
void gfx_async_counts(gfx_async *a, int *queued, int *running);

/* Stops the workers (waits for the jobs running, at most one load each), drops what is queued and turns async off:
 * images still pending go back to "not tried". Call it before the cache is freed. */
void gfx_async_stop(gfx_async *a);

#endif
