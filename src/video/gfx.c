#include "gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>

#include "assets/artsrc.h"
#include "assets/png_read.h"

static void warnf(gfx_cache *gc, const char *msg)
{
    if (gc->warn)
        gc->warn(msg, gc->warn_user);
}

void gfx_image_from_img(gfx_image *gi, const img_lib *lib, const img_image *img)
{
    memset(gi, 0, sizeof *gi);
    gi->img = img;
    gi->pal = img->palette != IMG_NONE ? &lib->palettes[img->palette] : NULL;
    gi->mask = img_palette_mask(gi->pal);
    gi->hi_state = -1;
}

int gfx_cache_init(gfx_cache *gc, const catalog *cat, catalog_warn_fn warn, void *warn_user)
{
    memset(gc, 0, sizeof *gc);
    gc->cat = cat;
    gc->use_overrides = 1;
    gc->warn = warn;
    gc->warn_user = warn_user;
    gc->images = calloc(cat->nimages ? (size_t)cat->nimages : 1, sizeof *gc->images);
    if (!gc->images)
        return 0;
    for (int i = 0; i < cat->nimages; i++) {
        const cat_image *ci = &cat->images[i];
        gfx_image_from_img(&gc->images[i], catalog_lib(cat, ci), catalog_image(cat, ci));
        gc->images[i].entry = ci;
        gc->images[i].hi_state = 0;
    }
    return 1;
}

void gfx_cache_reload(gfx_cache *gc)
{
    gc->epoch++;                 /* jobs still out belong to the old state: their results are dropped */
    gc->npending = 0;
    gfx_prefetch_clear(gc);
    for (int i = 0; i < gc->cat->nimages; i++) {
        gfx_image_free_override(&gc->images[i]);
        gc->images[i].hi_state = 0;
    }
}

void gfx_cache_free(gfx_cache *gc)
{
    if (gc->images)
        gfx_cache_reload(gc);
    free(gc->images);
    free(gc->plan);
    memset(gc, 0, sizeof *gc);
}

void gfx_image_free_override(gfx_image *gi)
{
    free(gi->hi);
    free(gi->hi_detail);
    gi->hi = NULL;
    gi->hi_detail = NULL;
    gi->hi_factor = 0;
    gi->hi_state = -1;
    gi->gen++;
}

