/*
 * Asynchronous loading of the high-resolution art, the core half (src/video/gfx.c, gfx_cache_set_async): the jobs
 * are queued in an array here and run by the test itself, one at a time, so every order is exercised and the test
 * is deterministic. The real worker threads are tested in test_gfx_worker.
 *
 *   test_gfx_async <tmpdir>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx_fixture.h"
#include "video/video.h"

static int failures;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                                 \
        }                                                                               \
    } while (0)

static int warnings;
static void count_warn(const char *msg, void *user)
{
    (void)msg;
    (void)user;
    warnings++;
}

/* the "platform": a list of jobs that were submitted and not run yet */
typedef struct {
    gfx_job job[32];
    int n, submits, promotes, refuse_prefetch_over;
} fake;

static int fake_submit(void *user, const gfx_job *job)
{
    fake *f = user;
    f->submits++;
    for (int i = 0; i < f->n; i++)
        if (f->job[i].gi == job->gi && f->job[i].epoch == job->epoch) {   /* queued: only the priority may change */
            if (!job->prefetch && f->job[i].prefetch) {
                f->job[i].prefetch = 0;
                f->promotes++;
            }
            return 1;
        }
    if (job->prefetch && f->refuse_prefetch_over >= 0) {
        int np = 0;
        for (int i = 0; i < f->n; i++)
            np += f->job[i].prefetch;
        if (np >= f->refuse_prefetch_over)
            return 0;
    }
    f->job[f->n++] = *job;
    return 1;
}

/* runs and hands back the job of image `label` (the worker's part, then the main thread's) */
static int run_job(fake *f, const char *label)
{
    for (int i = 0; i < f->n; i++)
        if (!strcmp(f->job[i].gi->entry->name, label)) {
            gfx_job j = f->job[i];
            memmove(&f->job[i], &f->job[i + 1], (size_t)(f->n - i - 1) * sizeof f->job[0]);
            f->n--;
            gfx_hi hi;
            char err[256] = "";
            int ok = gfx_job_run(&j, &hi, err, sizeof err);
            gfx_job_done(j.gc, &j, ok ? &hi : NULL, err, 1.0);
            return 1;
        }
    return 0;
}

static int has_job(const fake *f, const char *label)
{
    for (int i = 0; i < f->n; i++)
        if (!strcmp(f->job[i].gi->entry->name, label))
            return 1;
    return 0;
}

static void draw(video *v, const gfx_image *gi)
{
    dma_blit b = {gi, 4, 3, DMA_WNZ, 0x0101, 0, 0, 0, 0, 0, 0, 0};
    video_dma(v, &b);
}

