/*
 * Drawable images for the blitter: the original IMG pixels plus, when one
 * exists and overrides are enabled, a high-resolution indexed override.
 *
 * Overrides live in the catalog's override directory as <LABEL>.png, at an
 * integer multiple k of the original size on both axes
 * (docs/ASSET_OVERRIDES.md). They are loaded lazily on first use. Two kinds:
 *
 *   - indexed (or 8-bit grayscale): the values are the original palette
 *     indices;
 *   - RGB/RGBA true color: each opaque pixel becomes the nearest palette
 *     color the original image uses, plus a per-pixel detail word that holds
 *     the remaining difference. The renderer adds the detail back when it
 *     converts to RGB, scaled by how much the color RAM entry has dimmed, so
 *     fades and recolors still work while the art keeps more than 64 colors.
 */
#ifndef WWF_GFX_H
#define WWF_GFX_H

#include <stdint.h>

#include "assets/artsrc.h"
#include "assets/catalog.h"
#include "assets/png_read.h"

/*
 * Detail word: bits 0-7 red, 8-15 green, 16-23 blue residual (signed, in
 * 8-bit color units), bits 24-31 luma of the original palette color (never 0
 * for a pixel that carries detail).
 */
#define GFX_DETAIL_PACK(r, g, b, l) \
    ((uint32_t)(uint8_t)(r) | (uint32_t)(uint8_t)(g) << 8 | (uint32_t)(uint8_t)(b) << 16 | \
     (uint32_t)(l) << 24)

typedef struct {
    const cat_image *entry;
    const img_image *img;
    uint8_t mask;          /* img_palette_mask of the image's palette */
    const img_palette *pal; /* the image's own palette, or NULL */

    int hi_state;          /* 0 = not tried, 1 = loaded, -1 = none or invalid, 2 = being read by a worker
                              (gfx_cache_set_async): drawn like -1 (the original pixels) until it becomes 1 */
    int pend_prio;         /* while hi_state is 2: 1 = asked for ahead of time (prefetch), 2 = the game is drawing it */
    int hi_factor;         /* k: override pixels per original pixel */
    uint8_t *hi;           /* (width*k) x (height*k) indices */
    uint32_t *hi_detail;   /* same size, detail words; NULL for indexed overrides */
    int blank;             /* every pixel is zero (the DMA of a zero block): drawn as a plain fill */
    /* Kept for the video sink (video.h): its textures for this image, valid while `sink_gen` equals `gen`.
     * `gen` changes whenever the override is attached or dropped. */
    int last_use;          /* gfx_cache.clock when it was last asked for (the least recently used go first, gfx_cache_evict);
                              ahead of the clock for a prefetched image that nobody has drawn yet */
    unsigned gen;
    unsigned sink_gen;
    unsigned sink_owner;
    uint32_t sink_tex[3];  /* pixels, override pixels, override detail */
} gfx_image;

/* A decoded, prepared override (indices, and detail words for true colour art), not yet attached to an image. */
typedef struct {
    uint8_t *hi;
    uint32_t *detail;
    int k;
} gfx_hi;

struct gfx_cache;

/* One asynchronous load (see gfx_cache_set_async). Plain data: the platform may copy it. */
typedef struct {
    struct gfx_cache *gc;
    gfx_image *gi;
    unsigned epoch;        /* gfx_cache.epoch when it was made: a job from before a reload is dropped */
    int prefetch;          /* 1 = nobody is waiting for it yet */
    art_ref ref;           /* where the file is */
} gfx_job;

/* Asks the platform to run `gfx_job_run` for the job on a worker thread and to hand the result back with
 * `gfx_job_done` on the thread that calls gfx_get. Called on that (the main) thread. Returns 0 when it cannot take
 * the job (a full prefetch queue); a request for an image that is already queued or running must not be queued
 * twice, and with job->prefetch 0 it moves a queued prefetch job to the front (the game wants it now). */
typedef int (*gfx_submit_fn)(void *user, const gfx_job *job);

typedef struct gfx_cache {
    const catalog *cat;
    gfx_image *images;     /* parallel to cat->images */
    int use_overrides;
    catalog_warn_fn warn;
    void *warn_user;

    /* asynchronous loading, off (submit NULL) unless the platform turns it on: everything below is touched on the
     * main thread only */
    gfx_submit_fn submit;
    void *submit_user;
    unsigned epoch;          /* bumped by a reload and by turning async off */
    int npending;            /* images in state 2 */
    long prefetch_bytes;     /* hi-res bytes the prefetched images took so far */
    long prefetch_cap;       /* no more prefetch beyond this (bytes), see gfx_cache_set_async */
    int *plan;               /* catalog image indices to prefetch, in priority order */
    int plan_n, plan_cap, plan_pos;
    long async_count, async_dropped;   /* loads finished since the last gfx_load_stats, and results dropped as stale */
    double async_ms;
    int clock;               /* frames, see gfx_cache_tick */
} gfx_cache;