uint32_t gfx_apply_detail(uint32_t argb, uint32_t d)
{
    int r = (int)(argb >> 16 & 255), g = (int)(argb >> 8 & 255), b = (int)(argb & 255);
    int l = (r * 299 + g * 587 + b * 114) / 1000;
    int l0 = (int)(d >> 24);
    int ratio = l >= l0 ? 256 : l * 256 / l0; /* dimmed entries dim the detail too */
    r += (int8_t)(d & 255) * ratio / 256;
    g += (int8_t)(d >> 8 & 255) * ratio / 256;
    b += (int8_t)(d >> 16 & 255) * ratio / 256;
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    return (argb & 0xFF000000u) | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

/* Maps RGBA art onto the image's own palette (see gfx.h). Reads the image, touches nothing else. */
static int rgba_to_detail(const gfx_image *gi, png_indexed *png, gfx_hi *out, char *err, size_t err_len)
{
    const img_image *im = gi->img;
    const img_palette *pal = gi->pal;
    uint8_t cand[256];
    uint32_t cand_rgb[256];
    int n = 0;
    if (pal) {
        uint8_t seen[256] = {0};
        for (int y = 0; y < im->height; y++)
            for (int x = 0; x < im->width; x++) {
                unsigned p = im->pixels[(size_t)y * im->stride + x] & gi->mask;
                if (p && p < pal->ncolors && !seen[p]) {
                    seen[p] = 1;
                    cand[n] = (uint8_t)p;
                    cand_rgb[n++] = img_color_argb(pal->colors[p]);
                }
            }
    }
    if (n == 0) {
        snprintf(err, err_len, "true-color override needs an image with palette colors");
        return 0;
    }
    size_t np = (size_t)png->width * png->height;
    uint8_t *idx = malloc(np);
    uint32_t *det = malloc(np * sizeof *det);
    if (!idx || !det) {
        free(idx);
        free(det);
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    for (size_t i = 0; i < np; i++) {
        const uint8_t *px = png->rgba + 4 * i;
        if (px[3] < 128) {
            idx[i] = 0;
            det[i] = 0;
            continue;
        }
        int best = 0, bd = 1 << 30;
        for (int c = 0; c < n; c++) {
            int dr = px[0] - (int)(cand_rgb[c] >> 16 & 255);
            int dg = px[1] - (int)(cand_rgb[c] >> 8 & 255);
            int db = px[2] - (int)(cand_rgb[c] & 255);
            int d = 2 * dr * dr + 4 * dg * dg + 3 * db * db;
            if (d < bd) {
                bd = d;
                best = c;
            }
        }
        uint32_t pc = cand_rgb[best];
        int pr = (int)(pc >> 16 & 255), pg = (int)(pc >> 8 & 255), pb = (int)(pc & 255);
        int rr = px[0] - pr, rg = px[1] - pg, rb = px[2] - pb;
        rr = rr < -127 ? -127 : rr > 127 ? 127 : rr;
        rg = rg < -127 ? -127 : rg > 127 ? 127 : rg;
        rb = rb < -127 ? -127 : rb > 127 ? 127 : rb;
        int luma = (pr * 299 + pg * 587 + pb * 114) / 1000;
        idx[i] = cand[best];
        det[i] = GFX_DETAIL_PACK(rr, rg, rb, luma ? luma : 1);
    }
    free(png->rgba);
    png->rgba = NULL;
    out->hi = idx;
    out->detail = det;
    return 1;
}

/* Checks a decoded PNG against the image and turns it into what the renderer keeps. Takes the PNG's buffers on
 * success. Reads the image only, so it may run on a worker thread. */
static int gfx_prepare(const gfx_image *gi, png_indexed *png, gfx_hi *out, char *err, size_t err_len)
{
    const img_image *im = gi->img;
    int k = im->width ? png->width / im->width : 0;
    memset(out, 0, sizeof *out);
    if (k < 1 || png->width != im->width * k || png->height != im->height * k) {
        snprintf(err, err_len, "%dx%d is not an integer multiple of %dx%d", png->width,
                 png->height, im->width, im->height);
        return 0;
    }
    if (png->rgba) {
        if (!rgba_to_detail(gi, png, out, err, err_len))
            return 0;
    } else {
        out->hi = png->indices;
        png->indices = NULL;
    }
    out->k = k;
    return 1;
}

void gfx_hi_free(gfx_hi *hi)
{
    free(hi->hi);
    free(hi->detail);
    memset(hi, 0, sizeof *hi);
}

/* Makes the prepared override the image's hi-res source. */
static void gfx_install(gfx_image *gi, gfx_hi *hi)
{
    free(gi->hi);
    free(gi->hi_detail);
    gi->hi = hi->hi;
    gi->hi_detail = hi->detail;
    gi->hi_factor = hi->k;
    gi->hi_state = 1;
    gi->pend_prio = 0;
    memset(hi, 0, sizeof *hi);
    gi->gen++;
}

int gfx_attach_override(gfx_image *gi, png_indexed *png, char *err, size_t err_len)
{
    gfx_hi hi;
    if (!gfx_prepare(gi, png, &hi, err, err_len))
        return 0;
    gfx_install(gi, &hi);
    return 1;
}

static double load_ms_total;
static long load_count;

/* the processor time of the process, in ms: standard C, and what a decode on this thread costs */
static double now_ms(void)
{
    return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC;
}

void gfx_load_stats(long *count, double *ms)
{
    /* (the loads of the worker threads are counted per cache, gfx_cache_load_stats) */
    *count = load_count;
    *ms = load_ms_total;
    load_count = 0;
    load_ms_total = 0;
}

void gfx_cache_load_stats(gfx_cache *gc, long *count, double *ms, int *pending)
{
    gfx_load_stats(count, ms);
    *count += gc->async_count;
    *ms += gc->async_ms;
    *pending = gc->npending;
    gc->async_count = 0;
    gc->async_ms = 0;
}

static void load_override_raw(gfx_cache *gc, gfx_image *gi);

static void load_override(gfx_cache *gc, gfx_image *gi)
{
    double t0 = now_ms();
    load_override_raw(gc, gi);
    load_ms_total += now_ms() - t0;
    load_count++;
}

static void load_override_raw(gfx_cache *gc, gfx_image *gi)
{
    gi->hi_state = -1;
    char path[IMG_NAME_MAX + 8], err[512], msg[1700];
    if (!gc->use_overrides || !gi->entry || !gi->entry->has_override || !gc->cat->override_dir[0])
        return;
    snprintf(path, sizeof path, "%s.png", gi->entry->name);

    png_indexed png;
    if (!art_load_png(gc->cat->override_dir, path, &png, err, sizeof err)) {
        snprintf(msg, sizeof msg, "override ignored: %s", err);
        warnf(gc, msg);
        return;
    }
    if (!gfx_attach_override(gi, &png, err, sizeof err)) {
        snprintf(msg, sizeof msg, "override ignored: %s: %s", path, err);
        warnf(gc, msg);
    }
    png_indexed_free(&png);
}

/* The first request of an image's override: locates the file on this thread (the zip index is shared state) and
 * queues the job. Returns 0 when the platform did not take it: the image stays "not tried" and is asked again. */
static int request(gfx_cache *gc, gfx_image *gi, int prefetch)
{
    char err[512], msg[1700], file[IMG_NAME_MAX + 8];
    gfx_job job;
    if (!gc->use_overrides || !gi->entry || !gi->entry->has_override || !gc->cat->override_dir[0]) {
        gi->hi_state = -1;
        return 1;
    }
    snprintf(file, sizeof file, "%s.png", gi->entry->name);
    memset(&job, 0, sizeof job);
    job.gc = gc;
    job.gi = gi;
    job.epoch = gc->epoch;
    job.prefetch = prefetch;
    if (!art_locate(gc->cat->override_dir, file, &job.ref, err, sizeof err)) {
        snprintf(msg, sizeof msg, "override ignored: %s", err);
        warnf(gc, msg);
        gi->hi_state = -1;
        return 1;
    }
    if (!gc->submit(gc->submit_user, &job))
        return 0;
    gi->hi_state = 2;
    gi->pend_prio = prefetch ? 1 : 2;
    gc->npending++;
    return 1;
}

const gfx_image *gfx_get(gfx_cache *gc, const cat_image *ci)
{
    gfx_image *gi = &gc->images[ci - gc->cat->images];
    gi->last_use = gc->clock;
    if (gi->hi_state == 0) {
        if (!ci->has_override)
            gi->hi_state = -1;
        else if (gc->submit)
            request(gc, gi, 0);
        else
            load_override(gc, gi);
    } else if (gi->hi_state == 2 && gi->pend_prio < 2) {
        /* asked for ahead of time and now drawn: the platform moves it to the front */
        gfx_job job;
        char err[512];
        char file[IMG_NAME_MAX + 8];
        snprintf(file, sizeof file, "%s.png", gi->entry->name);
        memset(&job, 0, sizeof job);
        job.gc = gc;
        job.gi = gi;
        job.epoch = gc->epoch;
        if (art_locate(gc->cat->override_dir, file, &job.ref, err, sizeof err) && gc->submit(gc->submit_user, &job))
            gi->pend_prio = 2;
    }
    return gi;
}

/* ---- asynchronous loading ---- */

void gfx_cache_set_async(gfx_cache *gc, gfx_submit_fn submit, void *user, long prefetch_cap)
{
    if (gc->submit && !submit) {     /* pending images go back to "not tried", running jobs are dropped */
        gc->epoch++;
        gc->npending = 0;
        gfx_prefetch_clear(gc);
        for (int i = 0; i < gc->cat->nimages; i++)
            if (gc->images[i].hi_state == 2) {
                gc->images[i].hi_state = 0;
                gc->images[i].pend_prio = 0;
            }
    }
    gc->submit = submit;
    gc->submit_user = user;
    gc->prefetch_cap = prefetch_cap;
}

int gfx_job_run(const gfx_job *job, gfx_hi *out, char *err, size_t err_len)
{
    png_indexed png;
    memset(out, 0, sizeof *out);
    if (!art_read(&job->ref, &png, err, err_len))
        return 0;
    int ok = gfx_prepare(job->gi, &png, out, err, err_len);
    png_indexed_free(&png);
    return ok;
}

void gfx_job_done(gfx_cache *gc, const gfx_job *job, gfx_hi *hi, const char *err, double ms)
{
    gfx_image *gi = job->gi;
    if (job->epoch != gc->epoch || gi->hi_state != 2) {   /* the cache was reloaded meanwhile */
        if (hi)
            gfx_hi_free(hi);
        gc->async_dropped++;
        return;
    }
    gc->npending--;
    gc->async_count++;
    gc->async_ms += ms;
    if (!hi) {
        char msg[1700];
        snprintf(msg, sizeof msg, "override ignored: %s: %s", gi->entry ? gi->entry->name : "?", err ? err : "?");
        warnf(gc, msg);
        gi->hi_state = -1;
        gi->pend_prio = 0;
        return;
    }
    int ahead = gi->pend_prio == 1;
    if (ahead) {                  /* still nobody asked for it: counts against the prefetch budget */
        size_t px = (size_t)gi->img->width * gi->img->height * (size_t)hi->k * (size_t)hi->k;
        gc->prefetch_bytes += (long)(px * (hi->detail ? 5 : 1));
    }
    gfx_install(gi, hi);
    /* a prefetched image waits for its wrestler to come in: kept for a minute before it counts as unused */
    gi->last_use = ahead ? gc->clock + 3600 : gc->clock;
}

void gfx_cache_tick(gfx_cache *gc)
{
    gc->clock++;
}

static size_t image_bytes(const gfx_image *gi)
{
    if (gi->hi_state != 1 || !gi->hi)
        return 0;
    size_t px = (size_t)gi->img->width * gi->img->height * (size_t)gi->hi_factor * (size_t)gi->hi_factor;
    return px * (gi->hi_detail ? 5 : 1);
}

size_t gfx_cache_resident(const gfx_cache *gc)
{
    size_t n = 0;
    for (int i = 0; i < gc->cat->nimages; i++)
        n += image_bytes(&gc->images[i]);
    return n;
}

typedef struct {
    gfx_image *gi;
    int age;
} evict_cand;

static int evict_cmp(const void *a, const void *b)
{
    const evict_cand *x = a, *y = b;
    return x->age < y->age ? 1 : x->age > y->age ? -1 : 0;     /* the oldest first */
}

int gfx_cache_evict(gfx_cache *gc, size_t budget, int min_age, gfx_image **out, int max)
{
    size_t used = 0;
    int n = 0, nc = 0;
    evict_cand *cand;
    if (max <= 0)
        return 0;
    for (int i = 0; i < gc->cat->nimages; i++)
        used += image_bytes(&gc->images[i]);
    if (used <= budget)
        return 0;
    cand = malloc((size_t)gc->cat->nimages * sizeof *cand);
    if (!cand)
        return 0;
    for (int i = 0; i < gc->cat->nimages; i++) {
        gfx_image *gi = &gc->images[i];
        int age = gc->clock - gi->last_use;
        if (gi->hi_state == 1 && gi->hi && age >= min_age) {
            cand[nc].gi = gi;
            cand[nc].age = age;
            nc++;
        }
    }
    qsort(cand, (size_t)nc, sizeof *cand, evict_cmp);
    for (int i = 0; i < nc && n < max && used > budget; i++) {
        gfx_image *gi = cand[i].gi;
        used -= image_bytes(gi);
        free(gi->hi);
        free(gi->hi_detail);
        gi->hi = NULL;
        gi->hi_detail = NULL;
        gi->hi_factor = 0;
        gi->hi_state = 0;
        gi->gen++;
        out[n++] = gi;
    }
    free(cand);
    return n;
}

int gfx_pending_count(const gfx_cache *gc)
{
    return gc->npending;
}

void gfx_prefetch_add(gfx_cache *gc, const cat_image *ci)
{
    if (!gc->submit || !ci->has_override || gc->images[ci - gc->cat->images].hi_state != 0)
        return;
    if (gc->plan_n == gc->plan_cap) {
        int nc = gc->plan_cap ? gc->plan_cap * 2 : 1024;
        int *np = realloc(gc->plan, (size_t)nc * sizeof *np);
        if (!np)
            return;
        gc->plan = np;
        gc->plan_cap = nc;
    }
    gc->plan[gc->plan_n++] = (int)(ci - gc->cat->images);
}

void gfx_prefetch_clear(gfx_cache *gc)
{
    gc->plan_n = gc->plan_pos = 0;
}

void gfx_prefetch_pump(gfx_cache *gc)
{
    int budget = 8;           /* spread the file lookups over the frames */
    if (!gc->submit)
        return;
    while (gc->plan_pos < gc->plan_n && budget > 0) {
        if (gc->prefetch_cap && gc->prefetch_bytes >= gc->prefetch_cap) {
            gc->plan_pos = gc->plan_n;      /* the budget is used up: the rest is loaded when the game draws it */
            break;
        }
        gfx_image *gi = &gc->images[gc->plan[gc->plan_pos]];
        if (gi->hi_state == 0) {
            if (!request(gc, gi, 1))
                return;                     /* the queue is full: try again next frame */
            budget--;
        }
        gc->plan_pos++;
    }
    if (gc->plan_pos >= gc->plan_n)
        gfx_prefetch_clear(gc);
}

const gfx_image *gfx_find(gfx_cache *gc, const char *label)
{
    const cat_image *ci = catalog_find(gc->cat, label);
    return ci ? gfx_get(gc, ci) : NULL;
}