static int same_picture(const gfx_image *a, const gfx_image *b)
{
    video va, vb;
    int same;
    if (!video_init(&va, 2) || !video_init(&vb, 2))
        return 0;
    video_clear(&va, 0);
    video_clear(&vb, 0);
    draw(&va, a);
    draw(&vb, b);
    same = memcmp(va.fb, vb.fb, (size_t)va.w * va.h * sizeof *va.fb) == 0;
    video_free(&va);
    video_free(&vb);
    return same;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test_gfx_async <tmpdir>\n");
        return 2;
    }
    char dir[1100], ovr[1100];
    CHECK(fx_make(argv[1], "gfx_async", dir, sizeof dir, ovr, sizeof ovr));
    static const char *const lods[] = {"T.LOD", NULL};
    catalog cat;
    CHECK(catalog_open(&cat, dir, ovr, lods, NULL, NULL));
    CHECK(cat.nimages == FX_N);

    /* the reference: the synchronous cache (async is off unless a platform turns it on) */
    gfx_cache ref;
    CHECK(gfx_cache_init(&ref, &cat, NULL, NULL));
    CHECK(ref.submit == NULL);
    const gfx_image *r0 = gfx_find(&ref, "IM0"), *r1 = gfx_find(&ref, "IM1"), *r2 = gfx_find(&ref, "IM2");
    CHECK(r0->hi_state == 1 && r1->hi_state == 1 && r1->hi_detail && r2->hi_state == -1);
    CHECK(gfx_find(&ref, "IM3")->hi_state == -1);

    gfx_cache gc;
    fake f;
    memset(&f, 0, sizeof f);
    f.refuse_prefetch_over = -1;
    CHECK(gfx_cache_init(&gc, &cat, count_warn, NULL));
    gfx_cache_set_async(&gc, fake_submit, &f, 0);

    /* 1. the first request queues a job and the image is pending; asking again does not queue it twice */
    const gfx_image *g0 = gfx_find(&gc, "IM0");
    unsigned gen0 = g0->gen;
    CHECK(g0->hi_state == 2 && g0->hi == NULL && g0->hi_factor == 0);
    CHECK(f.n == 1 && f.submits == 1 && gc.npending == 1 && !f.job[0].prefetch);
    for (int i = 0; i < 100; i++)
        gfx_find(&gc, "IM0");
    CHECK(f.n == 1 && f.submits == 1);

    /* the pending image draws as the original: the same picture as an image without an override */
    gfx_image plain;
    gfx_image_from_img(&plain, catalog_lib(&cat, g0->entry), g0->img);
    CHECK(same_picture(g0, &plain));
    CHECK(!same_picture(r0, &plain));       /* (the override is not the same picture: the check means something) */

    /* 2. the result comes back: attached like a synchronous load (gen bumped), the same pixels, never loaded twice */
    CHECK(run_job(&f, "IM0"));
    CHECK(g0->hi_state == 1 && g0->hi_factor == 2 && g0->gen == gen0 + 1 && gc.npending == 0);
    CHECK(memcmp(g0->hi, r0->hi, 24) == 0 && g0->hi_detail == NULL);
    CHECK(same_picture(g0, r0));
    gfx_find(&gc, "IM0");
    CHECK(f.n == 0 && f.submits == 1 && g0->gen == gen0 + 1);

    /* 3. true colour: the colour mapping is part of the job; the same indices and detail as the synchronous load */
    const gfx_image *g1 = gfx_find(&gc, "IM1");
    CHECK(g1->hi_state == 2 && run_job(&f, "IM1"));
    CHECK(g1->hi_state == 1 && g1->hi_detail && memcmp(g1->hi, r1->hi, 24) == 0 &&
          memcmp(g1->hi_detail, r1->hi_detail, 24 * 4) == 0);

    /* 4. a bad file: one warning, no override, drawn from the original from then on, asked once */
    const gfx_image *g2 = gfx_find(&gc, "IM2");
    CHECK(g2->hi_state == 2 && warnings == 0 && run_job(&f, "IM2"));
    CHECK(g2->hi_state == -1 && g2->hi == NULL && warnings == 1);
    gfx_find(&gc, "IM2");
    CHECK(f.n == 0);

    /* 5. no override, or overrides off: no job at all */
    CHECK(gfx_find(&gc, "IM3")->hi_state == -1 && f.n == 0);

    /* 6. prefetch: behind the game's own requests, the queue is bounded, the budget too */
    f.refuse_prefetch_over = 1;
    gfx_prefetch_add(&gc, catalog_find(&cat, "IM4"));
    gfx_prefetch_add(&gc, catalog_find(&cat, "IM5"));
    gfx_prefetch_add(&gc, catalog_find(&cat, "IM0"));      /* loaded: not queued */
    gfx_prefetch_add(&gc, catalog_find(&cat, "IM3"));      /* no override: not queued */
    CHECK(gc.plan_n == 2);
    gfx_prefetch_pump(&gc);
    CHECK(f.n == 1 && has_job(&f, "IM4") && f.job[0].prefetch);
    CHECK(gc.images[4].hi_state == 2 && gc.images[4].pend_prio == 1);
    CHECK(gc.images[5].hi_state == 0 && gc.plan_n == 2);   /* refused: tried again at the next pump */
    /* the game draws IM4 while it is still queued as a prefetch: moved to the front, not queued twice */
    int submits = f.submits;
    gfx_find(&gc, "IM4");
    CHECK(f.n == 1 && f.promotes == 1 && !f.job[0].prefetch && gc.images[4].pend_prio == 2 && f.submits == submits + 1);
    gfx_find(&gc, "IM4");
    CHECK(f.submits == submits + 1);                       /* (once) */
    CHECK(run_job(&f, "IM4"));
    CHECK(gc.images[4].hi_state == 1 && gc.prefetch_bytes == 0);   /* it was asked for: not part of the budget */
    gfx_prefetch_pump(&gc);
    CHECK(has_job(&f, "IM5") && gc.plan_n == 0);
    CHECK(run_job(&f, "IM5"));
    CHECK(gc.images[5].hi_state == 1 && gc.prefetch_bytes == 24);

    /* the budget: past it the plan is dropped and the images wait for the game to ask */
    gfx_cache_reload(&gc);
    gc.prefetch_bytes = 0;
    gc.prefetch_cap = 1;
    f.n = 0;
    gfx_prefetch_add(&gc, catalog_find(&cat, "IM4"));
    gfx_prefetch_add(&gc, catalog_find(&cat, "IM5"));
    gfx_prefetch_pump(&gc);                                /* one queued, one refused */
    CHECK(f.n == 1 && gc.plan_n == 2);
    CHECK(run_job(&f, "IM4") && gc.prefetch_bytes == 24);
    gfx_prefetch_pump(&gc);
    CHECK(gc.plan_n == 0 && f.n == 0 && gc.images[5].hi_state == 0);
    gfx_find(&gc, "IM5");
    CHECK(has_job(&f, "IM5") && !f.job[0].prefetch);       /* the game's request still goes through */
    CHECK(run_job(&f, "IM5") && gc.images[5].hi_state == 1);
    gc.prefetch_cap = 0;

    /* 7. a reload with a job out: the image is "not tried" again; the old result is dropped, a new job is made */
    gfx_cache_reload(&gc);
    f.n = 0;
    const gfx_image *h0 = gfx_find(&gc, "IM0");
    CHECK(h0->hi_state == 2 && f.n == 1 && gc.npending == 1);
    gfx_job old = f.job[0];
    f.n = 0;
    gfx_cache_reload(&gc);
    CHECK(h0->hi_state == 0 && gc.npending == 0 && h0->hi == NULL);
    gfx_find(&gc, "IM0");
    CHECK(h0->hi_state == 2 && f.n == 1 && gc.npending == 1);
    {   /* the old job finishes late */
        gfx_hi hi;
        char err[256];
        CHECK(gfx_job_run(&old, &hi, err, sizeof err));
        long dropped = gc.async_dropped;
        gfx_job_done(&gc, &old, &hi, err, 1.0);
        CHECK(gc.async_dropped == dropped + 1 && h0->hi_state == 2 && h0->hi == NULL && gc.npending == 1);
    }
    CHECK(run_job(&f, "IM0") && h0->hi_state == 1 && gc.npending == 0);

    /* 8. turning async off: pending images are "not tried" and load synchronously from then on */
    gfx_cache_reload(&gc);
    f.n = 0;
    gfx_find(&gc, "IM4");
    CHECK(gc.images[4].hi_state == 2);
    gfx_find(&gc, "IM5");
    CHECK(run_job(&f, "IM5") && gc.images[5].hi_state == 1);
    gfx_cache_set_async(&gc, NULL, NULL, 0);
    CHECK(gc.images[4].hi_state == 0 && gc.images[5].hi_state == 1 && gc.npending == 0);
    CHECK(gfx_find(&gc, "IM4")->hi_state == 1);

    /* the statistics count what the platform finished, plus the pending number */
    {
        long n;
        double ms;
        int pend;
        gfx_cache_load_stats(&gc, &n, &ms, &pend);
        CHECK(n >= 6 && ms >= 6.0 && pend == 0);
        gfx_cache_load_stats(&gc, &n, &ms, &pend);
        CHECK(n == 0);
    }

    /* eviction (memory budget): the least recently asked-for go first, never the recent or the prefetched, the
     * freed images are reported, their state is 0 again and they are read again when drawn */
    {
        gfx_cache lc;
        gfx_image *gone[8];
        CHECK(gfx_cache_init(&lc, &cat, NULL, NULL));
        const cat_image *c0 = catalog_find(&cat, "IM0"), *c1 = catalog_find(&cat, "IM1"), *c4 = catalog_find(&cat, "IM4");
        gfx_get(&lc, c0);               /* loaded at clock 0 */
        for (int i = 0; i < 100; i++)
            gfx_cache_tick(&lc);
        gfx_get(&lc, c1);               /* at clock 100 */
        for (int i = 0; i < 100; i++)
            gfx_cache_tick(&lc);
        gfx_get(&lc, c4);               /* at clock 200 */
        gfx_cache_tick(&lc);
        size_t all = gfx_cache_resident(&lc);
        CHECK(all > 0 && lc.images[c0 - cat.images].hi_state == 1 && lc.images[c1 - cat.images].hi_state == 1);
        CHECK(gfx_cache_evict(&lc, all, 0, gone, 8) == 0);                  /* within the budget: nothing */
        CHECK(gfx_cache_evict(&lc, 0, 1000, gone, 8) == 0);                 /* everything is younger than 1000 frames */
        unsigned g0 = lc.images[c0 - cat.images].gen;
        int n = gfx_cache_evict(&lc, all - 1, 50, gone, 8);                 /* one must go: the oldest, IM0 */
        CHECK(n == 1 && gone[0] == &lc.images[c0 - cat.images]);
        CHECK(gone[0]->hi_state == 0 && gone[0]->hi == NULL && gone[0]->hi_factor == 0 && gone[0]->gen == g0 + 1);
        CHECK(lc.images[c1 - cat.images].hi_state == 1 && lc.images[c4 - cat.images].hi_state == 1);
        CHECK(gfx_cache_resident(&lc) <= all - 1);
        n = gfx_cache_evict(&lc, 0, 50, gone, 1);                           /* at most `max` per call: IM1 (IM4 is too young) */
        CHECK(n == 1 && gone[0] == &lc.images[c1 - cat.images]);
        CHECK(lc.images[c4 - cat.images].hi_state == 1);
        CHECK(gfx_get(&lc, c0)->hi_state == 1 && lc.images[c0 - cat.images].last_use == lc.clock);   /* read again, fresh */
        gfx_cache_free(&lc);
    }

    gfx_cache_free(&gc);
    gfx_cache_free(&ref);
    catalog_close(&cat);
    if (failures)
        fprintf(stderr, "%d failures\n", failures);
    else
        printf("gfx async: ok\n");
    return failures ? 1 : 0;
}