int gfx_cache_init(gfx_cache *gc, const catalog *cat, catalog_warn_fn warn, void *warn_user);
void gfx_cache_free(gfx_cache *gc);

/* Drops loaded overrides so they are re-read (after the catalog rescanned). */
void gfx_cache_reload(gfx_cache *gc);

/* Memory: a decoded override takes 1 byte per pixel (5 with the detail words of true colour art) and the video sink
 * keeps a copy of its own. Nothing was ever dropped, so a long game with many wrestlers (the Royal Rumble) grew until
 * the system killed the process. gfx_cache_tick() is called once a frame; gfx_cache_evict() frees overrides that were
 * not asked for during the last `min_age` frames, least recently used first, until the decoded bytes are at most
 * `budget`. They go back to state 0 (read again when the game draws them). The freed images are returned in
 * out[0..max) so that the platform can free what it keeps for them (the GPU path's textures); at most `max` are freed
 * per call. Returns how many. gfx_cache_resident() is the decoded bytes now. */
void gfx_cache_tick(gfx_cache *gc);
size_t gfx_cache_resident(const gfx_cache *gc);
int gfx_cache_evict(gfx_cache *gc, size_t budget, int min_age, gfx_image **out, int max);

/* Returns the drawable for a catalog entry, loading its override if needed. */
const gfx_image *gfx_get(gfx_cache *gc, const cat_image *ci);

/* Convenience: lookup by label; NULL if the label is unknown. */
const gfx_image *gfx_find(gfx_cache *gc, const char *label);

/*
 * Installs a decoded override (size k times the image; indexed or RGBA) as
 * the image's hi-res source. On success the PNG buffers are consumed; on
 * failure returns 0 with a message in err and the PNG is left to the caller.
 */
int gfx_attach_override(gfx_image *gi, png_indexed *png, char *err, size_t err_len);

/*
 * Asynchronous loading (docs/ASSET_OVERRIDES.md "Loading while playing"). The core has no threads: the platform
 * gives gfx_cache a submit function and runs the jobs.
 *
 * With it set, gfx_get no longer reads a PNG on the calling thread. On the first request of an image with an
 * override the image gets hi_state 2 and a job is submitted; until the result comes back (gfx_job_done) the image
 * is drawn from its original pixels, exactly as an image without an override. Nothing in the game's logic reads the
 * state: only the renderer looks at hi/hi_state/gen, and a finished load changes them the way a synchronous one
 * does (gen is bumped, so textures are rebuilt).
 *
 * gfx_job_run is the worker side: it reads, decodes and prepares (also the colour mapping of true colour art)
 * and touches no cache state; it only reads gi->img/pal/mask, which do not change while the machine runs.
 * gfx_job_done must be called on the main thread, never inside a draw. prefetch_cap is the hi-res bytes the
 * prefetch may fill (0 = no limit). submit NULL turns async off: pending images go back to "not tried" and
 * results of jobs still running are dropped when they arrive. Stop the workers before gfx_cache_free.
 */
void gfx_cache_set_async(gfx_cache *gc, gfx_submit_fn submit, void *user, long prefetch_cap);
int gfx_job_run(const gfx_job *job, gfx_hi *out, char *err, size_t err_len);
void gfx_hi_free(gfx_hi *hi);
/* hi NULL = the load failed (err says why). ms is what the worker spent on it. Frees hi. */
void gfx_job_done(gfx_cache *gc, const gfx_job *job, gfx_hi *hi, const char *err, double ms);
int gfx_pending_count(const gfx_cache *gc);

/* Prefetch: loads images before the game asks for them, by the same path, behind everything the game asks for.
 * gfx_prefetch_add appends an image to the plan (nothing happens for one that has no override or was tried);
 * gfx_prefetch_pump submits from the start of the plan while the submit function takes jobs and the byte cap
 * allows. Both do nothing while async is off. Call the pump once a frame. */
void gfx_prefetch_add(gfx_cache *gc, const cat_image *ci);
void gfx_prefetch_pump(gfx_cache *gc);
/* Drops the plan (images already queued stay queued). */
void gfx_prefetch_clear(gfx_cache *gc);

/* Frees the override buffers and marks the image as having none. */
void gfx_image_free_override(gfx_image *gi);

/* Applies a detail word to a 0xAARRGGBB color RAM color. */
uint32_t gfx_apply_detail(uint32_t argb, uint32_t detail);

/* High-resolution images loaded (read and decoded) for the first time since the last call, and the time that took
 * (a diagnostic for the log: on the game thread each is a stall; with async on these are the worker's loads and
 * the time the workers spent). The counters start again. */
void gfx_load_stats(long *count, double *ms);

/* As gfx_load_stats for this cache, plus the loads finished by the workers and the number of images still pending. */
void gfx_cache_load_stats(gfx_cache *gc, long *count, double *ms, int *pending);

/* A standalone image without catalog/override (tests, tools). */
void gfx_image_from_img(gfx_image *gi, const img_lib *lib, const img_image *img);

#endif
